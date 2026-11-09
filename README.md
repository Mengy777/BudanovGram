<p align="center">
  <img src="budanovgram/media/banner.svg" alt="BudanovGram — Telegram на Windows. Свой характер." width="100%">
</p>

<p align="center">
  <img src="budanovgram/avatar.png" alt="Иконка BudanovGram" width="112">
</p>

<p align="center">
  <strong>Твой Telegram. Твои плагины. Твой стиль.</strong><br>
  Официальный репозиторий BudanovGram — клиента на основе Telegram Desktop<br>с JavaScript-плагинами, своим оформлением и обновлениями внутри приложения.
</p>

<p align="center">
  <a href="https://github.com/Mengy777/BudanovGram/releases/latest"><img alt="Последний релиз" src="https://img.shields.io/github/v/release/Mengy777/BudanovGram?style=flat-square&amp;color=28b6ee&amp;label=release"></a>
  <img alt="Windows x64" src="https://img.shields.io/badge/Windows-x64-377cf7?style=flat-square">
  <a href="budanovgram/plugins/README.md"><img alt="JavaScript Plugin API" src="https://img.shields.io/badge/Plugins-JavaScript_API-9c88ff?style=flat-square"></a>
  <img alt="Portable ZIP" src="https://img.shields.io/badge/Portable-ZIP-41cfbd?style=flat-square">
  <a href="LICENSE"><img alt="GPL v3" src="https://img.shields.io/badge/license-GPL_v3-a38bfa?style=flat-square"></a>
</p>

<p align="center">
  <a href="https://github.com/Mengy777/BudanovGram/releases/latest"><img src="budanovgram/media/download-windows.svg" alt="Скачать BudanovGram для Windows x64" width="380"></a>
</p>

<p align="center">
  <a href="https://github.com/Mengy777/BudanovGram/releases/latest"><strong>↓ Скачать последнюю версию</strong></a>
  &nbsp; · &nbsp; <a href="#-установка-за-три-шага">Как установить</a>
  &nbsp; · &nbsp; <a href="#plugins">Плагины</a>
  &nbsp; · &nbsp; <a href="budanovgram/plugins/README.md">Создать плагин</a>
  &nbsp; · &nbsp; <a href="https://github.com/Mengy777/BudanovGram/releases">Все версии</a>
</p>

---

## 🚀 Установка за три шага

<p align="center">
  <img src="budanovgram/media/quick-start.gif" alt="Три шага: скачать ZIP, распаковать архив, запустить EXE и войти в Telegram" width="100%">
</p>

1. **Скачай.** Нажми [«Скачать последнюю версию»](https://github.com/Mengy777/BudanovGram/releases/latest) и выбери архив `BudanovGram-…-Windows-x64-portable.zip` в разделе **Assets**. Кнопка всегда ведёт к последнему опубликованному релизу.
2. **Распакуй.** Нажми правой кнопкой по архиву → **«Извлечь всё…»**. Выбери удобную папку, например `D:\Programs`. Внутри появится папка `BudanovGram`.
3. **Запусти.** Открой распакованную папку, запусти **`BudanovGram.exe`** и войди в свой Telegram-аккаунт.

> **Для установки не нужны API-ключи, Git, Visual Studio или сборка исходников.** Нужен только ZIP из релиза. Запускай EXE из распакованной папки, а не из архива.

## ✨ Что уже есть

<table>
  <tr>
    <td width="50%"><img src="budanovgram/media/style.svg" width="40" alt=""><br><strong>Фирменный стиль</strong><br>Собственная иконка и название BudanovGram в окне и меню клиента.</td>
    <td width="50%"><img src="budanovgram/media/portable.svg" width="40" alt=""><br><strong>Запуск из папки</strong><br>Portable-версия: распаковал, запустил, вошёл в аккаунт.</td>
  </tr>
  <tr>
    <td><img src="budanovgram/media/updates.svg" width="40" alt=""><br><strong>Обновления внутри клиента</strong><br>Проверка GitHub Releases, загрузка, проверка пакета и перезапуск.</td>
    <td><img src="budanovgram/media/source.svg" width="40" alt=""><br><strong>Открытые исходники</strong><br>Основа — Telegram Desktop. Изменения BudanovGram доступны в этом репозитории.</td>
  </tr>
  <tr>
    <td><strong>🧩 Свои плагины</strong><br>Отдельное меню, каталог, поиск, настройки и журнал. Расширения можно включать независимо друг от друга.</td>
    <td><strong>⚡ Команды и API</strong><br>Инструменты для сообщений, вычисления, HTTPS API, панели и оформление через JavaScript.</td>
  </tr>
</table>

<a id="plugins"></a>

## 🧩 Плагины: добавляй свои возможности

Открой **☰ → Плагины**. В одном окне — установленные расширения, каталог, настройки и журнал ошибок. JavaScript-движок уже встроен в клиент: отдельно устанавливать Node.js или Python не нужно.

<p align="center">
  <img src="budanovgram/media/plugins-manager.png" alt="Меню плагинов BudanovGram: установленные расширения, каталог, описание, настройки и управление" width="820">
</p>

### Файл из чата → установленный плагин

1. Отправь или получи файл **`.bgplugin`** в любом чате, в том числе в «Избранном».
2. Нажми на файл — откроется карточка с **названием, описанием, версией, создателем и разрешениями**.
3. Нажми **«Установить»**, затем выбери плагин в меню и **включи** его. При необходимости открой **«Настройки»**.

Плагин после установки выключен. Просмотр карточки не запускает его код. Ник создателя указывается самим автором пакета.

### Начни с готовых примеров

Вкладка **«Каталог» → «Загрузить каталог»** → выбери расширение и установи.

| Плагин | Что делает | Исходный пакет |
| --- | --- | --- |
| **Текстовые инструменты** | Считает слова и символы, меняет отображение текста только у тебя, возвращает оригинал | [text-tools.bgplugin](budanovgram/plugins/examples/text-tools.bgplugin) |
| **Внешний API** | Получает сведения о релизах GitHub и показывает результат в панели | [api-demo.bgplugin](budanovgram/plugins/examples/api-demo.bgplugin) |
| **Визуальная лаборатория** | Демонстрирует настройки, оформление стандартных окон Qt и подпись пункта меню плагинов | [visual-lab.bgplugin](budanovgram/plugins/examples/visual-lab.bgplugin) |

### Что можно написать самому

Команды в меню сообщений и клиента, обработку текста, вычисления, интеграции с HTTPS API, панели с кнопками, таймеры, настройки и отдельное хранилище. Для сообщений доступны отдельные разрешения на чтение, локальное отображение, отправку и редактирование.

Каждый плагин работает в отдельном процессе. При включении клиент показывает разрешения и сетевые домены; секретные поля настроек сохраняются через Windows DPAPI. На время блокировки клиента доступ плагинов к сообщениям и буферу обмена приостанавливается.

**[Документация API и первый плагин →](budanovgram/plugins/README.md)** · [Пакеты примеров](budanovgram/plugins/examples) · [Каталог](budanovgram/plugins/catalog.json)

<details>
<summary><strong>Границы текущего API и восстановление после сбоя</strong></summary>

Это экспериментальная система JavaScript-плагинов. Она предоставляет документированные точки интеграции: поддержка любых внутренних функций Telegram, произвольных DLL и плагинов exteraGram не заявлена. QSS меняет стандартные Qt-виджеты; собственные элементы чата Telegram поддерживаются только через доступные методы API.

После аварии или изменения файла автозапуск плагина отключается. Для аварийного запуска клиента без плагинов используй:

```text
BudanovGram.exe -no-plugins
```

</details>

**Готовые сборки для Windows x64** — в [GitHub Releases](https://github.com/Mengy777/BudanovGram/releases/latest). Номер актуальной версии отображается на значке вверху страницы автоматически. Тип сборки и изменения указаны в описании релиза.

## 🔄 Как обновляться

**☰ → «О программе» → «Обновить» → «Скачать» → «Установить».**

Ссылка «О программе» находится в самом низу бокового меню, рядом с версией. Если обновление есть, клиент предложит загрузить его и перезапуститься после установки. Данные аккаунтов и установленных плагинов сохраняются; перед заменой файлов приложения создаётся резервная копия.

<details>
<summary><strong>Обновление вручную</strong></summary>

1. Закрой BudanovGram через **«Выход»** в меню значка возле часов.
2. Сохрани резервную копию своей папки `TelegramForcePortable`.
3. Распакуй новый релиз и замени файлы приложения в прежней папке.
4. **Сохрани свою папку `TelegramForcePortable`** — в ней находятся аккаунты. В архиве релиза эта папка пустая.
5. Запусти `BudanovGram.exe`.

Если в установленной сборке ещё нет работающей кнопки обновления, воспользуйся этим способом для перехода на актуальный релиз.

</details>

## 💬 Частые вопросы

**Нужен новый Telegram-аккаунт?** Нет, войди в свой существующий аккаунт.

**Можно пользоваться рядом с обычным Telegram?** Да, установи BudanovGram в отдельную папку. У portable-версии собственная папка данных.

**Где хранятся аккаунты?** В папке `TelegramForcePortable` рядом с приложением. Не отправляй её другим людям и не публикуй в GitHub.

**Как создать ярлык на рабочем столе?** Нажми правой кнопкой по `BudanovGram.exe` → **«Отправить» → «Рабочий стол (создать ярлык)»**. В Windows 11 сначала может понадобиться **«Показать дополнительные параметры»**.

**Плагины исчезнут после обновления клиента?** Они хранятся отдельно от EXE в папке данных. Обновление клиента сохраняет их; после сбоя расширение может потребовать повторного включения.

**Нужен Telegram Premium для плагинов?** Нет. Возможности плагина определяются API BudanovGram и его разрешениями; серверные ограничения Telegram сохраняются.

**Где установщик Setup.exe?** Пока распространяется portable-архив ZIP. Отдельного установщика ещё нет.

**Есть версия для Android?** Пока нет. Android-версия запланирована; APK в текущих релизах отсутствует.

**Где посмотреть возможности конкретной сборки?** В описании [релиза](https://github.com/Mengy777/BudanovGram/releases/latest). Документация в основной ветке также может описывать изменения, которые ещё готовятся к выпуску.

## 🛠 Для разработчиков

Чтобы просто установить клиент, используй [готовую сборку](https://github.com/Mengy777/BudanovGram/releases/latest). Этот раздел нужен для самостоятельной сборки и разработки.

```sh
git clone --recursive https://github.com/Mengy777/BudanovGram.git tdesktop
```

[Инструкция сборки Windows →](budanovgram/README-RU.md)

Собственные Telegram `api_id` и `api_hash` нужны только при сборке. Файл `build-secrets.json`, каталог `out` и данные аккаунтов должны оставаться локальными.

Нашёл ошибку? [Создай issue](https://github.com/Mengy777/BudanovGram/issues/new) и укажи версию клиента, Windows и шаги воспроизведения. Не прикладывай данные аккаунтов или API-ключи.

---

<p align="center">
  <strong>BudanovGram</strong><br>
  Создатель: @Putin_Ebanoe_Huilo<br>
  <a href="https://github.com/Mengy777/BudanovGram/releases">Скачать</a> · <a href="https://github.com/Mengy777/BudanovGram/issues">Сообщить об ошибке</a> · <a href="LICENSE">Лицензия</a>
</p>

Основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop). BudanovGram не является официальным клиентом Telegram. Сохранены [исходное описание проекта](README.upstream.md), [LICENSE](LICENSE) и [LEGAL](LEGAL); лицензия GPL v3 с исключением OpenSSL.
