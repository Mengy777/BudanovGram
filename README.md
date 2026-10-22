# BudanovGram

Неофициальный клиент на основе Telegram Desktop. Создатель: @Putin_Ebanoe_Huilo.

<img src="budanovgram/avatar.png" width="256" alt="BudanovGram">

Текущая версия: **0.1.1**, основа: **Telegram Desktop 7.3.1**.
Проверена локальная Windows x64 Debug-сборка.

## Что изменено

- Название BudanovGram, иконка приложения и Windows-ярлык.
- Отдельные идентификаторы приложения в Windows.
- Официальный автоапдейтер отключён.
- Кнопка «Обновить» проверяет GitHub Releases, загружает пакет и предлагает установку с перезапуском.

Android-версия запланирована, исходников Android в этом репозитории пока нет.

## Сборка

Клонирование с подмодулями:

```sh
git clone --recursive https://github.com/Mengy777/BudanovGram.git tdesktop
```

[Инструкция для Windows](budanovgram/README-RU.md).
Для сборки нужны собственные Telegram api_id и api_hash в локальном файле.
Не публикуйте build-secrets.json, каталог out или данные аккаунтов tdata.

## Обновления

Сборки публикуются в [GitHub Releases](https://github.com/Mengy777/BudanovGram/releases).
В «О программе» нажмите «Обновить», затем «Скачать» и «Установить». Проверяется SHA-256 пакета. Папка аккаунтов сохраняется, предыдущие файлы приложения остаются в резервной копии. Для перехода с 0.1.0 на 0.1.1 требуется один раз заменить приложение вручную.

## Исходный проект

Основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop),
commit `6fed91ffab9861771a75f29df65031b3c80b6941`.
[Исходное описание проекта](README.upstream.md), [лицензия](LICENSE), [уведомления](LEGAL).
