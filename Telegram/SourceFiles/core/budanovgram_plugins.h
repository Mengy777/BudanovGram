#pragma once

#include "base/basic_types.h"

class HistoryItem;
class DocumentData;
struct FullMsgId;
class QWidget;
struct TextWithEntities;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Core::Plugins {

void Start();
void Show(QWidget *parent = nullptr);
bool OpenDocument(not_null<DocumentData*> document, FullMsgId context);
void AddMessageCommands(not_null<Ui::PopupMenu*> menu, not_null<HistoryItem*> item);
void ObserveMessage(not_null<HistoryItem*> item, const QString &text, bool sent = false);
const TextWithEntities *LocalText(not_null<const HistoryItem*> item);

} // namespace Core::Plugins
