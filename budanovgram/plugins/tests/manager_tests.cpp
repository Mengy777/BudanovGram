#include "../manager.cpp"
#include "../privacy.cpp"

#include <QtCore/QEventLoop>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtPlugin>
#include <iostream>

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

namespace BudanovGram::Plugins {

int RunTests(QString executable, QString root) {
	auto failures = 0;
	const auto check = [&](bool condition, const char *name) {
		std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
		if (!condition) ++failures;
	};
	QTemporaryDir temporary(root + "/plugin-tests-XXXXXX");
	check(temporary.isValid(), "temporary test directory");
	if (!temporary.isValid()) return 1;
	{
		QFile options(temporary.path() + "/budanovgram-privacy.json");
		options.open(QIODevice::WriteOnly);
		options.write(R"JSON({"101":{"enabled":true,"messages":true,"online":false,"typing":true,"stories":true}})JSON");
		options.close();
		Privacy::Initialize(temporary.path());
		check(!Privacy::Allowed(101, Privacy::Activity::Messages), "ghost suppresses read receipts");
		check(Privacy::Allowed(101, Privacy::Activity::Online), "ghost respects independent online switch");
		check(!Privacy::Allowed(101, Privacy::Activity::Typing), "ghost suppresses activity");
		check(!Privacy::Allowed(101, Privacy::Activity::Stories), "ghost suppresses story views");
		check(Privacy::Allowed(102, Privacy::Activity::Messages), "ghost settings isolated by account");
		QTimer::singleShot(80, qApp, [&] {
			const auto dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
			if (!dialog) return;
			for (const auto box : dialog->findChildren<QCheckBox*>()) {
				if (box->text().contains(S("Включить режим"))) box->setChecked(false);
			}
			dialog->grab().save(root + "/budanovgram-ghost-preview.png");
			dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
		});
		Privacy::Show(101, [] {});
		check(Privacy::Allowed(101, Privacy::Activity::Messages), "disabling ghost restores normal requests");
		QFile saved(temporary.path() + "/budanovgram-privacy.json");
		saved.open(QIODevice::ReadOnly);
		check(!QJsonDocument::fromJson(saved.readAll()).object().value("101").toObject().value("enabled").toBool(true), "ghost options saved to disk");
	}

	auto manifest = QJsonObject{{"id", "test.integration"}, {"name", "Test"},
		{"version", "1.0.0"}, {"apiVersion", 1}, {"permissions", QJsonArray{"messages.send"}},
		{"settings", QJsonArray{QJsonObject{{"key", "token"}, {"type", "secret"}}}}};
	auto package = QJsonObject{{"manifest", manifest}, {"script", "console.log('ok')"}};
	check(Validate(package).isEmpty(), "valid manifest");
	auto bad = manifest;
	bad.insert("id", "../../escape");
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject path traversal ID");
	bad = manifest;
	bad.insert("id", "test.integration.");
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject Windows trailing-dot alias");
	bad.insert("id", "con.plugin");
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject Windows reserved device alias");
	bad = manifest;
	bad.insert("permissions", QJsonArray{"unknown.permission"});
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject unknown permission");
	bad = manifest;
	bad.insert("apiVersion", 500);
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject incompatible API");
	bad = manifest;
	bad.insert("networkHosts", QJsonArray{"https://example.org/path"});
	check(!Validate({{"manifest", bad}, {"script", "x"}}).isEmpty(), "reject malformed host");
	const auto secret = S("test secret Привет 🔐");
	const auto encrypted = Protect(secret, false);
	check(!encrypted.isEmpty() && encrypted != secret && Protect(encrypted, true) == secret, "DPAPI roundtrip");
	auto &manager = Manager::Instance();
	auto calls = 0;
	manager.start(temporary.path(), [&](QString method, QJsonObject params, Manager::Reply done) {
		++calls;
		check(method == "messages.send" && params.value("pluginId") == "test.integration", "host supplies plugin identity");
		done(QJsonObject{{"queued", true}}, {});
	});
	const auto d = manager._private.get();
	d->hostExecutable = executable;
	const auto p = std::make_shared<Manager::Private::Plugin>();
	p->id = "test.integration";
	p->package = package;
	p->settings = {{"token", secret}};
	p->state = {{"enabled", true}};
	QDir().mkpath(d->root + '/' + p->id);
	d->plugins[p->id] = p;
	check(d->persist(p), "persist settings");
	QFile state(d->path(p, "state.json"));
	state.open(QIODevice::ReadOnly);
	check(!state.readAll().contains(secret.toUtf8()), "secret absent from plaintext state");
	state.close();
	auto changedSchema = manifest;
	changedSchema.insert("settings", QJsonArray{QJsonObject{{"key", "token"}, {"type", "string"}, {"default", ""}}});
	p->package.insert("manifest", changedSchema);
	check(d->persist(p) && p->settings.value("token").toString().isEmpty(), "changing secret field type clears old secret");
	state.open(QIODevice::ReadOnly);
	check(!state.readAll().contains(secret.toUtf8()), "schema migration does not expose old secret");
	state.close();
	p->package.insert("manifest", manifest);
	p->package.insert("script", R"JS(
(async () => {
  const flags = {};
  try { await bg.messages.get({}); } catch(e) { flags.readDenied = true; }
  try { await bg.users.resolve({account:'1'}, 'testuser'); } catch(e) { flags.usersDenied = true; }
  try { await bg.network.fetch('https://example.org'); } catch(e) { flags.networkDenied = true; }
  try { await bg.clipboard.read(); } catch(e) { flags.clipboardDenied = true; }
  const sent = await bg.messages.send({account:'1',peer:'2'},'test');
  flags.sendAllowed = sent.queued;
  await bg.storage.set('flags',flags);
  const recovered = await bg.storage.get('flags');
  await bg.storage.set('roundtrip',recovered.sendAllowed);
})();
)JS");
	d->startPlugin(p);
	QEventLoop loop;
	QTimer poll;
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
		if (p->state.value("storage").toObject().contains("roundtrip")) loop.quit();
	});
	poll.start(20);
	QTimer::singleShot(5000, &loop, &QEventLoop::quit);
	loop.exec();
	const auto storage = p->state.value("storage").toObject();
	const auto flags = storage.value("flags").toObject();
	check(flags.value("readDenied").toBool(), "read permission enforced in parent");
	check(flags.value("usersDenied").toBool(), "user resolution permission enforced in parent");
	check(flags.value("networkDenied").toBool(), "network permission enforced in parent");
	check(flags.value("clipboardDenied").toBool(), "clipboard permission enforced in parent");
	check(flags.value("sendAllowed").toBool() && calls == 1, "granted API routed once");
	check(storage.value("roundtrip").toBool(), "native IPC storage roundtrip");
	d->stop(p);
	check(!p->process && p->commands.isEmpty() && !p->state.value("running").toBool(), "clean stop");
	p->package.insert("script", "while(true){}");
	d->startPlugin(p);
	QEventLoop crash;
	QTimer watch;
	QObject::connect(&watch, &QTimer::timeout, &crash, [&] { if (!p->process) crash.quit(); });
	watch.start(20);
	QTimer::singleShot(5000, &crash, &QEventLoop::quit);
	crash.exec();
	check(!p->process && !p->state.value("enabled").toBool(), "faulted plugin automatically disabled");
	d->stop(p);
	manifest.insert("permissions", QJsonArray{"network"});
	manifest.insert("networkHosts", QJsonArray{"api.github.com"});
	p->package.insert("manifest", manifest);
	p->package.insert("script", R"JS(
(async () => {
  let denied = false;
  try { await bg.network.fetch('https://unlisted.example.org'); } catch(e) { denied = true; }
  await bg.storage.set('domainDenied',denied);
  const response = await bg.network.fetch('https://api.github.com/repos/Mengy777/BudanovGram/releases/latest');
  await bg.storage.set('httpsStatus',response.status);
  await bg.storage.set('releaseTag',(await response.json()).tag_name);
})();
)JS");
	d->startPlugin(p);
	QEventLoop network;
	QTimer networkPoll;
	QObject::connect(&networkPoll, &QTimer::timeout, &network, [&] {
		if (p->state.value("storage").toObject().contains("releaseTag")) network.quit();
	});
	networkPoll.start(20);
	QTimer::singleShot(25000, &network, &QEventLoop::quit);
	network.exec();
	const auto networkData = p->state.value("storage").toObject();
	check(networkData.value("domainDenied").toBool(), "network host allowlist enforced");
	check(networkData.value("httpsStatus").toInt() == 200, "real HTTPS API response");
	check(networkData.value("releaseTag").toString().startsWith('v'), "real API JSON parsed in plugin");
	d->stop(p);
	d->plugins.clear();
	const auto approve = [&] {
		QTimer::singleShot(50, qApp, [&] {
			if (const auto prompt = qobject_cast<QMessageBox*>(qApp->activeModalWidget())) {
				prompt->done(QMessageBox::Yes);
			} else if (const auto card = qobject_cast<QDialog*>(qApp->activeModalWidget())) {
				check(card->findChild<QLabel*>("PluginCardName")
					&& card->findChild<QLabel*>("PluginCardAuthor")
					&& card->findChild<QPlainTextEdit*>("PluginCardDescription"), "install card exposes name, author, description");
				card->grab().save(root + "/budanovgram-plugin-install-preview.png");
				card->accept();
			}
		});
	};
	auto fixtureManifest = manifest;
	fixtureManifest.insert("id", "bg.text-tools");
	fixtureManifest.insert("name", S("Тест установки"));
	const auto exampleBytes = QJsonDocument(QJsonObject{{"manifest", fixtureManifest},
		{"script", "console.log('installation test')"}}).toJson();
	approve();
	check(d->install(exampleBytes, nullptr), "install from real package with consent");
	const auto installed = d->plugins["bg.text-tools"];
	check(installed && !installed->state.value("enabled").toBool()
		&& QFile::exists(d->path(installed, "plugin.bgplugin")), "installed package starts disabled");
	installed->state.insert("storage", QJsonObject{{"sentinel", "preserve me"}});
	installed->state.insert("enabled", true);
	d->persist(installed);
	approve();
	check(d->install(exampleBytes, nullptr), "package update accepted");
	check(!installed->state.value("enabled").toBool()
		&& installed->state.value("storage").toObject().value("sentinel") == "preserve me"
		&& QFile::exists(d->path(installed, "previous.bgplugin")), "update preserves storage, backs up package, requires reenable");
	d->plugins.clear();
	auto chatPackage = QJsonDocument::fromJson(exampleBytes).object();
	auto chatManifest = chatPackage.value("manifest").toObject();
	chatManifest.insert("id", "test.chat-card");
	chatManifest.insert("name", S("Плагин из чата"));
	chatManifest.insert("author", "@plugin_author");
	chatManifest.insert("description", S("Описание файла, присланного в чате"));
	chatPackage.insert("manifest", chatManifest);
	approve();
	check(manager.installPackage(QJsonDocument(chatPackage).toJson()), "chat/file public install entry point");
	check(d->installed->currentItem()->data(Qt::UserRole).toString() == "test.chat-card"
		&& !d->plugins["test.chat-card"]->state.value("enabled").toBool(), "chat install focuses disabled plugin in manager");
	d->plugins.clear();
	d->refreshList();
	manager.show();
	qApp->processEvents();
	check(d->window && d->window->findChild<QTabWidget*>()->count() == 2, "manager has installed/developer tabs without catalog");
	if (d->installed && d->installed->count() > 1) d->installed->setCurrentRow(1);
	qApp->processEvents();
	check(d->window->grab().save(root + "/budanovgram-plugins-preview.png"), "native UI preview captured");
	manager.setLocked(true);
	check(!d->window->isVisible(), "locking client hides plugin windows");
	manager.show();
	check(!d->window->isVisible(), "locked manager cannot reopen");
	check(!manager.installPackage(exampleBytes), "locked client cannot start installation");
	manager.setLocked(false);
	manager.show();
	check(d->window->isVisible(), "manager reopens after unlock");
	d->window->close();
	d->plugins.clear();
	return failures ? 1 : 0;
}

} // namespace BudanovGram::Plugins

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	if (argc != 3) return 2;
	return BudanovGram::Plugins::RunTests(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
}
