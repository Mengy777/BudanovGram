// Copyright @Radolyn, 2026 (AyuGram source).
// Ghost-mode integration adapted from AyuGram Desktop (GPL-3.0).
// Source: AyuGram/AyuGramDesktop, commit 0f2a31bb420de98f331990432265b131b8d836b5.
#include "privacy.h"
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QDir>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>
namespace BudanovGram::Privacy {
namespace {
QString File;
QJsonObject Accounts;
QString Key(Activity activity) {
    switch (activity) {
    case Activity::Messages: return "messages";
    case Activity::Online: return "online";
    case Activity::Typing: return "typing";
    case Activity::Stories: return "stories";
    }
    return {};
}
QString S(const char *text) { return QString::fromUtf8(text); }
} // namespace
void Initialize(const QString &directory) {
    if (!File.isEmpty()) return;
    QDir().mkpath(directory);
    File = directory + "/budanovgram-privacy.json";
    QFile input(File);
    if (input.open(QIODevice::ReadOnly) && input.size() < 65536) {
        Accounts = QJsonDocument::fromJson(input.readAll()).object();
    }
}
bool Allowed(uint64_t account, Activity activity) {
    const auto options = Accounts.value(QString::number(account)).toObject();
    return !options.value("enabled").toBool() || !options.value(Key(activity)).toBool(true);
}
void Show(uint64_t account, std::function<void()> changed, QWidget *parent) {
    auto dialog = QDialog(parent);
    dialog.setWindowTitle(S("BudanovGram · Режим призрака"));
    dialog.resize(540, 390);
    auto layout = QVBoxLayout(&dialog);
    const auto note = new QLabel(S("Настройки действуют для текущего аккаунта и сохраняются после перезапуска. "
        "Другие активные клиенты Telegram могут отправлять эти события независимо от BudanovGram."), &dialog);
    note->setWordWrap(true);
    layout.addWidget(note);
    const auto options = Accounts.value(QString::number(account)).toObject();
    auto enabled = QCheckBox(S("Включить режим призрака"), &dialog);
    enabled.setChecked(options.value("enabled").toBool());
    layout.addWidget(&enabled);
    auto messages = QCheckBox(S("Не отправлять отметки прочтения сообщений и медиа"), &dialog);
    auto online = QCheckBox(S("Не отправлять статус онлайн"), &dialog);
    auto typing = QCheckBox(S("Скрывать набор текста, запись и загрузку файлов"), &dialog);
    auto stories = QCheckBox(S("Не отправлять просмотры и прочтение историй"), &dialog);
    messages.setChecked(options.value("messages").toBool(true));
    online.setChecked(options.value("online").toBool(true));
    typing.setChecked(options.value("typing").toBool(true));
    stories.setChecked(options.value("stories").toBool(true));
    for (auto checkbox : {&messages, &online, &typing, &stories}) {
        layout.addWidget(checkbox);
        checkbox->setEnabled(enabled.isChecked());
        QObject::connect(&enabled, &QCheckBox::toggled, checkbox, &QWidget::setEnabled);
    }
    const auto detail = new QLabel(S("Уже отправленные отметки не отменяются. Отправка сообщений, звонки, "
        "реакции и другие действия могут раскрывать активность: этот режим не обеспечивает анонимность."), &dialog);
    detail->setWordWrap(true);
    layout.addWidget(detail);
    layout.addStretch();
    auto buttons = QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto updated = Accounts;
        updated.insert(QString::number(account), QJsonObject{{"enabled", enabled.isChecked()},
            {"messages", messages.isChecked()}, {"online", online.isChecked()},
            {"typing", typing.isChecked()}, {"stories", stories.isChecked()}});
        QSaveFile output(File);
        const auto bytes = QJsonDocument(updated).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            QMessageBox::warning(&dialog, S("BudanovGram"), S("Не удалось сохранить настройки."));
            return;
        }
        Accounts = updated;
        if (changed) changed();
        dialog.accept();
    });
    dialog.exec();
}
} // namespace BudanovGram::Privacy
