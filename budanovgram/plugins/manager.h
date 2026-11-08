#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>

#include <functional>
#include <memory>

class QWidget;

namespace BudanovGram::Plugins {

struct Command {
	QString plugin;
	QString id;
	QString title;
};

class Manager final {
public:
	using Reply = std::function<void(QJsonValue, QString)>;
	using ClientHandler = std::function<void(QString, QJsonObject, Reply)>;
	static Manager &Instance();
	void start(QString dataDirectory, ClientHandler handler);
	void show(QWidget *parent = nullptr);
	bool installPackage(const QByteArray &data, QWidget *parent = nullptr);
	void setLocked(bool locked);
	QList<Command> commands(const QString &scope) const;
	void invoke(const Command &command, const QJsonObject &context);
	void publish(const QString &event, const QJsonObject &data);
	void setCleanup(std::function<void(QString)> callback);
	QString label(const QString &key, const QString &fallback) const;

private:
	friend int RunTests(QString, QString);
	Manager();
	~Manager();
	struct Private;
	std::unique_ptr<Private> _private;

};

} // namespace BudanovGram::Plugins
