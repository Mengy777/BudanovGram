# BudanovGram

Неофициальный клиент на основе Telegram Desktop. Создатель: @Putin_Ebanoe_Huilo.

<img src="budanovgram/avatar.png" width="256" alt="BudanovGram">

Первая версия: **0.1.0**, основа: **Telegram Desktop 7.3.1**.
Проверена локальная Windows x64 Debug-сборка.

## Что изменено

- Название BudanovGram, иконка приложения и Windows-ярлык.
- Отдельные идентификаторы приложения в Windows.
- Официальный автоапдейтер отключён.
- Кнопка «Обновить» пока показывает сообщение; установка обновлений ещё не реализована.

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

Будущие сборки планируется размещать в [GitHub Releases](https://github.com/Mengy777/BudanovGram/releases).
Создание репозитория само по себе не включает обновление внутри клиента.

## Исходный проект

Основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop),
commit `6fed91ffab9861771a75f29df65031b3c80b6941`.
[Исходное описание проекта](README.upstream.md), [лицензия](LICENSE), [уведомления](LEGAL).
