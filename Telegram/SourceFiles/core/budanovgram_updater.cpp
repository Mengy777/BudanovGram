#include "core/budanovgram_updater.h"

#include "core/application.h"
#include "base/platform/base_platform_info.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QVersionNumber>

namespace Core {
namespace {

class UpdateSession final : public QObject {
public:
	UpdateSession(not_null<Ui::GenericBox*> box)
	: QObject(box)
	, _box(box)
	, _work(QCoreApplication::applicationDirPath()
		+ u"/.budanovgram-update-XXXXXX"_q) {
		_box->setTitle(u"Обновление BudanovGram"_q);
		_box->setWidth(st::aboutWidth);
		_label = _box->addRow(object_ptr<Ui::FlatLabel>(
			_box,
			u"Проверяю GitHub Releases…"_q,
			st::aboutLabel), st::boxRowPadding);
		closeButton();
		if (!_work.isValid()) {
			fail(u"Нет доступа для записи в папку приложения."_q);
			return;
		}
		QFile source(u":/gui/budanovgram/update-windows.ps1"_q);
		QFile target(_work.filePath(u"update.ps1"_q));
		if (!source.open(QIODevice::ReadOnly)
			|| !target.open(QIODevice::WriteOnly)) {
			fail(u"Не удалось подготовить обновление."_q);
			return;
		}
		const auto contents = source.readAll();
		// Windows PowerShell 5 requires a BOM for UTF-8 scripts.
		if (target.write(QByteArray::fromHex("efbbbf") + contents)
			!= contents.size() + 3) {
			fail(u"Не удалось записать помощник обновления."_q);
			return;
		}
		target.close();
		run(false);
	}

	~UpdateSession() override {
		for (const auto process : findChildren<QProcess*>()) {
			QObject::disconnect(process, nullptr, this, nullptr);
			if (process->state() != QProcess::NotRunning) {
				process->kill();
				process->waitForFinished(3000);
			}
		}
	}

private:
	QString powershell() const {
		return qEnvironmentVariable("SystemRoot")
			+ u"/System32/WindowsPowerShell/v1.0/powershell.exe"_q;
	}

	QStringList arguments(const QString &mode) const {
		return {
			u"-NoProfile"_q, u"-NonInteractive"_q,
			u"-ExecutionPolicy"_q, u"Bypass"_q,
			u"-File"_q, _work.filePath(u"update.ps1"_q),
			u"-Mode"_q, mode,
			u"-CurrentVersion"_q, QString::fromLatin1(BudanovGramVersion),
		};
	}

	void closeButton() {
		_box->addButton(rpl::single(u"Закрыть"_q), [=] {
			_box->closeBox();
		});
	}

	void fail(const QString &message) {
		_label->setText(message);
		_box->clearButtons();
		closeButton();
	}

	void run(bool download) {
		const auto process = new QProcess(this);
		const auto timer = new QTimer(process);
		timer->setSingleShot(true);
		QObject::connect(timer, &QTimer::timeout, process, [=] {
			process->kill();
		});
		QObject::connect(process, &QProcess::errorOccurred, this, [=](auto) {
			fail(u"Не удалось запустить проверку обновлений."_q);
		});
		QObject::connect(process,
			qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this,
			[=](int code, QProcess::ExitStatus status) {
				timer->stop();
				const auto output = process->readAllStandardOutput();
				process->deleteLater();
				if (code || status != QProcess::NormalExit) {
					fail(u"Не удалось получить или проверить обновление. "_q
						+ u"Проверь интернет и попробуй снова."_q);
					return;
				}
				if (download) {
					if (output.trimmed() != "READY") {
						fail(u"Не удалось проверить пакет обновления."_q);
						return;
					}
					ready();
					return;
				}
				const auto data = QJsonDocument::fromJson(output).object();
				_version = data.value(u"version"_q).toString();
				if (QVersionNumber::fromString(_version).isNull()) {
					fail(u"Некорректный ответ GitHub Releases."_q);
					return;
				}
				if (!data.value(u"available"_q).toBool()) {
					_label->setText(u"Установлена последняя версия: %1."_q
						.arg(QString::fromLatin1(BudanovGramVersion)));
					return;
				}
				_label->setText(u"Доступна версия %1 (%2 МБ)."_q
					.arg(_version)
					.arg(data.value(u"size"_q).toDouble() / 1048576., 0, 'f', 1));
				_box->clearButtons();
				_box->addButton(rpl::single(u"Скачать"_q), [=] {
					_box->clearButtons();
					closeButton();
					_label->setText(u"Загружаю обновление и проверяю SHA-256…"_q);
					run(true);
				});
				closeButton();
			});
		auto args = arguments(download ? u"Download"_q : u"Check"_q);
		if (download) {
			args << u"-Version"_q << _version;
		}
		timer->start(download ? 1000000 : 45000);
		process->start(powershell(), args);
	}

	void ready() {
		_label->setText(u"Версия %1 загружена и проверена. "_q.arg(_version)
			+ u"Для установки клиент закроется и запустится снова. "_q
			+ u"Аккаунты сохранятся."_q);
		_box->clearButtons();
		_box->addButton(rpl::single(u"Установить"_q), [=] {
			auto args = arguments(u"Install"_q);
			args << u"-Version"_q << _version
				<< u"-InstallDirectory"_q << QCoreApplication::applicationDirPath()
				<< u"-ClientProcessId"_q
				<< QString::number(QCoreApplication::applicationPid());
			QProcess installer;
			installer.setProgram(powershell());
			installer.setArguments(args);
			installer.setWorkingDirectory(QCoreApplication::applicationDirPath());
			if (!installer.startDetached()) {
				fail(u"Не удалось запустить установку обновления."_q);
				return;
			}
			_work.setAutoRemove(false);
			Quit();
		});
		closeButton();
	}

	const not_null<Ui::GenericBox*> _box;
	Ui::FlatLabel *_label = nullptr;
	QTemporaryDir _work;
	QString _version;

};

void UpdateBox(not_null<Ui::GenericBox*> box) {
	new UpdateSession(box);
}

} // namespace

void ShowBudanovGramUpdate(not_null<Ui::GenericBox*> parent) {
#ifdef Q_OS_WIN
	if (Platform::IsWindows64Bit()) {
		parent->getDelegate()->show(Box(UpdateBox));
	} else {
		parent->getDelegate()->show(Ui::MakeInformBox(
			u"Обновления доступны для Windows x64."_q));
	}
#else
	parent->getDelegate()->show(Ui::MakeInformBox(
		u"Обновления BudanovGram пока доступны для Windows x64."_q));
#endif
}

} // namespace Core
