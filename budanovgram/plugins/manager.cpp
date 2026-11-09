#include "manager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QSysInfo>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

#include <algorithm>
#include <map>

namespace BudanovGram::Plugins {
namespace {

constexpr auto MaxPackage = 1024 * 1024;
constexpr auto MaxState = 2 * 1024 * 1024;
constexpr auto MaxResponse = 1024 * 1024;
const auto Permissions = QSet<QString>{
	"messages.read", "messages.events", "messages.local", "messages.send",
	"messages.edit", "network", "clipboard.read", "clipboard.write",
	"ui", "ui.style", "ui.labels",
};
const auto Capabilities = QJsonArray{
	"commands.message", "commands.main", "messages.get", "messages.events",
	"messages.setLocalText", "messages.resetLocalText", "messages.send", "messages.edit",
	"network.fetch", "settings", "storage", "resources", "timers",
	"clipboard", "ui.panel", "ui.notify", "ui.style", "ui.labels", "ui.openUrl",
	"client.locked",
};

QString S(const char *text) {
	return QString::fromUtf8(text);
}

bool Save(const QString &path, const QJsonObject &object) {
	QSaveFile file(path);
	const auto data = QJsonDocument(object).toJson();
	return file.open(QIODevice::WriteOnly)
		&& file.write(data) == data.size()
		&& file.commit();
}

QJsonObject Read(const QString &path, int limit = MaxState) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

QString Protect(const QString &value, bool decrypt) {
#ifdef Q_OS_WIN
	auto bytes = decrypt ? QByteArray::fromBase64(value.toLatin1()) : value.toUtf8();
	DATA_BLOB input = { DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data()) };
	DATA_BLOB output = {};
	const auto ok = decrypt
		? CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
		: CryptProtectData(&input, L"BudanovGram plugin setting", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
	if (!ok) {
		return {};
	}
	const auto result = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
	SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	return decrypt ? QString::fromUtf8(result) : QString::fromLatin1(result.toBase64());
#else
	return {};
#endif
}

QString Validate(const QJsonObject &package) {
	const auto m = package.value("manifest").toObject();
	const auto id = m.value("id").toString();
	if (!QRegularExpression("^[a-z][a-z0-9.-]{1,78}[a-z0-9-]$").match(id).hasMatch()
		|| QRegularExpression("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\.|$)").match(id).hasMatch()) {
		return S("Некорректный идентификатор плагина.");
	}
	const auto version = m.value("version").toString();
	if (!QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(version).hasMatch()
		|| version.size() > 32
		|| m.value("apiVersion").toInt() != 1
		|| m.value("name").toString().isEmpty()
		|| m.value("name").toString().size() > 120
		|| m.value("author").toString().size() > 120
		|| m.value("description").toString().size() > 8000
		|| !package.value("script").isString()
		|| package.value("script").toString().isEmpty()) {
		return S("Нужны название, версия x.y.z, apiVersion: 1 и script.");
	}
	if (!m.value("permissions").isArray() || m.value("permissions").toArray().size() > 20) {
		return S("Некорректный список разрешений.");
	}
	for (const auto &permission : m.value("permissions").toArray()) {
		if (!Permissions.contains(permission.toString())) {
			return S("Неизвестное разрешение: ") + permission.toString();
		}
	}
	for (const auto &host : m.value("networkHosts").toArray()) {
		const auto name = host.toString();
		if (name != "*" && !QRegularExpression("^[a-z0-9.-]+$").match(name).hasMatch()) {
			return S("networkHosts: укажи домены без протокола и пути или *.");
		}
	}
	for (const auto &capability : m.value("requires").toArray()) {
		if (!Capabilities.contains(capability)) {
			return S("Клиент не поддерживает возможность: ") + capability.toString();
		}
	}
	const auto fields = m.value("settings").toArray();
	if (fields.size() > 100) {
		return S("Слишком много настроек.");
	}
	auto keys = QSet<QString>();
	for (const auto &entry : fields) {
		const auto field = entry.toObject();
		const auto key = field.value("key").toString();
		const auto type = field.value("type").toString();
		if (!QRegularExpression("^[a-zA-Z0-9_.-]{1,80}$").match(key).hasMatch()
			|| keys.contains(key)
			|| !QStringList{"boolean", "number", "string", "secret", "select"}.contains(type)) {
			return S("Некорректная схема настроек.");
		}
		keys.insert(key);
	}
	return {};
}

QString PermissionText(const QJsonObject &manifest) {
	auto lines = QStringList();
	const auto descriptions = QMap<QString, QString>{
		{"messages.read", S("Читать загруженные сообщения")},
		{"messages.events", S("Получать текст сообщений всех открытых аккаунтов")},
		{"messages.local", S("Менять отображение текста только у тебя")},
		{"messages.send", S("Отправлять сообщения от лица аккаунта")},
		{"messages.edit", S("Редактировать сообщения в Telegram")},
		{"network", S("Обращаться к внешним API и передавать им данные")},
		{"clipboard.read", S("Читать буфер обмена")},
		{"clipboard.write", S("Записывать в буфер обмена")},
		{"ui", S("Открывать панели и HTTPS-ссылки")},
		{"ui.style", S("Менять оформление стандартных элементов")},
		{"ui.labels", S("Менять поддержанные подписи интерфейса")},
	};
	for (const auto &p : manifest.value("permissions").toArray()) {
		lines.push_back(descriptions.value(p.toString(), p.toString()) + " [" + p.toString() + ']');
	}
	if (lines.contains("network")) {
		auto hosts = QStringList();
		for (const auto &host : manifest.value("networkHosts").toArray()) {
			hosts.push_back(host.toString());
		}
		lines.push_back(S("Сетевые адреса: ") + hosts.join(", "));
	}
	return lines.isEmpty() ? S("Без дополнительных разрешений") : lines.join('\n');
}

bool ConfirmInstall(const QJsonObject &manifest, QWidget *parent) {
	QDialog card(parent);
	card.setObjectName("PluginInstallCard");
	card.setProperty("bgPluginUi", true);
	card.setWindowTitle(S("Установка плагина · BudanovGram"));
	card.resize(540, 530);
	card.setStyleSheet(
		"QDialog { background:#101923; color:#e6eef7; }"
		"QLabel { color:#e6eef7; }"
		"QPlainTextEdit { background:#142232; color:#e6eef7; border:1px solid #293c50; border-radius:6px; padding:8px; }"
		"QPushButton { background:#203b53; color:#e6eef7; border:1px solid #31536d; border-radius:6px; padding:10px 20px; }"
		"QPushButton#InstallPlugin { background:#087fa8; color:white; }"
	);
	const auto layout = new QVBoxLayout(&card);
	const auto title = new QLabel(manifest.value("name").toString(), &card);
	title->setObjectName("PluginCardName");
	title->setTextFormat(Qt::PlainText);
	title->setWordWrap(true);
	auto font = title->font();
	font.setPointSize(17);
	font.setBold(true);
	title->setFont(font);
	layout->addWidget(title);
	const auto meta = new QLabel(S("Версия ") + manifest.value("version").toString()
		+ " · " + manifest.value("id").toString(), &card);
	meta->setTextFormat(Qt::PlainText);
	layout->addWidget(meta);
	const auto author = new QLabel(S("Создатель: ") + manifest.value("author").toString(S("Не указан")), &card);
	author->setObjectName("PluginCardAuthor");
	author->setTextFormat(Qt::PlainText);
	author->setTextInteractionFlags(Qt::TextSelectableByMouse);
	author->setWordWrap(true);
	layout->addWidget(author);
	const auto description = new QPlainTextEdit(&card);
	description->setObjectName("PluginCardDescription");
	description->setReadOnly(true);
	description->setPlainText(manifest.value("description").toString(S("Описание не указано")));
	description->setMaximumHeight(125);
	layout->addWidget(description);
	layout->addWidget(new QLabel(S("Запрашиваемые возможности:"), &card));
	const auto permissions = new QPlainTextEdit(PermissionText(manifest), &card);
	permissions->setReadOnly(true);
	layout->addWidget(permissions);
	const auto note = new QLabel(S("Имя создателя указано в файле. После установки плагин появится в меню «Плагины» выключенным."), &card);
	note->setWordWrap(true);
	layout->addWidget(note);
	const auto buttons = new QDialogButtonBox(&card);
	const auto install = buttons->addButton(S("Установить"), QDialogButtonBox::AcceptRole);
	install->setObjectName("InstallPlugin");
	buttons->addButton(S("Отмена"), QDialogButtonBox::RejectRole);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &card, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &card, &QDialog::reject);
	layout->addWidget(buttons);
	return card.exec() == QDialog::Accepted;
}

} // namespace

struct Manager::Private : QObject {
	struct Plugin {
		QString id;
		QJsonObject package;
		QJsonObject state;
		QJsonObject settings;
		QPointer<QProcess> process;
		QPointer<QObject> runtime;
		QByteArray buffer;
		QList<QJsonObject> commands;
		QStringList logs;
		QString status;
		QString stylesheet;
		QJsonObject labels;
		int pendingFrames = 0;
		int requests = 0;
		int inFlight = 0;
		int stuckTicks = 0;
		QMap<QString, QTimer*> timers;
		QList<QPointer<QDialog>> panels;
	};
	using Ptr = std::shared_ptr<Plugin>;
	QString root;
	std::map<QString, Ptr> plugins;
	ClientHandler client;
	std::function<void(QString)> cleanup;
	QPointer<QDialog> window;
	QPointer<QListWidget> installed;
	QPointer<QLineEdit> installedSearch;
	QPointer<QPlainTextEdit> logView;
	QTimer rateTimer;
	QTimer watchdog;
	QString originalStyle;
	bool started = false;
	bool shuttingDown = false;
	bool locked = false;
	QString hostExecutable;
	QJsonObject preferences;

	QString path(const Ptr &p, const QString &name) const;

	void log(const Ptr &p, const QString &text);

	bool persist(const Ptr &p);

	bool allowed(const Ptr &p, const QString &permission) const;

	void write(const Ptr &p, QJsonObject message);

	void event(const Ptr &p, QString name, QJsonObject data);

	void styles();

	void stop(const Ptr &p, QString reason = {});

	void startPlugin(const Ptr &p);

	void fetch(QObject *owner, QJsonObject options, std::function<void(QJsonValue, QString)> done);

	void handle(const Ptr &p, const QJsonObject &message);
	void showPanel(const Ptr &p, const QJsonObject &panel);
	void settingsDialog(const Ptr &p, QWidget *parent);
	void showWindow(QWidget *parent);
	bool install(const QByteArray &data, QWidget *parent, const QJsonObject &expected = {});
	void refreshList();
	void focusPlugin(const QString &id);

};

QString Manager::Private::path(const Ptr &p, const QString &name) const {
	return root + '/' + p->id + '/' + name;
}

void Manager::Private::log(const Ptr &p, const QString &text) {
	p->logs.push_back(text.left(4000));
	while (p->logs.size() > 150) {
		p->logs.removeFirst();
	}
}

bool Manager::Private::persist(const Ptr &p) {
	auto state = p->state;
	auto settings = QJsonObject();
	auto memorySettings = QJsonObject();
	auto types = QJsonObject();
	const auto previousTypes = state.value("settingTypes").toObject();
	const auto previousSecrets = state.value("secretSettings").toObject();
	auto secrets = QJsonObject();
	for (const auto &entry : p->package.value("manifest").toObject().value("settings").toArray()) {
		const auto field = entry.toObject();
		const auto key = field.value("key").toString();
		const auto type = field.value("type").toString();
		const auto previous = previousTypes.value(key).toString(previousSecrets.contains(key) ? "secret" : "");
		const auto changed = !previous.isEmpty() && previous != type;
		const auto value = !changed && p->settings.contains(key) ? p->settings.value(key) : field.value("default");
		memorySettings.insert(key, value);
		types.insert(key, type);
		if (type == "secret") {
			const auto plain = value.toString();
			if (!plain.isEmpty()) {
				const auto encrypted = Protect(plain, false);
				if (encrypted.isEmpty()) {
					return false;
				}
				secrets.insert(key, encrypted);
			}
		} else {
			settings.insert(key, value);
		}
	}
	state.insert("settings", settings);
	state.insert("secretSettings", secrets);
	state.insert("settingTypes", types);
	if (QJsonDocument(state).toJson().size() > MaxState) {
		return false;
	}
	if (!Save(path(p, "state.json"), state)) return false;
	p->state = state;
	p->settings = memorySettings;
	return true;
}

bool Manager::Private::allowed(const Ptr &p, const QString &permission) const {
	return p->package.value("manifest").toObject().value("permissions").toArray().contains(permission);
}

void Manager::Private::write(const Ptr &p, QJsonObject message) {
	if (!p->process || p->process->state() != QProcess::Running) {
		return;
	}
	if (p->process->bytesToWrite() > 2 * MaxState || p->pendingFrames > 256) {
		stop(p, S("Остановлен: переполнена очередь событий"));
		return;
	}
	++p->pendingFrames;
	p->process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void Manager::Private::event(const Ptr &p, QString name, QJsonObject data) {
	write(p, {{"type", "event"}, {"name", name}, {"data", data}});
}

void Manager::Private::styles() {
	auto result = originalStyle;
	for (const auto &[id, p] : plugins) {
		result += '\n' + p->stylesheet;
	}
	qApp->setStyleSheet(result);
	if (window) {
		window->setStyleSheet(window->property("bgBaseStyle").toString() + '\n' + result);
	}
}

void Manager::Private::stop(const Ptr &p, QString reason) {
	const auto runtime = p->runtime.data();
	p->runtime = nullptr;
	if (p->process) {
		p->process->disconnect(this);
		p->process->kill();
		p->process->waitForFinished(1500);
	}
	p->process = nullptr;
	for (const auto timer : p->timers) timer->stop();
	for (const auto &panel : p->panels) if (panel) panel->close();
	p->panels.clear();
	if (runtime) runtime->deleteLater();
	p->commands.clear();
	p->timers.clear();
	p->stylesheet.clear();
	p->labels = {};
	p->buffer.clear();
	p->pendingFrames = 0;
	p->inFlight = 0;
	p->status = reason.isEmpty() ? S("Выключен") : reason;
	p->state.insert("running", false);
	if (!reason.isEmpty()) {
		p->state.insert("enabled", false);
		log(p, reason);
	}
	persist(p);
	if (cleanup) {
		cleanup(p->id);
	}
	styles();
}

void Manager::Private::startPlugin(const Ptr &p) {
	stop(p);
	const auto executable = hostExecutable;
	if (!QFile::exists(executable)) {
		stop(p, S("Нет BudanovPluginHost рядом с клиентом"));
		return;
	}
	p->state.insert("running", true);
	if (!persist(p)) {
		stop(p, S("Ошибка сохранения состояния"));
		return;
	}
	p->runtime = new QObject(this);
	p->process = new QProcess(p->runtime);
	const auto process = p->process.data();
	process->setWorkingDirectory(path(p, ""));
#ifdef Q_OS_WIN
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_NO_WINDOW;
	});
#endif
	connect(process, &QProcess::started, this, [=] {
		p->status = S("Загрузка…");
		write(p, {{"type", "load"}, {"manifest", p->package.value("manifest")},
			{"script", p->package.value("script")}, {"settings", p->settings}});
	});
	connect(process, &QProcess::readyReadStandardOutput, this, [=] {
		p->buffer += process->readAllStandardOutput();
		if (p->buffer.size() > 2 * MaxState) {
			stop(p, S("Некорректный ответ движка"));
			return;
		}
		while (p->process && p->buffer.contains('\n')) {
			const auto end = p->buffer.indexOf('\n');
			const auto frame = p->buffer.left(end);
			p->buffer.remove(0, end + 1);
			const auto message = QJsonDocument::fromJson(frame).object();
			const auto type = message.value("type").toString();
			if (type == "idle") {
				p->pendingFrames = std::max(0, p->pendingFrames - 1);
			} else if (type == "ready") {
				p->status = S("Работает");
			} else if (type == "log") {
				log(p, message.value("text").toString());
			} else if (type == "request") {
				handle(p, message);
			} else {
				stop(p, S("Неизвестный ответ движка"));
			}
		}
	});
	connect(process, &QProcess::readyReadStandardError, this, [=] {
		log(p, QString::fromUtf8(process->readAllStandardError().left(8000)));
	});
	connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [=](int code, auto) {
		stop(p, S("Движок остановлен, код ") + QString::number(code));
	});
	connect(process, &QProcess::errorOccurred, this, [=](auto error) {
		if (error == QProcess::FailedToStart) {
			stop(p, S("Не удалось запустить движок"));
		}
	});
	process->start(executable, QStringList());
}

void Manager::Private::fetch(QObject *owner, QJsonObject options, std::function<void(QJsonValue, QString)> done) {
	const auto url = QUrl(options.value("url").toString());
	if (url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) {
		done({}, S("Нужен HTTPS URL без логина и пароля"));
		return;
	}
	const auto manager = new QNetworkAccessManager(owner);
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setRawHeader("User-Agent", "BudanovGram-Plugins/1");
	const auto headers = options.value("headers").toObject();
	for (auto i = headers.begin(); i != headers.end(); ++i) {
		if (i.key().contains('\r') || i.key().contains('\n')
			|| i.value().toString().contains('\r') || i.value().toString().contains('\n')
			|| i.key().compare("Host", Qt::CaseInsensitive) == 0) {
			manager->deleteLater();
			done({}, S("Некорректный HTTP заголовок"));
			return;
		}
		request.setRawHeader(i.key().toUtf8(), i.value().toString().toUtf8());
	}
	const auto method = options.value("method").toString("GET").toUpper().toLatin1();
	if (!QList<QByteArray>{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"}.contains(method)) {
		manager->deleteLater();
		done({}, S("Неизвестный HTTP метод"));
		return;
	}
	const auto body = options.value("body").toString().toUtf8();
	if (body.size() > MaxResponse) {
		manager->deleteLater();
		done({}, S("Слишком большой HTTP запрос"));
		return;
	}
	const auto reply = manager->sendCustomRequest(request, method, body);
	const auto data = std::make_shared<QByteArray>();
	const auto tooLarge = std::make_shared<bool>(false);
	connect(reply, &QNetworkReply::readyRead, manager, [=] {
		*data += reply->readAll();
		if (data->size() > MaxResponse) {
			*tooLarge = true;
			reply->abort();
		}
	});
	QTimer::singleShot(20000, reply, [=] { reply->abort(); });
	connect(reply, &QNetworkReply::finished, manager, [=] {
		*data += reply->readAll();
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (*tooLarge || data->size() > MaxResponse) {
			done({}, S("Ответ API превышает 1 МБ"));
		} else if (!status) {
			done({}, S("Сетевая ошибка: ") + reply->errorString());
		} else {
			auto responseHeaders = QJsonObject();
			for (const auto &pair : reply->rawHeaderPairs()) {
				responseHeaders.insert(QString::fromLatin1(pair.first), QString::fromLatin1(pair.second));
			}
			done(QJsonObject{{"status", status}, {"headers", responseHeaders}, {"body", QString::fromUtf8(*data)}}, {});
		}
		manager->deleteLater();
	});
}


void Manager::Private::handle(const Ptr &p, const QJsonObject &message) {
	if (++p->requests > 300 || p->inFlight >= 128) {
		stop(p, S("Остановлен: слишком много запросов к клиенту"));
		return;
	}
	++p->inFlight;
	const auto generation = p->runtime;
	const auto id = message.value("id");
	const auto method = message.value("method").toString();
	const auto args = message.value("params").toObject();
	const auto answered = std::make_shared<bool>(false);
	const auto done = [=](QJsonValue result, QString error = {}) {
		if (*answered || !generation || p->runtime != generation) {
			return;
		}
		*answered = true;
		--p->inFlight;
		write(p, {{"type", "response"}, {"id", id}, {"result", result}, {"error", error}});
	};
	const auto permission = [&]() -> QString {
		if (method.startsWith("messages.")) {
			if (method == "messages.get") return "messages.read";
			if (method == "messages.send") return "messages.send";
			if (method == "messages.edit") return "messages.edit";
			return "messages.local";
		} else if (method == "network.fetch") return "network";
		else if (method == "clipboard.read") return "clipboard.read";
		else if (method == "clipboard.write") return "clipboard.write";
		else if (method == "ui.style") return "ui.style";
		else if (method == "ui.labels") return "ui.labels";
		else if (method.startsWith("ui.")) return "ui";
		return {};
	}();
	if (locked && (method.startsWith("messages.")
		|| method.startsWith("clipboard.") || method.startsWith("ui."))) {
		done({}, S("Клиент заблокирован"));
		return;
	}
	if (!permission.isEmpty() && !allowed(p, permission)) {
		done({}, S("Разрешение не выдано: ") + permission);
		return;
	}
	if (method == "client.info") {
		done(QJsonObject{{"name", "BudanovGram"}, {"pluginApiVersion", 1},
			{"platform", QSysInfo::productType()}, {"runtime", "QuickJS-NG"},
			{"capabilities", Capabilities}, {"locked", locked}});
	} else if (method == "commands.register") {
		const auto key = args.value("id").toString();
		const auto scope = args.value("scope").toString();
		if (!QRegularExpression("^[a-zA-Z0-9._-]{1,80}$").match(key).hasMatch()
			|| args.value("title").toString().isEmpty()
			|| !QStringList{"message", "main"}.contains(scope)
			|| (scope == "message" && !allowed(p, "messages.read"))
			|| p->commands.size() >= 64) {
			done({}, S("Команда некорректна или нет messages.read"));
			return;
		}
		for (auto &command : p->commands) {
			if (command.value("id") == key) {
				command = args;
				done(true);
				return;
			}
		}
		p->commands.push_back(args);
		done(true);
	} else if (method.startsWith("storage.")) {
		const auto key = args.value("key").toString();
		if (key.isEmpty() || key.size() > 200) {
			done({}, S("Некорректный ключ"));
			return;
		}
		auto storage = p->state.value("storage").toObject();
		if (method == "storage.get") {
			done(storage.contains(key) ? storage.value(key) : args.value("fallback"));
		} else if (method == "storage.set" || method == "storage.remove") {
			const auto before = p->state;
			if (method == "storage.set") storage.insert(key, args.value("value"));
			else storage.remove(key);
			p->state.insert("storage", storage);
			if (!persist(p)) {
				p->state = before;
				done({}, S("Не удалось сохранить данные (лимит 2 МБ)"));
			} else done(true);
		} else done({}, S("Неизвестный метод хранилища"));
	} else if (method == "settings.set") {
		const auto key = args.value("key").toString();
		auto field = QJsonObject();
		for (const auto &entry : p->package.value("manifest").toObject().value("settings").toArray()) {
			if (entry.toObject().value("key") == key) field = entry.toObject();
		}
		if (field.isEmpty()) {
			done({}, S("Настройка отсутствует в manifest.settings"));
			return;
		}
		const auto value = args.value("value");
		const auto type = field.value("type").toString();
		const auto valid = type == "boolean" ? value.isBool()
			: type == "number" ? (value.isDouble()
				&& value.toDouble() >= field.value("min").toDouble(-1000000)
				&& value.toDouble() <= field.value("max").toDouble(1000000))
			: (value.isString() && value.toString().size() <= 16000
				&& (type != "select" || field.value("options").toArray().contains(value)));
		if (!valid) {
			done({}, S("Значение не соответствует схеме настройки"));
			return;
		}
		const auto before = p->settings;
		p->settings.insert(key, args.value("value"));
		if (!persist(p)) {
			p->settings = before;
			done({}, S("Ошибка сохранения настройки"));
		} else {
			event(p, "settings.changed", p->settings);
			done(true);
		}
	} else if (method == "resources.read") {
		const auto resources = p->package.value("resources").toObject();
		const auto key = args.value("name").toString();
		if (!resources.contains(key)) done({}, S("Ресурс не найден"));
		else done(resources.value(key));
	} else if (method == "network.fetch") {
		const auto host = QUrl(args.value("url").toString()).host().toLower();
		const auto hosts = p->package.value("manifest").toObject().value("networkHosts").toArray();
		if (!hosts.contains(host) && !hosts.contains("*")) {
			done({}, S("Домен отсутствует в networkHosts: ") + host);
			return;
		}
		if (p->runtime->findChildren<QNetworkAccessManager*>().size() >= 8) {
			done({}, S("Уже выполняются 8 сетевых запросов"));
			return;
		}
		fetch(p->runtime, args, done);
	} else if (method == "clipboard.read") {
		done(qApp->clipboard()->text().left(100000));
	} else if (method == "clipboard.write") {
		qApp->clipboard()->setText(args.value("text").toString().left(100000));
		done(true);
	} else if (method == "ui.notify") {
		if (std::count_if(p->panels.cbegin(), p->panels.cend(), [](const auto &d) { return !d.isNull(); }) >= 8) {
			done({}, S("Слишком много открытых окон"));
			return;
		}
		showPanel(p, {{"title", p->package.value("manifest").toObject().value("name")},
			{"text", args.value("text")}});
		done(true);
	} else if (method == "ui.panel") {
		if (std::count_if(p->panels.cbegin(), p->panels.cend(), [](const auto &d) { return !d.isNull(); }) >= 8) {
			done({}, S("Слишком много открытых окон"));
			return;
		}
		showPanel(p, args);
		done(true);
	} else if (method == "ui.openUrl") {
		const auto url = QUrl(args.value("url").toString());
		if (url.scheme() != "https" || !url.userInfo().isEmpty()) {
			done({}, S("Разрешены только HTTPS ссылки"));
		} else done(QDesktopServices::openUrl(url));
	} else if (method == "ui.style") {
		const auto sheet = args.value("stylesheet").toString();
		if (sheet.size() > 16000 || sheet.contains(QRegularExpression("url\\s*\\(", QRegularExpression::CaseInsensitiveOption))) {
			done({}, S("Стиль должен быть до 16 КБ и без url()"));
		} else {
			p->stylesheet = sheet;
			styles();
			done(true);
		}
	} else if (method == "ui.labels") {
		p->labels = args.value("labels").toObject();
		done(true);
	} else if (method == "timers.start") {
		const auto key = args.value("id").toString();
		if (key.isEmpty() || p->timers.size() >= 32 || p->timers.contains(key)) {
			done({}, S("Некорректный таймер или достигнут лимит 32"));
			return;
		}
		const auto timer = new QTimer(p->runtime);
		p->timers.insert(key, timer);
		const auto repeat = args.value("repeat").toBool();
		timer->setSingleShot(!repeat);
		connect(timer, &QTimer::timeout, this, [=] {
			event(p, "timer", {{"id", key}});
			if (!repeat) {
				p->timers.remove(key);
				timer->deleteLater();
			}
		});
		timer->start(std::clamp(args.value("delay").toInt(), 50, 86400000));
		done(true);
	} else if (method == "timers.stop") {
		delete p->timers.take(args.value("id").toString());
		done(true);
	} else if (method.startsWith("messages.") && client) {
		auto params = args;
		params.insert("pluginId", p->id);
		client(method, params, done);
	} else done({}, S("Неизвестный API метод: ") + method);
}

void Manager::Private::showPanel(const Ptr &p, const QJsonObject &panel) {
	p->panels.erase(std::remove_if(p->panels.begin(), p->panels.end(), [](const auto &d) { return d.isNull(); }), p->panels.end());
	const auto dialog = new QDialog(window);
	dialog->setProperty("bgPluginUi", true);
	p->panels.push_back(dialog);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(panel.value("title").toString().left(160));
	dialog->resize(560, 380);
	const auto layout = new QVBoxLayout(dialog);
	const auto text = new QPlainTextEdit(dialog);
	text->setReadOnly(true);
	text->setPlainText(panel.value("text").toString().left(100000));
	layout->addWidget(text);
	for (const auto &entry : panel.value("buttons").toArray()) {
		if (layout->count() > 16) break;
		const auto action = entry.toObject();
		const auto button = new QPushButton(action.value("title").toString().left(120), dialog);
		layout->addWidget(button);
		connect(button, &QPushButton::clicked, dialog, [=] {
			event(p, "ui.action", {{"panel", panel.value("id")}, {"action", action.value("id")}});
		});
	}
	const auto runtime = p->runtime;
	connect(runtime, &QObject::destroyed, dialog, &QDialog::close);
	dialog->setProperty("bgPluginId", p->id);
	dialog->show();
}

bool Manager::Private::install(const QByteArray &data, QWidget *parent, const QJsonObject &expected) {
	if (locked) return false;
	const auto package = QJsonDocument::fromJson(data).object();
	auto error = Validate(package);
	const auto manifest = package.value("manifest").toObject();
	if (data.size() > MaxPackage) error = S("Пакет превышает 1 МБ.");
	if (!expected.isEmpty()
		&& (manifest.value("id") != expected.value("id")
			|| manifest.value("version") != expected.value("version"))) {
		error = S("Идентификатор или версия не совпадают с каталогом.");
	}
	if (!error.isEmpty()) {
		QMessageBox::warning(parent, S("Установка плагина"), error);
		return false;
	}
	if (!ConfirmInstall(manifest, parent)) return false;
	const auto id = manifest.value("id").toString();
	auto p = plugins.count(id) ? plugins[id] : std::make_shared<Plugin>();
	p->id = id;
	const auto directory = root + '/' + id;
	if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) {
		QMessageBox::warning(parent, S("Ошибка"), S("Нет доступа к каталогу плагина."));
		return false;
	}
	if (p->package.isEmpty()) {
		p->state = Read(directory + "/state.json");
		p->settings = p->state.value("settings").toObject();
		const auto secrets = p->state.value("secretSettings").toObject();
		for (auto i = secrets.begin(); i != secrets.end(); ++i) {
			p->settings.insert(i.key(), Protect(i.value().toString(), true));
		}
	}
	if (QFile::exists(directory + "/plugin.bgplugin")) {
		QFile::remove(directory + "/previous.bgplugin");
		if (!QFile::copy(directory + "/plugin.bgplugin", directory + "/previous.bgplugin")) {
			QMessageBox::warning(parent, S("Ошибка"), S("Не удалось сохранить предыдущую версию плагина."));
			return false;
		}
	}
	if (!Save(directory + "/plugin.bgplugin", package)) {
		QMessageBox::warning(parent, S("Ошибка"), S("Не удалось записать пакет."));
		return false;
	}
	if (!p->package.isEmpty()) stop(p);
	p->package = package;
	p->state.insert("enabled", false);
	p->state.insert("running", false);
	p->status = S("Выключен");
	for (const auto &entry : manifest.value("settings").toArray()) {
		const auto field = entry.toObject();
		const auto key = field.value("key").toString();
		if (!p->settings.contains(key)) p->settings.insert(key, field.value("default"));
	}
	plugins[id] = p;
	if (!persist(p)) {
		QMessageBox::warning(parent, S("Ошибка"), S("Пакет установлен, но его настройки не удалось сохранить."));
	}
	refreshList();
	focusPlugin(id);
	return true;
}

void Manager::Private::focusPlugin(const QString &id) {
	if (!window || !installed) return;
	if (installedSearch) installedSearch->clear();
	window->findChild<QTabWidget*>()->setCurrentIndex(0);
	for (int i = 0; i < installed->count(); ++i) {
		if (installed->item(i)->data(Qt::UserRole).toString() == id) installed->setCurrentRow(i);
	}
}

void Manager::Private::refreshList() {
	if (!installed) return;
	const auto selected = installed->currentItem()
		? installed->currentItem()->data(Qt::UserRole).toString() : QString();
	installed->clear();
	for (const auto &[id, p] : plugins) {
		const auto manifest = p->package.value("manifest").toObject();
		const auto item = new QListWidgetItem(manifest.value("name").toString()
			+ " · " + manifest.value("version").toString() + "\n" + p->status, installed);
		item->setData(Qt::UserRole, id);
		if (installedSearch) item->setHidden(!item->text().contains(installedSearch->text(), Qt::CaseInsensitive));
		if (id == selected) installed->setCurrentItem(item);
	}
}

void Manager::Private::showWindow(QWidget *parent) {
	if (locked) return;
	if (window) {
		window->show();
		window->raise();
		window->activateWindow();
		return;
	}
	const auto dialog = new QDialog(parent);
	window = dialog;
	dialog->setProperty("bgPluginUi", true);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(S("Плагины · BudanovGram"));
	dialog->resize(820, 620);
	dialog->setStyleSheet(
		"QDialog { background:#101923; color:#e6eef7; }"
		"QLabel,QCheckBox { color:#e6eef7; }"
		"QTabWidget::pane { border:1px solid #293c50; border-radius:8px; }"
		"QTabBar::tab { background:#172536; color:#aebfd2; padding:10px 18px; }"
		"QTabBar::tab:selected { color:#38d6ee; background:#20344a; }"
		"QListWidget,QPlainTextEdit,QLineEdit,QSpinBox,QComboBox {"
		" background:#142232; color:#e6eef7; border:1px solid #293c50; border-radius:6px; padding:7px; }"
		"QListWidget::item { padding:10px; }"
		"QListWidget::item:selected { background:#24516b; color:#ffffff; }"
		"QPushButton { background:#203b53; color:#e6eef7; border:1px solid #31536d;"
		" border-radius:6px; padding:9px 12px; }"
		"QPushButton:hover { background:#2c536f; }"
		"QPushButton:disabled { color:#698096; }"
	);
	dialog->setProperty("bgBaseStyle", dialog->styleSheet());
	const auto outer = new QVBoxLayout(dialog);
	const auto heading = new QLabel(S("BUDANOVGRAM  /  ПЛАГИНЫ"), dialog);
	auto font = heading->font();
	font.setPointSize(15);
	font.setBold(true);
	heading->setFont(font);
	outer->addWidget(heading);
	const auto tabs = new QTabWidget(dialog);
	outer->addWidget(tabs);
	const auto page = new QWidget(tabs);
	const auto layout = new QVBoxLayout(page);
	installedSearch = new QLineEdit(page);
	installedSearch->setPlaceholderText(S("Поиск установленных плагинов…"));
	layout->addWidget(installedSearch);
	installed = new QListWidget(page);
	connect(installedSearch, &QLineEdit::textChanged, dialog, [=](const QString &query) {
		for (int i = 0; i < installed->count(); ++i) {
			const auto row = installed->item(i);
			row->setHidden(!row->text().contains(query, Qt::CaseInsensitive));
		}
	});
	layout->addWidget(installed);
	const auto details = new QPlainTextEdit(page);
	details->setReadOnly(true);
	details->setMaximumHeight(165);
	layout->addWidget(details);
	const auto buttons = new QHBoxLayout();
	layout->addLayout(buttons);
	const auto add = new QPushButton(S("Установить из файла…"), page);
	const auto toggle = new QPushButton(S("Включить / выключить"), page);
	const auto settings = new QPushButton(S("Настройки"), page);
	const auto remove = new QPushButton(S("Удалить"), page);
	for (const auto b : {add, toggle, settings, remove}) buttons->addWidget(b);
	tabs->addTab(page, S("Установленные"));
	const auto selected = [=]() -> Ptr {
		if (!installed || !installed->currentItem()) return {};
		const auto id = installed->currentItem()->data(Qt::UserRole).toString();
		return plugins.count(id) ? plugins[id] : Ptr();
	};
	connect(installed, &QListWidget::currentRowChanged, dialog, [=](int) {
		const auto p = selected();
		if (!p) {
			details->clear();
			return;
		}
		const auto m = p->package.value("manifest").toObject();
		toggle->setText(p->process ? S("Выключить") : S("Включить"));
		details->setPlainText(m.value("description").toString() + "\n\nID: " + p->id
			+ S("\nАвтор: ") + m.value("author").toString() + "\n" + PermissionText(m));
	});
	connect(add, &QPushButton::clicked, dialog, [=] {
		const auto file = QFileDialog::getOpenFileName(dialog, S("Выбери плагин"), {}, "BudanovGram (*.bgplugin)");
		if (file.isEmpty()) return;
		QFile source(file);
		if (!source.open(QIODevice::ReadOnly) || source.size() > MaxPackage) {
			QMessageBox::warning(dialog, S("Ошибка"), S("Не удалось прочитать пакет до 1 МБ."));
			return;
		}
		install(source.readAll(), dialog);
	});
	connect(toggle, &QPushButton::clicked, dialog, [=] {
		const auto p = selected();
		if (!p) return;
		if (p->process) {
			p->state.insert("enabled", false);
			stop(p);
		} else {
			QMessageBox prompt(QMessageBox::Question, S("Разрешения плагина"),
				PermissionText(p->package.value("manifest").toObject())
				+ S("\n\nРазрешить эти возможности и запустить плагин?"),
				QMessageBox::Yes | QMessageBox::Cancel, dialog);
			prompt.setTextFormat(Qt::PlainText);
			if (prompt.exec() != QMessageBox::Yes) return;
			p->state.insert("enabled", true);
			p->state.insert("approvedHash", QString::fromLatin1(QCryptographicHash::hash(
				QJsonDocument(p->package).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
			startPlugin(p);
		}
		refreshList();
	});
	connect(settings, &QPushButton::clicked, dialog, [=] {
		if (const auto p = selected()) settingsDialog(p, dialog);
	});
	connect(remove, &QPushButton::clicked, dialog, [=] {
		const auto p = selected();
		if (!p || QMessageBox::question(dialog, S("Удалить плагин?"),
			S("Пакет будет удалён. Настройки и данные сохранятся для повторной установки.")) != QMessageBox::Yes) return;
		p->state.insert("enabled", false);
		stop(p);
		if (!QFile::remove(path(p, "plugin.bgplugin"))) {
			QMessageBox::warning(dialog, S("Ошибка"), S("Не удалось удалить пакет."));
			return;
		}
		plugins.erase(p->id);
		refreshList();
	});

	const auto catalogPage = new QWidget(tabs);
	const auto catalogLayout = new QVBoxLayout(catalogPage);
	const auto url = new QLineEdit(preferences.value("catalogUrl").toString(
		"https://raw.githubusercontent.com/Mengy777/BudanovGram/main/budanovgram/plugins/catalog.json"), catalogPage);
	catalogLayout->addWidget(new QLabel(S("Каталог плагинов (HTTPS JSON):"), catalogPage));
	catalogLayout->addWidget(url);
	const auto load = new QPushButton(S("Загрузить каталог"), catalogPage);
	catalogLayout->addWidget(load);
	const auto catalog = new QListWidget(catalogPage);
	const auto catalogSearch = new QLineEdit(catalogPage);
	catalogSearch->setPlaceholderText(S("Поиск по каталогу…"));
	catalogLayout->addWidget(catalogSearch);
	connect(catalogSearch, &QLineEdit::textChanged, dialog, [=](const QString &query) {
		for (int i = 0; i < catalog->count(); ++i) {
			const auto row = catalog->item(i);
			row->setHidden(!row->text().contains(query, Qt::CaseInsensitive));
		}
	});
	catalogLayout->addWidget(catalog);
	const auto download = new QPushButton(S("Скачать / обновить выбранный плагин"), catalogPage);
	catalogLayout->addWidget(download);
	const auto catalogStatus = new QLabel(S("Каталог загружается только по нажатию кнопки."), catalogPage);
	catalogStatus->setTextFormat(Qt::PlainText);
	catalogStatus->setWordWrap(true);
	catalogLayout->addWidget(catalogStatus);
	tabs->addTab(catalogPage, S("Каталог"));
	connect(load, &QPushButton::clicked, dialog, [=] {
		preferences.insert("catalogUrl", url->text());
		Save(root + "/manager.json", preferences);
		load->setEnabled(false);
		catalogStatus->setText(S("Загрузка…"));
		fetch(dialog, {{"url", url->text()}}, [=](QJsonValue result, QString error) {
			load->setEnabled(true);
			if (!error.isEmpty() || result.toObject().value("status").toInt() != 200) {
				catalogStatus->setText(error.isEmpty() ? S("Каталог недоступен") : error);
				return;
			}
			const auto data = QJsonDocument::fromJson(result.toObject().value("body").toString().toUtf8()).object();
			if (data.value("schemaVersion").toInt() != 1 || !data.value("plugins").isArray()) {
				catalogStatus->setText(S("Некорректный формат каталога"));
				return;
			}
			catalog->clear();
			for (const auto &value : data.value("plugins").toArray()) {
				if (catalog->count() >= 500) break;
				const auto item = value.toObject();
				const auto row = new QListWidgetItem(item.value("name").toString() + " · " + item.value("version").toString()
					+ "\n" + item.value("description").toString(), catalog);
				row->setData(Qt::UserRole, item);
				row->setHidden(!row->text().contains(catalogSearch->text(), Qt::CaseInsensitive));
			}
			catalogStatus->setText(S("Выбери плагин. Перед установкой будут показаны его разрешения."));
		});
	});
	connect(download, &QPushButton::clicked, dialog, [=] {
		if (!catalog->currentItem()) return;
		const auto item = catalog->currentItem()->data(Qt::UserRole).toJsonObject();
		if (!QRegularExpression("^[a-f0-9]{64}$").match(item.value("sha256").toString()).hasMatch()) {
			catalogStatus->setText(S("В каталоге отсутствует корректная SHA-256 сумма"));
			return;
		}
		download->setEnabled(false);
		catalogStatus->setText(S("Скачивание…"));
		fetch(dialog, {{"url", item.value("url")}}, [=](QJsonValue result, QString error) {
			download->setEnabled(true);
			const auto bytes = result.toObject().value("body").toString().toUtf8();
			if (!error.isEmpty() || result.toObject().value("status").toInt() != 200
				|| QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != item.value("sha256").toString().toLatin1()) {
				catalogStatus->setText(error.isEmpty() ? S("Ошибка загрузки или несовпадение SHA-256") : error);
				return;
			}
			catalogStatus->setText(install(bytes, dialog, item) ? S("Установлен. Включи плагин во вкладке «Установленные».") : S("Установка отменена"));
		});
	});

	const auto developer = new QWidget(tabs);
	const auto devLayout = new QVBoxLayout(developer);
	const auto help = new QLabel(S("JavaScript · API v1 · отдельный процесс QuickJS-NG для каждого плагина\n"
		"Формат .bgplugin: JSON с manifest, script и необязательными resources.\n"
		"Справочник API, примеры и исходники доступны в репозитории.\n"
		"Аварийный запуск без плагинов: BudanovGram.exe -no-plugins\n"
		"Секретные настройки Windows шифрует для текущего пользователя (DPAPI).\n"
		"Обычные настройки и storage сохраняются без шифрования."), developer);
	help->setTextFormat(Qt::PlainText);
	help->setWordWrap(true);
	devLayout->addWidget(help);
	const auto docs = new QPushButton(S("Документация API и примеры"), developer);
	devLayout->addWidget(docs);
	connect(docs, &QPushButton::clicked, dialog, [] {
		QDesktopServices::openUrl(QUrl("https://github.com/Mengy777/BudanovGram/tree/main/budanovgram/plugins"));
	});
	const auto folder = new QPushButton(S("Открыть папку плагинов"), developer);
	devLayout->addWidget(folder);
	connect(folder, &QPushButton::clicked, dialog, [=] { QDesktopServices::openUrl(QUrl::fromLocalFile(root)); });
	logView = new QPlainTextEdit(developer);
	logView->setReadOnly(true);
	devLayout->addWidget(logView);
	tabs->addTab(developer, S("Разработка и журнал"));
	const auto tick = new QTimer(dialog);
	connect(tick, &QTimer::timeout, dialog, [=] {
		if (const auto p = selected()) {
			const auto text = p->id + " · " + p->status + "\n" + p->logs.join('\n');
			if (logView->toPlainText() != text) logView->setPlainText(text);
		}
		for (int i = 0; i < installed->count(); ++i) {
			const auto row = installed->item(i);
			const auto id = row->data(Qt::UserRole).toString();
			if (!plugins.count(id)) continue;
			const auto p = plugins[id];
			const auto m = p->package.value("manifest").toObject();
			row->setText(m.value("name").toString() + " · " + m.value("version").toString() + "\n" + p->status);
		}
	});
	tick->start(1000);
	refreshList();
	styles();
	dialog->show();
}

void Manager::Private::settingsDialog(const Ptr &p, QWidget *parent) {
	if (locked) return;
	const auto dialog = new QDialog(parent);
	dialog->setProperty("bgPluginUi", true);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(S("Настройки · ") + p->package.value("manifest").toObject().value("name").toString());
	dialog->resize(520, 440);
	const auto layout = new QVBoxLayout(dialog);
	const auto scroll = new QScrollArea(dialog);
	scroll->setWidgetResizable(true);
	const auto body = new QWidget(scroll);
	const auto form = new QFormLayout(body);
	scroll->setWidget(body);
	layout->addWidget(scroll);
	const auto getters = std::make_shared<QMap<QString, std::function<QJsonValue()>>>();
	auto section = QString();
	for (const auto &entry : p->package.value("manifest").toObject().value("settings").toArray()) {
		const auto field = entry.toObject();
		const auto key = field.value("key").toString();
		const auto type = field.value("type").toString();
		const auto value = p->settings.value(key);
		if (field.value("section").toString() != section) {
			section = field.value("section").toString();
			if (!section.isEmpty()) {
				const auto heading = new QLabel(section, body);
				heading->setTextFormat(Qt::PlainText);
				auto font = heading->font();
				font.setBold(true);
				heading->setFont(font);
				form->addRow(heading);
			}
		}
		QWidget *widget = nullptr;
		if (type == "boolean") {
			const auto input = new QCheckBox(body);
			input->setChecked(value.toBool());
			(*getters)[key] = [=] { return input->isChecked(); };
			widget = input;
		} else if (type == "number") {
			const auto input = new QSpinBox(body);
			input->setRange(field.value("min").toInt(-1000000), field.value("max").toInt(1000000));
			input->setValue(value.toInt());
			(*getters)[key] = [=] { return input->value(); };
			widget = input;
		} else if (type == "select") {
			const auto input = new QComboBox(body);
			for (const auto &option : field.value("options").toArray()) {
				input->addItem(option.toString());
			}
			input->setCurrentText(value.toString());
			(*getters)[key] = [=] { return input->currentText(); };
			widget = input;
		} else {
			const auto input = new QLineEdit(value.toString(), body);
			input->setMaxLength(16000);
			if (type == "secret") input->setEchoMode(QLineEdit::Password);
			(*getters)[key] = [=] { return input->text(); };
			widget = input;
		}
		const auto label = new QLabel(field.value("label").toString(key), body);
		label->setTextFormat(Qt::PlainText);
		form->addRow(label, widget);
		if (!field.value("description").toString().isEmpty()) {
			const auto hint = new QLabel(field.value("description").toString(), body);
			hint->setTextFormat(Qt::PlainText);
			hint->setWordWrap(true);
			form->addRow(hint);
		}
	}
	const auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
		const auto before = p->settings;
		for (auto i = getters->begin(); i != getters->end(); ++i) p->settings.insert(i.key(), i.value()());
		if (!persist(p)) {
			p->settings = before;
			QMessageBox::warning(dialog, S("Ошибка"), S("Не удалось сохранить настройки или зашифровать секрет."));
			return;
		}
		event(p, "settings.changed", p->settings);
		dialog->close();
	});
	dialog->show();
}


Manager::Manager() : _private(std::make_unique<Private>()) {
}

Manager::~Manager() = default;

Manager &Manager::Instance() {
    static auto result = Manager();
    return result;
}

void Manager::start(QString dataDirectory, ClientHandler handler, bool safeMode) {
    const auto d = _private.get();
    if (d->started) return;
    d->started = true;
    d->client = std::move(handler);
    d->root = dataDirectory + "/budanovgram-plugins";
    d->originalStyle = qApp->styleSheet();
    if (!QDir().mkpath(d->root) || QFileInfo(d->root).isSymLink()) return;
    d->preferences = Read(d->root + "/manager.json");
    QFile embedded(":/budanovgram/plugin-host");
    if (embedded.open(QIODevice::ReadOnly)) {
        const auto bytes = embedded.readAll();
        const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        const auto runtimeDirectory = d->root + "/.runtime";
        if (!QFileInfo(runtimeDirectory).isSymLink() && QDir().mkpath(runtimeDirectory)) {
            const auto file = runtimeDirectory + "/host-" + QString::fromLatin1(digest) + ".exe";
            QFile existing(file);
            const auto matches = existing.open(QIODevice::ReadOnly)
                && QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256).toHex() == digest;
            existing.close();
            auto ready = matches;
            if (!ready) {
                QSaveFile target(file);
                ready = target.open(QIODevice::WriteOnly) && target.write(bytes) == bytes.size() && target.commit();
            }
            if (ready) {
                QFile::setPermissions(file, QFile::permissions(file) | QFile::ExeOwner);
                d->hostExecutable = file;
            }
        }
    } else {
        d->hostExecutable = QCoreApplication::applicationDirPath() + "/BudanovPluginHost.exe";
    }
    const auto safe = safeMode || QCoreApplication::arguments().contains("-no-plugins");
    for (const auto &id : QDir(d->root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto directory = d->root + '/' + id;
        if (QFileInfo(directory).isSymLink()) continue;
        const auto package = Read(directory + "/plugin.bgplugin", MaxPackage);
        if (!Validate(package).isEmpty() || package.value("manifest").toObject().value("id") != id) continue;
        const auto p = std::make_shared<Private::Plugin>();
        p->id = id;
        p->package = package;
        p->state = Read(directory + "/state.json");
        p->settings = p->state.value("settings").toObject();
        const auto secrets = p->state.value("secretSettings").toObject();
        for (auto i = secrets.begin(); i != secrets.end(); ++i) p->settings.insert(i.key(), Protect(i.value().toString(), true));
        for (const auto &entry : package.value("manifest").toObject().value("settings").toArray()) {
            const auto field = entry.toObject();
            const auto key = field.value("key").toString();
            if (!p->settings.contains(key)) p->settings.insert(key, field.value("default"));
        }
        d->plugins[id] = p;
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(package).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        if (safe || p->state.value("running").toBool() || p->state.value("approvedHash").toString() != hash) {
            p->state.insert("enabled", false);
            d->stop(p, safe ? S("Безопасный запуск: плагины отключены") : S("Нужно включить после изменения пакета или аварийного завершения"));
        } else if (p->state.value("enabled").toBool()) {
            d->startPlugin(p);
        } else {
            p->status = S("Выключен");
        }
    }
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, d, [=] {
        d->shuttingDown = true;
        for (const auto &[id, p] : d->plugins) d->stop(p);
        if (d->window) d->window->close();
    });
    d->rateTimer.setInterval(1000);
    QObject::connect(&d->rateTimer, &QTimer::timeout, d, [=] {
        for (const auto &[id, p] : d->plugins) p->requests = 0;
    });
    d->rateTimer.start();
    d->watchdog.setInterval(1000);
    QObject::connect(&d->watchdog, &QTimer::timeout, d, [=] {
        for (const auto &[id, p] : d->plugins) {
            if (p->process && p->pendingFrames) {
                if (++p->stuckTicks > 4) d->stop(p, S("Остановлен: движок не отвечает"));
            } else p->stuckTicks = 0;
        }
    });
    d->watchdog.start();
}

void Manager::show(QWidget *parent) {
    if (_private->locked) return;
    _private->showWindow(parent);
}

bool Manager::installPackage(const QByteArray &data, QWidget *parent) {
	const auto d = _private.get();
	if (d->locked) return false;
	if (!d->install(data, parent)) return false;
	d->showWindow(parent);
	const auto id = QJsonDocument::fromJson(data).object().value("manifest").toObject().value("id").toString();
	d->focusPlugin(id);
	return true;
}

QList<Command> Manager::commands(const QString &scope) const {
    auto result = QList<Command>();
    for (const auto &[id, p] : _private->plugins) {
        if (!p->process) continue;
        for (const auto &command : p->commands) {
            if (command.value("scope") == scope) {
                result.push_back({id, command.value("id").toString(),
                    p->package.value("manifest").toObject().value("name").toString()
                    + " · " + command.value("title").toString().left(120)});
            }
        }
    }
    return result;
}

void Manager::setLocked(bool locked) {
	const auto d = _private.get();
	const auto changed = d->locked != locked;
	d->locked = locked;
	if (changed) publish("client.locked", {{"locked", locked}});
	if (!locked) return;
	auto windows = QList<QPointer<QWidget>>();
	for (const auto widget : qApp->topLevelWidgets()) {
		for (auto owner = widget; owner; owner = owner->parentWidget()) {
			if (owner->property("bgPluginUi").toBool()) {
				windows.push_back(widget);
				break;
			}
		}
	}
	for (const auto &widget : windows) {
		if (!widget) continue;
		if (widget.data() == d->window.data()) widget->hide();
		else if (const auto dialog = qobject_cast<QDialog*>(widget.data())) dialog->reject();
		else widget->hide();
	}
}

void Manager::invoke(const Command &command, const QJsonObject &context) {
    const auto d = _private.get();
    if (!d->plugins.count(command.plugin)) return;
    const auto p = d->plugins[command.plugin];
    if (!p->process) return;
    d->write(p, {{"type", "command"}, {"id", command.id}, {"context", context}});
}

void Manager::publish(const QString &event, const QJsonObject &data) {
    const auto d = _private.get();
    if (d->shuttingDown) return;
    for (const auto &[id, p] : d->plugins) {
        if (!p->process) continue;
        if (event.startsWith("message.") && !d->allowed(p, "messages.events")) continue;
        d->event(p, event, data);
    }
}

void Manager::setCleanup(std::function<void(QString)> callback) {
    _private->cleanup = std::move(callback);
}

QString Manager::label(const QString &key, const QString &fallback) const {
    auto result = fallback;
    for (const auto &[id, p] : _private->plugins) {
        if (p->process && p->labels.value(key).isString()) result = p->labels.value(key).toString().left(120);
    }
    return result;
}

} // namespace BudanovGram::Plugins
