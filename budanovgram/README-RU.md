# BudanovGram 0.1.1 — исходники первой версии

Основа: Telegram Desktop 7.3.1, точный upstream commit указан в release.json.
Создатель: @Putin_Ebanoe_Huilo.

## Что готово

- Название BudanovGram в главных окнах, меню, стартовом экране и окне «О программе».
- Выбранная аватарка в Windows ICO и растровых ресурсах приложения.
- Имя выходного файла BudanovGram.exe; отдельные AppId, GUID и Windows-ярлык.
- Официальный автоапдейтер отключён, чтобы не заменять форк обычным Telegram.
- Кнопка «Обновить» проверяет GitHub Releases и предлагает скачать и установить новую версию с перезапуском.
- Скрипты подготовки, Debug-сборки и первой установки в D:\Programs\BudanovGram.

## Текущий статус

Windows x64 Debug-сборка 0.1.1 успешно собрана и установлена в D:\Programs\BudanovGram.
Окно BudanovGram запущено и отвечает. Все 35 этапов подготовки зависимостей завершены.
GitHub Releases подключён; Android-версия пока не собрана.

## Получить и сохранить API-данные

1. Открой https://my.telegram.org, войди и выбери API development tools.
2. App title: BudanovGram; Short name: budanovgram; Platform: Desktop.
3. Description: Custom Telegram Desktop client for Windows. URL пока можно оставить пустым.
4. Нажми Create application, затем найди App api_id и App api_hash.
5. Создай папку D:\Programs\BudanovGram, если её нет.
6. В Блокноте создай файл D:\Programs\BudanovGram\build-secrets.json.
   При сохранении выбери «Все файлы», чтобы не получить .json.txt.
7. Используй следующий формат, заменив пример своими значениями:

```json
{
  "api_id": 12345,
  "api_hash": "вставь_свой_api_hash"
}
```

api_id — число без кавычек. api_hash — строка в кавычках, обычно 32 шестнадцатеричных символа.
Не отправляй значения в чат и не публикуй build-secrets.json в GitHub.
Кэш out/ также содержит API-данные и не должен публиковаться.

## Сборка Windows x64

Для тяжёлой подготовки библиотек используй диск с достаточным свободным местом;
рекомендуется отдельный каталог сборки
на диске с десятками гигабайт свободного места. Установка программы остаётся на D:.
Сохраняй структуру: BuildPath/tdesktop, BuildPath/Libraries, BuildPath/ThirdParty.
Данный репозиторий можно скопировать в BuildPath/tdesktop целиком, включая .git.

1. В Visual Studio Installer добавь C++ Build Tools, MSVC 14.44 и Windows SDK 10.0.26100.0.
   В Visual Studio Installer добавь язык English: он нужен для распознавания MSVC в FFmpeg.
   Нужны также Git, Python 3.10 и CMake. См. upstream docs/building-win.md.
2. Запусти budanovgram/prepare-windows.cmd. Он загружает submodules и готовит библиотеки
   по официальному сценарию с skip-release. Это длительная операция.
3. Сохрани API-данные в указанный выше файл.
4. Запусти budanovgram/build-windows.cmd. Скрипт читает локальный JSON,
   конфигурирует проект и собирает Debug-цель Telegram с именем BudanovGram.exe.
5. Результат ожидается в out/Debug/BudanovGram.exe.
6. Для первой установки запусти в PowerShell из корня репозитория:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\budanovgram\install-windows.ps1
```

Скрипт проверяет ProductName EXE, копирует приложение и находящиеся рядом DLL/плагины,
создаёт portable-каталог и ярлык на рабочем столе. Он не перезаписывает уже установленный EXE.
После сборки требуется проверить наличие всех runtime-библиотек, запуск, вход,
иконку в панели задач/трее и отсутствие обращения к официальному автоапдейтеру.

## Обновление через GitHub Releases

В «О программе» нажми «Обновить», затем «Скачать» и «Установить».
Клиент проверяет стабильный релиз в Mengy777/BudanovGram и SHA-256 архива.
Установка ждёт завершения клиента, сохраняет резервную копию файлов приложения
в .budanovgram-backup-* и запускает обновлённую версию. При ошибке замены или запуска
восстанавливаются прежние файлы. Папка TelegramForcePortable и API-файл не заменяются.
Обновляющий помощник использует штатный Windows PowerShell 5.1.

Для перехода с версии 0.1.0 нужно один раз вручную установить 0.1.1.
Архивы обновлений должны содержать BudanovGram.exe, release.json и при необходимости
LICENSE.txt, LEGAL.txt, README.txt внутри папки BudanovGram. Папка TelegramForcePortable
может присутствовать только пустой. Дополнительные файлы отклоняются.
Рядом с архивом публикуется SHA256SUMS.txt; формат имени архива:
BudanovGram-VERSION-Windows-x64-portable.zip. Поддерживается формат тегов vMAJOR.MINOR.PATCH.

## Лицензия и источники

Сохранены LICENSE, LEGAL и исходные уведомления Telegram Desktop.
Исходники основаны на https://github.com/telegramdesktop/tdesktop.
Инструкция Telegram API: https://core.telegram.org/api/obtaining_api_id.
