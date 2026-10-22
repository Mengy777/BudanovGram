#pragma once

#include "ui/layers/generic_box.h"

namespace Core {

inline constexpr auto BudanovGramVersion = "0.1.2";

void ShowBudanovGramUpdate(not_null<Ui::GenericBox*> parent);

} // namespace Core
