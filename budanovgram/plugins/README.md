# Плагины BudanovGram

JavaScript API v1. Плагин — файл `.bgplugin`: обычный UTF-8 JSON с описанием,
кодом и необязательными ресурсами. Node.js и Python пользователю не нужны.

## Установка и управление

Открой **☰ → Плагины**. Поиск помогает быстро найти расширение среди установленных и в каталоге.

Файлом `.bgplugin` можно поделиться в любом чате, включая «Избранное». Нажми
на документ: клиент загрузит его и покажет карточку с названием, описанием,
версией и ником создателя. Нажми **«Установить»** — затем плагин появится в меню.
Код при получении и просмотре файла не исполняется. Ник задаётся полем `author`,
например `"author":"@my_nickname"`; это заявленные автором данные, а не проверка личности.

- **Установленные:** установить файл, включить, выключить, настроить, удалить.
- **Каталог:** загрузить каталог, выбрать плагин, скачать или обновить его.
- **Разработка и журнал:** справочник, папка данных, ошибки выбранного плагина.

После установки или обновления плагин выключен. При включении показываются его
разрешения и сетевые домены. Повторное включение не требуется при обычном запуске,
если пакет не менялся и клиент был закрыт корректно. После аварии или изменения
файла автозапуск отключается. Аварийный запуск клиента: `BudanovGram.exe -no-plugins`.

Каталог можно заменить своим HTTPS-адресом. Установка проверяет SHA-256,
идентификатор и версию пакета. Контрольная сумма проверяет соответствие каталогу,
но не устанавливает добросовестность автора. Автоматического обновления кода без
действия пользователя нет. Предыдущий пакет сохраняется как `previous.bgplugin`.

## Что доступно

| Область | API |
| --- | --- |
| Сообщения | Чтение загруженного сообщения, события загрузки/изменения текста, локальное отображение, отправка, серверное редактирование доступных сообщений |
| Команды | Пункты контекстного меню сообщения и основного меню |
| Панели | Собственные окна с текстом и кнопками, события кнопок |
| Настройки | Boolean, число, строка, пароль/API-ключ, список; группы, подсказки и события изменений |
| Сеть | HTTPS GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS, заголовки, тело, статус, JSON |
| Данные | Отдельное JSON-хранилище плагина, ресурсы пакета |
| Внешний вид | QSS для стандартных Qt-виджетов и переименование поддержанных пунктов меню |
| Дополнительно | Таймеры, буфер обмена, HTTPS-ссылки, журнал, информация о клиенте |

Это расширяемый API, а не доступ ко всем внутренностям Telegram. Самописные
элементы Telegram рисуются собственным движком: QSS не меняет любую часть чата.
Сейчас из текстовых точек интерфейса поддержан `menu.plugins`. Следующие точки
интеграции добавляются в нативный мост с сохранением API v1. Прямой загрузки DLL,
произвольных MTProto-вызовов, Python-плагинов или совместимости с плагинами
exteraGram нет.

## Первый плагин

```json
{
  "manifest": {
    "id": "my.text-tools",
    "name": "Мои инструменты",
    "version": "1.0.0",
    "apiVersion": 1,
    "author": "Автор",
    "description": "Подсчёт символов",
    "permissions": ["messages.read", "ui"],
    "settings": []
  },
  "script": "bg.commands.register({id:'count',title:'Посчитать символы'}, c => bg.ui.notify(String(Array.from(c.text).length)));"
}
```

Сохрани как `my-tools.bgplugin`, установи из файла и включи. Нажми правой кнопкой
по сообщению → «Мои инструменты · Посчитать символы». Рабочие примеры — в
[examples](examples): инструменты текста, внешний API и визуальная лаборатория.

Для удобной разработки держи JS отдельным файлом, а JSON собирай скриптом:

```python
import json
from pathlib import Path

package = {
    "manifest": json.loads(Path("manifest.json").read_text(encoding="utf-8")),
    "script": Path("main.js").read_text(encoding="utf-8"),
    "resources": {"greeting": "Привет!"}
}
Path("my-plugin.bgplugin").write_text(
    json.dumps(package, ensure_ascii=False, indent=2), encoding="utf-8")
```

## Справочник API

Все операции, которые обращаются к клиенту, возвращают `Promise`. Используй
`await` и обрабатывай ошибки. Отсутствующие разрешения дают отклонённый Promise.
ID аккаунта, чата и сообщения — **строки**: не преобразовывай их в JS Number.

```js
bg.apiVersion;                  // 1
bg.manifest;                    // копия manifest
await bg.client.info();         // name, pluginApiVersion, platform, runtime, capabilities
bg.log('Запущен');              // console.log/info/warn/error также доступны

const off = bg.events.on('message.loaded', async message => {
  // {account, peer, message, text, outgoing}
  // Загруженный текст, включая историю; это НЕ только новые входящие сообщения.
});
off();
bg.events.once('plugin.start', () => console.log('Старт'));

await bg.commands.register({id:'action',title:'Моё действие',scope:'message'}, async context => {
  const original = await bg.messages.get(context);
  await bg.messages.setLocalText(context, original.text.toUpperCase());
});
// scope:'main' добавляет команду в главное меню; context содержит account.
// Новые команды и подписи видны при следующем открытии меню.

await bg.messages.resetLocalText(ref);
await bg.messages.send({account,peer}, 'Текст'); // {queued:true}; не подтверждение доставки
await bg.messages.edit(ref, 'Новый текст');     // результат Telegram / ошибка сервера

await bg.storage.set('counter', 1);
const counter = await bg.storage.get('counter', 0);
await bg.storage.remove('counter');
const greeting = await bg.resources.read('greeting');

const prefix = bg.settings.get('prefix', '');
await bg.settings.set('prefix', '✨ ');
bg.settings.onChange(settings => console.log('Настройки изменились'));

const response = await bg.network.fetch('https://api.example.org/data', {
  method:'POST',
  headers:{'Content-Type':'application/json', Authorization:'Bearer '+bg.settings.get('apiKey')},
  body:JSON.stringify({text:'Привет'})
});
if (!response.ok) throw Error('HTTP '+response.status);
const data = await response.json(); // также response.text(), status, headers

await bg.clipboard.write('Текст');
const clipboard = await bg.clipboard.read();
await bg.ui.openUrl('https://example.org');
await bg.ui.notify('Готово'); // отдельное информационное окно
await bg.ui.panel({id:'my-panel',title:'Результаты',text:'Здесь текст',
  buttons:[{id:'refresh',title:'Обновить'}]});
bg.events.on('ui.action', ({panel,action}) => console.log(panel,action));
await bg.ui.setLabels({'menu.plugins':'Мои расширения'});
await bg.ui.setStyle('QTabBar::tab:selected { color:#22d3ee; }');

const timer = await bg.timers.setInterval(() => console.log('Тик'), 1000);
await bg.timers.clear(timer);
await bg.timers.setTimeout(() => console.log('Один раз'), 500);
```

Локальная замена меняет отображение основного текста сообщения в текущем запуске.
Оригинал, сервер, экспорт и другие пользователи не меняются. Замены снимаются при
выключении плагина или изменении исходного текста; не сохраняются в `tdata`.
Чтение относится к сообщениям, которые уже загружены клиентом. Серверное
редактирование проверяет права Telegram. При блокировке клиента/экрана доступ к
сообщениям прекращается. События защищённого содержимого не публикуются.

## Разрешения manifest

| Разрешение | Доступ |
| --- | --- |
| `messages.read` | Чтение и контекстные команды |
| `messages.events` | Текст загружаемых/изменяемых сообщений всех открытых аккаунтов |
| `messages.local` | Локальная замена отображения |
| `messages.send` | Отправка от лица аккаунта |
| `messages.edit` | Редактирование на сервере |
| `network` | Сеть, дополнительно нужен `networkHosts` |
| `clipboard.read`, `clipboard.write` | Чтение/запись буфера обмена |
| `ui` | Окна, кнопки, внешние HTTPS-ссылки |
| `ui.style` | QSS стандартных виджетов |
| `ui.labels` | Поддержанные подписи интерфейса |

Пример доменов: `"networkHosts":["api.example.org","api.github.com"]`.
Для плагина, которому доверяешь произвольные сетевые подключения, разрешено `"*"`.
Поддомены не включаются автоматически. Перенаправления не выполняются: плагин
может сам запросить следующий разрешённый адрес. TLS проверяется обычным способом.
Сетевые запросы не используют cookies Telegram или браузера.

`messages.events` вместе с `network` позволяет плагину передавать доступный ему
текст внешнему сервису; `messages.send` — действовать от твоего лица. Выдавай эти
разрешения с пониманием поведения кода. Runtime изолирует API, но отдельный процесс
**не является ОС-песочницей** против эксплуатации уязвимостей движка.

## Настройки

В `manifest.settings` перечисляются поля:

```json
[
  {"key":"enabled","type":"boolean","label":"Включить обработку","default":true},
  {"key":"limit","type":"number","label":"Количество","min":1,"max":100,"default":10},
  {"key":"prefix","type":"string","label":"Префикс","default":"✨ "},
  {"key":"apiKey","type":"secret","label":"API-ключ","default":"",
   "description":"Никому не передавай этот ключ"},
  {"key":"mode","type":"select","label":"Режим","options":["manual","auto"],"default":"manual"}
]
```

Секретные поля маскируются и шифруются Windows DPAPI для текущего пользователя.
После переноса на другой компьютер или Windows-аккаунт их нужно ввести заново.
Сам плагин получает свои секреты и может использовать их согласно разрешениям.
Не записывай ключи в обычное `storage`, manifest, журнал или репозиторий.

## Каталог

```json
{
  "schemaVersion": 1,
  "plugins": [{
    "id":"my.text-tools","name":"Мои инструменты","version":"1.0.0",
    "description":"Описание",
    "url":"https://example.org/my-tools.bgplugin",
    "sha256":"64 шестнадцатеричных символа в нижнем регистре"
  }]
}
```

Публикуй JSON и пакеты на HTTPS-сервере. Укажи SHA-256 точных UTF-8 байтов файла.
Пользователь может добавлять функции из любого такого каталога. Идентификаторы
версий пакетов независимы от версии BudanovGram.

## Устройство и совместимость

- **Нативный мост:** `Telegram/SourceFiles/core/budanovgram_plugins.*`.
- **Менеджер, IPC, разрешения, сеть, UI:** `manager.*`.
- **JavaScript SDK:** `sdk.js`, встроен в `BudanovPluginHost`.
- **Движок:** закреплённый QuickJS-NG, MIT; источник и commit в `vendor/quickjs/UPSTREAM.txt`.
- **Данные:** `budanovgram-plugins` внутри рабочего каталога клиента.
- **Runtime:** встроен в EXE и извлекается под именем с SHA-256 в `.runtime`.
- **Обновление клиента:** старый формат ZIP сохраняется; плагины и их данные не заменяются.

Протокол IPC передаёт JSON-кадры по stdin/stdout, отдельно для каждого плагина.
Нативные методы проверяют разрешения **в родительском процессе**. JavaScript не
получает Node.js, require, std/os, прямой доступ к `tdata`, файловой системе или
командной оболочке. Встроенные вычисления, JSON, RegExp, Promise и async/await
работают без отдельного разрешения. Сетевые операции и таймеры асинхронны.

Лимиты нужны для устойчивости, а не для платных уровней: 64 МБ heap, 512 КБ JS
stack, 500 мс непрерывного исполнения на кадр, 1 МБ пакет/HTTP-ответ, 2 МБ
хранилище, 8 запросов одновременно, 32 таймера, 128 ожидающих RPC, 300 RPC/сек.
Длительные вычисления разбивай на шаги через таймеры. Ошибки обработчиков попадают
в журнал; отказ/зависание процесса отключает плагин, включая его стили и замены.
Каталог и SDK версионируются отдельно. Новые возможности добавляются новыми
методами/событиями; несовместимая версия manifest.apiVersion отклоняется.

## Проверка разработчиками

Движок можно собрать отдельно, без Telegram/Qt (MSVC, Debug):

```sh
cmake -S budanovgram/plugins -B ../plugin-host-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ../plugin-host-build
python budanovgram/plugins/tests/test_host.py ../plugin-host-build/BudanovPluginHost.exe -v
```

В настроенном дереве Telegram: `cmake --build out --config Debug --target BudanovPluginTests`.
Тестовый EXE принимает два аргумента: путь к `BudanovPluginHost.exe` и каталог
для временных тестовых данных. В тестах используются отдельные данные и подмена
только действий Telegram; настоящие аккаунты не нужны.

Поля настроек можно группировать через `section`. Для проверки совместимости manifest
может содержать `requires`, например `["messages.setLocalText", "ui.panel"]`.
Неподдерживаемые возможности отклоняются при установке, доступные перечислены
в результате `bg.client.info().capabilities`.

Для пакетов в Git отключи преобразование переносов строк: `*.bgplugin -text`
в `.gitattributes`. Сумма должна соответствовать именно опубликованным байтам.
Для открытия менеджера сразу при запуске доступен `BudanovGram.exe -plugins`.

При обновлении сохраняются настройки с прежними ключами и типами. Если тип поля
изменён, его значение сбрасывается; удалённые поля убираются. Так секретная
настройка не превращается в обычный текст при изменении схемы плагина.
