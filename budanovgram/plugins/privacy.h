#pragma once
#include <QtCore/QString>
#include <cstdint>
#include <functional>
class QWidget;
namespace BudanovGram::Privacy {
enum class Activity { Messages, Online, Typing, Stories };
void Initialize(const QString &directory);
bool Allowed(uint64_t account, Activity activity);
void Show(uint64_t account, std::function<void()> changed, QWidget *parent = nullptr);
} // namespace BudanovGram::Privacy
