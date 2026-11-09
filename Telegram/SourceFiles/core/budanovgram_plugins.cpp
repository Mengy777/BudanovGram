#include "core/budanovgram_plugins.h"

#include "api/api_editing.h"
#include "apiwrap.h"
#include "../../../budanovgram/plugins/manager.h"
#include "../../../budanovgram/plugins/privacy.h"
#include "core/application.h"
#include "core/launcher.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "rpl/combine.h"
#include "ui/widgets/popup_menu.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QSet>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtWidgets/QMessageBox>

namespace Core::Plugins {
namespace {

using Engine = BudanovGram::Plugins::Manager;

struct LocalOverride {
	QString plugin;
	QJsonObject ref;
	TextWithEntities text;
};

auto Started = false;
auto Overrides = std::map<QString, LocalOverride>();
auto PendingEvents = QMap<QString, QJsonObject>();
auto EventQueued = false;
auto PackageDownloads = QSet<QString>();
auto UiLockLifetime = rpl::lifetime();

QString Key(const QJsonObject &ref) {
	return ref.value("account").toString() + ':'
		+ ref.value("peer").toString() + ':' + ref.value("message").toString();
}

QJsonObject Ref(not_null<const HistoryItem*> item) {
	return {
		{ "account", QString::number(item->history()->session().uniqueId()) },
		{ "peer", QString::number(item->history()->peer->id.value) },
		{ "message", QString::number(item->id.bare) },
	};
}

Main::Session *Session(const QJsonObject &ref) {
	for (const auto &entry : App().domain().accounts()) {
		if (const auto session = entry.account->maybeSession()) {
			if (QString::number(session->uniqueId()) == ref.value("account").toString()) {
				return session;
			}
		}
	}
	return nullptr;
}

HistoryItem *Item(const QJsonObject &ref) {
	const auto session = Session(ref);
	if (!session) {
		return nullptr;
	}
	return session->data().message(
		PeerId(ref.value("peer").toString().toULongLong()),
		MsgId(ref.value("message").toString().toLongLong()));
}

void Refresh(const QJsonObject &ref) {
	if (const auto item = Item(ref)) {
		item->history()->owner().requestItemTextRefresh(item);
	}
}

void Handle(QString method, QJsonObject args, Engine::Reply done) {
	if (App().passcodeLocked() || App().screenIsLocked()) {
		done({}, u"Клиент заблокирован"_q);
		return;
	}
	const auto session = Session(args);
	if (!session) {
		done({}, u"Аккаунт недоступен"_q);
		return;
	}
	if (method == "users.resolve") {
		auto username = args.value("username").toString().trimmed();
		if (username.startsWith('@')) username.remove(0, 1);
		if (!QRegularExpression("^[A-Za-z][A-Za-z0-9_]{3,31}$").match(username).hasMatch()) {
			done({}, u"Некорректный юзернейм"_q);
			return;
		}
		const auto weak = base::make_weak(session);
		session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0), MTP_string(username), MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			if (!weak || App().passcodeLocked() || App().screenIsLocked()) {
				done({}, u"Аккаунт недоступен или клиент заблокирован"_q);
				return;
			}
			const auto &data = result.data();
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()));
			const auto user = peer ? peer->asUser() : nullptr;
			if (!user) {
				done({}, u"Этот юзернейм принадлежит не пользователю"_q);
				return;
			}
			done(QJsonObject{
				{ "id", QString::number(peerToUser(user->id).bare) },
				{ "username", user->username() },
				{ "name", user->name() },
			}, {});
		}).fail([=](const MTP::Error &error) {
			done({}, error.type());
		}).send();
		return;
	}
	const auto item = Item(args);
	const auto text = args.value("text").toString();
	if (text.size() > 32000) {
		done({}, u"Текст превышает 32000 символов"_q);
		return;
	}
	if (method == "messages.send") {
		const auto peer = PeerId(args.value("peer").toString().toULongLong());
		if (!peer || text.isEmpty()) {
			done({}, u"Нужны peer и непустой text"_q);
			return;
		}
		auto message = Api::MessageToSend(Api::SendAction(session->data().history(peer)));
		message.textWithTags.text = text;
		message.action.clearDraft = false;
		session->api().sendMessage(std::move(message));
		done(QJsonObject{ { "queued", true } }, {});
		return;
	} else if (!item) {
		done({}, u"Сообщение не загружено или удалено"_q);
		return;
	}
	if (method == "messages.deleteLocal") {
		if (!item->isRegular()) {
			done({}, u"Можно удалить только обычное сообщение из локального кэша"_q);
			return;
		}
		Overrides.erase(Key(args));
		item->destroy();
		done(true, {});

		return;
	}
	if (item->forbidsSaving()) {
		done({}, u"Для этого сообщения ограничен доступ к содержимому"_q);
		return;
	}
	if (method == "messages.get") {
		auto result = Ref(item);
		result.insert("text", item->originalText().text);
		result.insert("outgoing", item->out());
		done(result, {});
	} else if (method == "messages.setLocalText") {
		if (Overrides.size() >= 5000 && !Overrides.count(Key(args))) {
			done({}, u"Достигнут лимит локальных замен"_q);
			return;
		}
		Overrides[Key(args)] = { args.value("pluginId").toString(), Ref(item), { text } };
		Refresh(args);
		done(true, {});
	} else if (method == "messages.resetLocalText") {
		const auto i = Overrides.find(Key(args));
		if (i != Overrides.end() && i->second.plugin == args.value("pluginId").toString()) {
			Overrides.erase(i);
			Refresh(args);
		}
		done(true, {});
	} else if (method == "messages.edit") {
		if (!item->canBeEdited() || text.isEmpty()) {
			done({}, u"Сообщение нельзя редактировать"_q);
			return;
		}
		Api::EditTextMessage(item, TextWithEntities{ text }, {}, {},
			[=](mtpRequestId) { done(true, {}); },
			[=](const QString &error, mtpRequestId) { done({}, error); },
			false);
	} else {
		done({}, u"Неизвестный метод сообщений"_q);
	}
}

} // namespace

void Start() {
	BudanovGram::Privacy::Initialize(cWorkingDir());
	Started = true;
	Engine::Instance().setCleanup([](QString plugin) {
		auto refs = QList<QJsonObject>();
		for (auto i = Overrides.begin(); i != Overrides.end();) {
			if (i->second.plugin == plugin) {
				refs.push_back(i->second.ref);
				i = Overrides.erase(i);
			} else {
				++i;
			}
		}
		for (const auto &ref : refs) Refresh(ref);
	});
	Engine::Instance().start(cWorkingDir(), Handle,
		Launcher::Instance().arguments().contains(u"-no-plugins"_q));
	rpl::combine(
		App().passcodeLockValue(),
		App().screenIsLockedValue()
	) | rpl::on_next([](bool passcode, bool screen) {
		Engine::Instance().setLocked(passcode || screen);
	}, UiLockLifetime);
	if (Launcher::Instance().arguments().contains(u"-plugins"_q)) {
		QTimer::singleShot(0, QCoreApplication::instance(), [] { Show(); });
	}
}

void Show(QWidget *parent) {
	Engine::Instance().show(parent);
}

bool OpenDocument(not_null<DocumentData*> document, FullMsgId context) {
	if (!document->filename().endsWith(u".bgplugin"_q, Qt::CaseInsensitive)) {
		return false;
	}
	if (App().passcodeLocked() || App().screenIsLocked()) {
		return true;
	}
	if (document->forbidsFileSave()) {
		QMessageBox::warning(nullptr, u"Плагин BudanovGram"_q,
			u"Для этого файла ограничено сохранение."_q);
		return true;
	}
	if (document->size <= 0 || document->size > 1024 * 1024) {
		QMessageBox::warning(nullptr, u"Плагин BudanovGram"_q,
			u"Файл плагина должен быть непустым и не больше 1 МБ."_q);
		return true;
	}
	const auto key = QString::number(document->session().uniqueId())
		+ ':' + QString::number(document->id);
	if (PackageDownloads.contains(key)) {
		return true;
	}
	PackageDownloads.insert(key);
	const auto session = base::make_weak(&document->session());
	const auto media = document->createMediaView();
	const auto timer = new QTimer(QCoreApplication::instance());
	const auto elapsed = std::make_shared<QElapsedTimer>();
	elapsed->start();
	QObject::connect(timer, &QTimer::timeout, timer, [=] {
		if (!session || App().passcodeLocked() || App().screenIsLocked()
			|| elapsed->elapsed() > 60000 || document->cancelled()
			|| document->status == FileDownloadFailed) {
			PackageDownloads.remove(key);
			timer->stop();
			timer->deleteLater();
			return;
		}
		if (!media->loaded(true)) {
			return;
		}
		PackageDownloads.remove(key);
		timer->stop();
		timer->deleteLater();
		auto bytes = media->bytes();
		if (bytes.isEmpty()) {
			const auto &location = document->location(true);
			if (location.accessEnable()) {
				const auto guard = gsl::finally([&] { location.accessDisable(); });
				QFile file(location.name());
				if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
					bytes = file.read(1024 * 1024 + 1);
				}
			}
		}
		if (bytes.isEmpty() || bytes.size() > 1024 * 1024) {
			QMessageBox::warning(nullptr, u"Плагин BudanovGram"_q,
				u"Не удалось прочитать файл плагина."_q);
			return;
		}
		Engine::Instance().installPackage(bytes);
	});
	if (!media->loaded(true)) {
		document->forceToCache(true);
		document->save(context, QString());
	}
	timer->start(100);
	return true;
}

void AddMessageCommands(not_null<Ui::PopupMenu*> menu, not_null<HistoryItem*> item) {
	if (item->isService() || item->forbidsSaving()) {
		return;
	}
	auto context = Ref(item);
	context.insert("text", item->originalText().text);
	context.insert("outgoing", item->out());
	for (const auto &command : Engine::Instance().commands("message")) {
		menu->addAction(command.title, [=] {
			Engine::Instance().invoke(command, context);
		});
	}
}

void ObserveMessage(not_null<HistoryItem*> item, const QString &text, bool sent) {
	if (!Started || item->isService() || item->forbidsSaving()
		|| App().passcodeLocked() || App().screenIsLocked()) {
		return;
	}
	auto data = Ref(item);
	Overrides.erase(Key(data));
	data.insert("text", text);
	data.insert("outgoing", item->out());
	data.insert("date", double(item->date()));
	data.insert("sent", sent || PendingEvents.value(Key(data)).value("sent").toBool());
	PendingEvents.insert(Key(data), data);
	if (PendingEvents.size() > 1000) PendingEvents.erase(PendingEvents.begin());
	if (EventQueued) {
		return;
	}
	EventQueued = true;
	QTimer::singleShot(0, QCoreApplication::instance(), [] {
		EventQueued = false;
		const auto events = std::exchange(PendingEvents, {});
		if (App().passcodeLocked() || App().screenIsLocked()) {
			return;
		}
		for (const auto &data : events) {
			if (const auto item = Item(data)) {
				if (!item->forbidsSaving()) {
					Engine::Instance().publish("message.loaded", data);
					if (data.value("sent").toBool()) {
						Engine::Instance().publish("message.sent", data);
					}
				}
			}
		}
	});
}

const TextWithEntities *LocalText(not_null<const HistoryItem*> item) {
	if (!Started || Overrides.empty() || item->forbidsSaving()) {
		return nullptr;
	}
	const auto i = Overrides.find(Key(Ref(item)));
	return (i == Overrides.end()) ? nullptr : &i->second.text;
}

} // namespace Core::Plugins
