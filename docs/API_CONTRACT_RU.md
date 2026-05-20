# ROS Web IDE — Контракт API между бэкендом и фронтендом

> **Версия**: 1.0.0
> **Последнее обновление**: 2026-05-20
> **Статус**: Черновик

Этот документ определяет полный контракт взаимодействия между **C++ бэкендом** и **TypeScript фронтендом** ROS Web IDE. Обе команды должны считать этот документ единственным источником истины. Любая неоднозначность здесь — это ошибка в контракте; обсудите её, прежде чем действовать наугад.

---

## Содержание

1. [Обзор протокола](#1-обзор-протокола)
2. [REST API](#2-rest-api)
3. [Протокол WebSocket](#3-протокол-websocket)
4. [Общие определения типов](#4-общие-определения-типов)
5. [Обработка ошибок](#5-обработка-ошибок)
6. [Расширяемость и версионирование](#6-расширяемость-и-версионирование)
7. [Приложение: История изменений](#7-приложение-история-изменений)

---

## 1. Обзор протокола

### 1.1 Сводка по транспортам

| Задача | Транспорт | Когда используется |
|---|---|---|
| CRUD файлового браузера, поиск, конфигурация рабочего пространства | REST (HTTP/1.1) | Запрос-ответ |
| Получение списка сущностей ROS2, доступ к параметрам | REST | Запрос-ответ |
| Системная информация, запуск сборки | REST | Запрос-ответ |
| Ввод/вывод терминала | WebSocket | Двунаправленный поток |
| Уведомления об изменении файлов | WebSocket | Серверные push-события |
| Эхо топиков ROS2, вызовы сервисов и действий | WebSocket | Двунаправленный поток |
| Вывод сборки и запуска | WebSocket | Серверные push-потоки |

### 1.2 Базовые URL

```
REST:       http://{host}:{port}/api/v1
WebSocket:  ws://{host}:{port}/ws
```

- Порт по умолчанию: `8080`
- Фронтенд ДОЛЖЕН поддерживать настраиваемые `host` и `port`.

### 1.3 Обёртка REST-ответа

Каждый REST-ответ использует следующую JSON-обёртку:

```jsonc
// Успех
{
  "ok": true,
  "data": { /* данные конкретного домена */ }
}

// Ошибка
{
  "ok": false,
  "error": {
    "code": "FS_PATH_NOT_FOUND",
    "message": "Человекочитаемое описание",
    "details": {}  // опционально, зависит от домена
  }
}
```

### 1.4 Заголовки REST-запросов

| Заголовок | Значение | Обязателен |
|---|---|---|
| `Content-Type` | `application/json` | Да (для тел запросов) |
| `Accept` | `application/json` | Да |

### 1.5 Соединение WebSocket

- Одно постоянное соединение на вкладку браузера.
- Сервер ДОЛЖЕН отвечать на `ping`-кадры кадром `pong`.
- При разрыве соединения фронтенд ДОЛЖЕН пытаться переподключиться с экспоненциальной задержкой (1с, 2с, 4с, 8с, макс. 30с).
- При переподключении фронтенд ДОЛЖЕН заново подписаться на все ранее активные каналы (сервер не хранит подписки между соединениями).

### 1.6 Обёртка WebSocket-сообщения

Каждый WebSocket-кадр (в обоих направлениях) использует следующую JSON-обёртку:

```jsonc
{
  "channel": "terminal",        // строка — подсистема, которой принадлежит сообщение
  "type": "output",             // строка — конкретный тип сообщения в рамках канала
  "payload": { /* ... */ },     // объект — данные, специфичные для типа
  "seq": 42                     // опциональное целое число — порядковый номер для корреляции запрос-ответ
}
```

**Правила**:
- `channel` и `type` — всегда строки в нижнем регистре через дефис (например, `"subscribe-topic"`).
- `seq` назначается клиентом для запросов. Сервер возвращает тот же `seq` в ответе. Для инициативных серверных push-сообщений `seq` опускается.
- Неизвестные значения `channel` или `type` ДОЛЖНЫ молча игнорироваться (прямая совместимость).

---

## 2. REST API

Все эндпоинты указаны относительно базового URL REST: `/api/v1`

---

### 2.1 Файловая система — `/fs`

#### `GET /fs/tree`

Получить содержимое директории в виде дерева.

**Параметры запроса (query)**:

| Параметр | Тип | По умолчанию | Описание |
|---|---|---|---|
| `path` | string | корень рабочего пространства | Абсолютный или относительный путь от корня рабочего пространства |
| `depth` | integer | 1 | Глубина рекурсии (1 = только непосредственные дочерние элементы, 0 = бесконечно) |

**Ответ `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src",
  "name": "src",
  "type": "directory",
  "children": [
    {
      "path": "/home/user/ros_ws/src/my_package",
      "name": "my_package",
      "type": "directory",
      "children": [
        { "path": "/home/user/ros_ws/src/my_package/package.xml", "name": "package.xml", "type": "file", "size": 1234 }
      ]
    }
  ]
}
```

**Ссылка на тип**: `FileEntry` (см. [раздел 4](#fileentry))

---

#### `GET /fs/file`

Прочитать содержимое одного файла.

**Параметры запроса (query)**:

| Параметр | Тип | Обязателен | Описание |
|---|---|---|---|
| `path` | string | Да | Путь к файлу |

**Ответ `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
  "content": "// содержимое файла\n",
  "encoding": "utf-8",
  "size": 234,
  "modified": "2026-05-20T10:30:00Z"
}
```

> **Примечание**: Бинарные файлы возвращаются с `"encoding": "base64"` и `content` в кодировке base64.

---

#### `PUT /fs/file`

Создать или перезаписать файл.

**Тело запроса**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/new_file.cpp",
  "content": "#include <iostream>\n",
  "createParents": true   // опционально, по умолчанию false — создать промежуточные директории
}
```

**Ответ `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/new_file.cpp",
  "size": 20,
  "created": true  // true, если файл был создан заново; false, если перезаписан
}
```

---

#### `DELETE /fs/file`

Удалить файл или директорию.

**Параметры запроса (query)**:

| Параметр | Тип | Обязателен | Описание |
|---|---|---|---|
| `path` | string | Да | Путь для удаления |
| `recursive` | boolean | Нет | Обязателен, если путь — непустая директория (по умолчанию false) |

**Ответ `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/old_file.cpp",
  "deleted": true
}
```

---

#### `POST /fs/rename`

Переименовать или переместить файл или директорию.

**Тело запроса**:

```jsonc
{
  "oldPath": "/home/user/ros_ws/src/my_pkg/a.cpp",
  "newPath": "/home/user/ros_ws/src/my_pkg/b.cpp"
}
```

**Ответ `data`**:

```jsonc
{
  "oldPath": "/home/user/ros_ws/src/my_pkg/a.cpp",
  "newPath": "/home/user/ros_ws/src/my_pkg/b.cpp"
}
```

---

#### `POST /fs/mkdir`

Создать директорию.

**Тело запроса**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/include/my_pkg",
  "createParents": true   // опционально, по умолчанию false
}
```

**Ответ `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/include/my_pkg",
  "created": true
}
```

---

#### `GET /fs/search`

Поиск файлов по шаблону имени или содержимому.

**Параметры запроса (query)**:

| Параметр | Тип | По умолчанию | Описание |
|---|---|---|---|
| `query` | string | (обязательный) | Поисковый запрос (glob-шаблон имени или текстовый шаблон) |
| `path` | string | корень рабочего пространства | Корневая директория для поиска |
| `type` | string | `"all"` | `"filename"`, `"content"` или `"all"` |
| `maxResults` | integer | 100 | Максимальное количество результатов |

**Ответ `data`**:

```jsonc
{
  "results": [
    {
      "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
      "type": "file",
      "matches": [
        { "line": 42, "column": 10, "text": "rclcpp::Publisher", "length": 16 }
      ]
    }
  ],
  "total": 1,
  "truncated": false
}
```

---

### 2.2 Рабочее пространство — `/workspace`

#### `GET /workspace`

Получить информацию о текущем рабочем пространстве.

**Ответ `data`**:

```jsonc
{
  "rootPath": "/home/user/ros_ws",
  "name": "ros_ws",
  "rosDistro": "humble",         // определено из окружения
  "packages": [                   // обнаруженные пакеты ROS2 в рабочем пространстве
    {
      "name": "my_pkg",
      "path": "/home/user/ros_ws/src/my_pkg",
      "type": "ament_cmake",      // "ament_cmake" | "ament_python"
      "executables": ["talker", "listener"]
    }
  ]
}
```

---

#### `POST /workspace/open`

Изменить корень рабочего пространства.

**Тело запроса**:

```jsonc
{
  "path": "/home/user/other_ws"
}
```

**Ответ**: такой же, как у `GET /workspace`.

---

### 2.3 ROS2 — `/ros`

#### `GET /ros/nodes`

Получить список всех активных узлов ROS2.

**Ответ `data`**:

```jsonc
{
  "nodes": [
    {
      "name": "/talker",
      "namespace": "/",
      "pid": 12345
    }
  ]
}
```

---

#### `GET /ros/topics`

Получить список всех активных топиков.

**Параметры запроса (query)**:

| Параметр | Тип | По умолчанию | Описание |
|---|---|---|---|
| `includeHidden` | boolean | false | Включить топики, начинающиеся с `_` или `/_` |

**Ответ `data`**:

```jsonc
{
  "topics": [
    {
      "name": "/chatter",
      "type": "std_msgs/msg/String",
      "publisherCount": 1,
      "subscriberCount": 2
    }
  ]
}
```

---

#### `GET /ros/services`

Получить список всех доступных сервисов.

**Ответ `data`**:

```jsonc
{
  "services": [
    {
      "name": "/set_parameters",
      "type": "rcl_interfaces/srv/SetParameters",
      "node": "/talker"
    }
  ]
}
```

---

#### `GET /ros/actions`

Получить список всех доступных действий (actions).

**Ответ `data`**:

```jsonc
{
  "actions": [
    {
      "name": "/navigate",
      "type": "nav2_msgs/action/NavigateToPose",
      "node": "/planner"
    }
  ]
}
```

---

#### `GET /ros/params`

Получить параметры конкретного узла.

**Параметры запроса (query)**:

| Параметр | Тип | Обязателен | Описание |
|---|---|---|---|
| `node` | string | Да | Полное имя узла |

**Ответ `data`**:

```jsonc
{
  "node": "/talker",
  "parameters": [
    {
      "name": "publish_rate",
      "type": "integer",     // "integer" | "double" | "string" | "boolean" | "array" | "object"
      "value": 10,
      "description": ""       // опционально, если доступно из дескриптора
    }
  ]
}
```

---

#### `PUT /ros/params`

Установить параметр узла.

**Тело запроса**:

```jsonc
{
  "node": "/talker",
  "name": "publish_rate",
  "value": 20
}
```

**Ответ `data`**:

```jsonc
{
  "node": "/talker",
  "name": "publish_rate",
  "value": 20,
  "success": true
}
```

---

#### `GET /ros/interfaces`

Получить список всех доступных типов интерфейсов ROS2.

**Параметры запроса (query)**:

| Параметр | Тип | По умолчанию | Описание |
|---|---|---|---|
| `kind` | string | `"all"` | `"msg"`, `"srv"`, `"action"` или `"all"` |
| `filter` | string | `""` | Подстрока для фильтрации по имени пакета или типа |

**Ответ `data`**:

```jsonc
{
  "interfaces": [
    { "kind": "msg",  "package": "std_msgs",  "name": "String" },
    { "kind": "srv",  "package": "example_interfaces", "name": "AddTwoInts" },
    { "kind": "action", "package": "nav2_msgs", "name": "NavigateToPose" }
  ]
}
```

---

#### `GET /ros/interface-detail`

Получить определения полей для конкретного типа интерфейса.

**Параметры запроса (query)**:

| Параметр | Тип | Обязателен | Описание |
|---|---|---|---|
| `type` | string | Да | Полное имя типа, например `std_msgs/msg/String` |

**Ответ `data`**:

```jsonc
{
  "type": "geometry_msgs/msg/Twist",
  "fields": [
    {
      "name": "linear",
      "type": "geometry_msgs/msg/Vector3",
      "isArray": false,
      "defaultValue": null,
      "children": [
        { "name": "x", "type": "float64", "isArray": false, "defaultValue": 0.0 },
        { "name": "y", "type": "float64", "isArray": false, "defaultValue": 0.0 },
        { "name": "z", "type": "float64", "isArray": false, "defaultValue": 0.0 }
      ]
    }
  ]
}
```

---

### 2.4 Сборка и запуск — `/build`

#### `POST /build`

Запустить сборку colcon.

**Тело запроса**:

```jsonc
{
  "targets": ["my_pkg"],        // опционально — пусто или пропущено = собрать всё
  "args": ["--cmake-args", "-DCMAKE_BUILD_TYPE=Release"],  // дополнительные аргументы CLI
  "clean": false                 // запустить с --clean-first
}
```

**Ответ `data`**:

```jsonc
{
  "buildId": "b_1716201000_abc",
  "status": "running"
}
```

Вывод сборки передаётся через WebSocket (см. [раздел 3.5](#35-канал-сборки---channel-build)).

---

#### `GET /build/status`

Получить статус сборки.

**Параметры запроса (query)**:

| Параметр | Тип | Обязателен | Описание |
|---|---|---|---|
| `buildId` | string | Нет | ID конкретной сборки. Если опущен — последняя сборка. |

**Ответ `data`**:

```jsonc
{
  "buildId": "b_1716201000_abc",
  "status": "completed",     // "running" | "completed" | "failed" | "cancelled"
  "targets": {
    "my_pkg": {
      "status": "completed",
      "returnCode": 0
    }
  }
}
```

---

#### `POST /launch`

Запустить ROS2 launch-файл.

**Тело запроса**:

```jsonc
{
  "file": "/home/user/ros_ws/src/my_pkg/launch/demo.launch.py",
  "arguments": {                // опционально — аргументы запуска как пары ключ-значение
    "use_sim": "true"
  }
}
```

**Ответ `data`**:

```jsonc
{
  "launchId": "l_1716201000_xyz",
  "status": "running",
  "pid": 12345
}
```

Вывод запуска передаётся через WebSocket (см. [раздел 3.5](#35-канал-сборки---channel-build)).

---

#### `POST /launch/stop`

Остановить запущенный launch-процесс.

**Тело запроса**:

```jsonc
{
  "launchId": "l_1716201000_xyz"
}
```

**Ответ `data`**:

```jsonc
{
  "launchId": "l_1716201000_xyz",
  "status": "stopped"
}
```

---

#### `GET /launch-files`

Обнаружить доступные launch-файлы в рабочем пространстве.

**Ответ `data`**:

```jsonc
{
  "files": [
    {
      "path": "/home/user/ros_ws/src/my_pkg/launch/demo.launch.py",
      "package": "my_pkg",
      "arguments": [
        { "name": "use_sim", "type": "string", "default": "false", "description": "Use simulation" }
      ]
    }
  ]
}
```

---

### 2.5 Система — `/system`

#### `GET /system/info`

Получить информацию об устройстве/системе.

**Ответ `data`**:

```jsonc
{
  "hostname": "jetson-nano",
  "platform": "aarch64",
  "os": "Ubuntu 22.04",
  "cpu": {
    "model": "ARM Cortex-A57",
    "cores": 4,
    "usagePercent": 23.5
  },
  "memory": {
    "totalBytes": 4294967296,
    "usedBytes": 2147483648,
    "availableBytes": 2147483648
  },
  "disk": {
    "totalBytes": 322122547200,
    "usedBytes": 107374182400,
    "availableBytes": 214748364800,
    "mountPoint": "/"
  }
}
```

---

#### `GET /system/ros-env`

Получить конфигурацию окружения ROS2.

**Ответ `data`**:

```jsonc
{
  "rosDistro": "humble",
  "rosVersion": "2",
  "domainId": 0,
  "variables": {
    "ROS_DOMAIN_ID": "0",
    "AMENT_PREFIX_PATH": "/opt/ros/humble:/home/user/ros_ws/install",
    "LD_LIBRARY_PATH": "/opt/ros/humble/lib:...",
    "PATH": "/opt/ros/humble/bin:..."
  }
}
```

---

## 3. Протокол WebSocket

Эндпоинт соединения: `ws://{host}:{port}/ws`

### 3.1 Канал терминала — `channel: "terminal"`

Канал терминала управляет сессиями псевдотерминалов (PTY). Каждый терминал получает уникальный `terminalId`.

#### Сообщения: Клиент → Сервер

##### `create`

Создать новую сессию терминала.

```jsonc
{
  "channel": "terminal",
  "type": "create",
  "seq": 1,
  "payload": {
    "terminalId": "term_1",        // ID, назначаемый клиентом
    "shell": "/bin/bash",          // опционально, по умолчанию: оболочка пользователя
    "cwd": "/home/user/ros_ws",    // опционально, по умолчанию: корень рабочего пространства
    "env": {},                     // опционально, дополнительные переменные окружения
    "cols": 80,                    // начальная ширина терминала
    "rows": 24                     // начальная высота терминала
  }
}
```

##### `input`

Отправить пользовательский ввод в терминал.

```jsonc
{
  "channel": "terminal",
  "type": "input",
  "payload": {
    "terminalId": "term_1",
    "data": "ls -la\r"
  }
}
```

##### `resize`

Уведомить бэкенд об изменении размера терминала.

```jsonc
{
  "channel": "terminal",
  "type": "resize",
  "payload": {
    "terminalId": "term_1",
    "cols": 120,
    "rows": 40
  }
}
```

##### `close`

Закрыть сессию терминала.

```jsonc
{
  "channel": "terminal",
  "type": "close",
  "payload": {
    "terminalId": "term_1"
  }
}
```

#### Сообщения: Сервер → Клиент

##### `created`

Подтверждает создание терминала (возвращает `seq` из запроса `create`).

```jsonc
{
  "channel": "terminal",
  "type": "created",
  "seq": 1,
  "payload": {
    "terminalId": "term_1",
    "pid": 12345
  }
}
```

##### `output`

Выходные данные терминала. Использует стандартные PTY escape-последовательности.

```jsonc
{
  "channel": "terminal",
  "type": "output",
  "payload": {
    "terminalId": "term_1",
    "data": "\u001b[?2004hroot@jetson:~$ "
  }
}
```

> **Примечание**: `data` содержит сырой вывод терминала, включая ANSI escape-последовательности. Фронтенд отвечает за их рендеринг (например, через xterm.js).

##### `exited`

Процесс терминала завершился.

```jsonc
{
  "channel": "terminal",
  "type": "exited",
  "payload": {
    "terminalId": "term_1",
    "exitCode": 0
  }
}
```

---

### 3.2 Канал наблюдения за файлами — `channel: "file-watch"`

Отслеживает изменения файловой системы в реальном времени.

#### Сообщения: Клиент → Сервер

##### `watch`

Подписаться на события файловой системы по указанному пути.

```jsonc
{
  "channel": "file-watch",
  "type": "watch",
  "seq": 10,
  "payload": {
    "watchId": "w_1",              // ID подписки, назначаемый клиентом
    "path": "/home/user/ros_ws/src/my_pkg",
    "recursive": true              // отслеживать подкаталоги
  }
}
```

##### `unwatch`

Прекратить наблюдение за путём.

```jsonc
{
  "channel": "file-watch",
  "type": "unwatch",
  "payload": {
    "watchId": "w_1"
  }
}
```

#### Сообщения: Сервер → Клиент

##### `watching`

Подтверждает подписку на наблюдение (возвращает `seq`).

```jsonc
{
  "channel": "file-watch",
  "type": "watching",
  "seq": 10,
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg"
  }
}
```

##### `changed`

Содержимое файла было изменено.

```jsonc
{
  "channel": "file-watch",
  "type": "changed",
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
    "kind": "modified"           // "modified" | "created" | "deleted" | "renamed"
  }
}
```

Для `"renamed"`:

```jsonc
{
  "channel": "file-watch",
  "type": "changed",
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg/new_name.cpp",
    "oldPath": "/home/user/ros_ws/src/my_pkg/old_name.cpp",
    "kind": "renamed"
  }
}
```

---

### 3.3 Канал ROS2 — `channel: "ros"`

Все взаимодействия с ROS2 в реальном времени проходят через этот канал.

#### Сообщения: Клиент → Сервер

##### `subscribe-topic`

Подписаться на сообщения в топике ROS2.

```jsonc
{
  "channel": "ros",
  "type": "subscribe-topic",
  "seq": 20,
  "payload": {
    "subscriptionId": "sub_1",     // назначается клиентом
    "topic": "/chatter",
    "type": "std_msgs/msg/String", // опционально — сервер может автоопределить
    "throttleRate": 0,             // опционально — макс. сообщений в секунду (0 = без ограничений)
    "queueLength": 1               // опционально — сколько сообщений буферизировать
  }
}
```

##### `unsubscribe-topic`

```jsonc
{
  "channel": "ros",
  "type": "unsubscribe-topic",
  "payload": {
    "subscriptionId": "sub_1"
  }
}
```

##### `publish-topic`

Опубликовать сообщение в топик (одиночное или постоянное).

```jsonc
{
  "channel": "ros",
  "type": "publish-topic",
  "payload": {
    "topic": "/cmd_vel",
    "type": "geometry_msgs/msg/Twist",
    "message": {
      "linear": { "x": 1.0, "y": 0.0, "z": 0.0 },
      "angular": { "x": 0.0, "y": 0.0, "z": 0.5 }
    }
  }
}
```

##### `call-service`

Вызвать сервис ROS2. Ответ приходит с совпадающим `seq`.

```jsonc
{
  "channel": "ros",
  "type": "call-service",
  "seq": 21,
  "payload": {
    "callId": "call_1",            // назначается клиентом
    "service": "/set_parameters",
    "type": "rcl_interfaces/srv/SetParameters",
    "request": {
      "parameters": [
        { "name": "publish_rate", "value": { "type": 2, "integer_value": 20 } }
      ]
    },
    "timeout": 5000                // мс, опционально, по умолчанию 5000
  }
}
```

##### `call-action`

Отправить цель (goal) серверу действий ROS2. Несколько ответов (feedback + result) разделяют один `callId`.

```jsonc
{
  "channel": "ros",
  "type": "call-action",
  "seq": 22,
  "payload": {
    "callId": "act_1",
    "action": "/navigate",
    "type": "nav2_msgs/action/NavigateToPose",
    "goal": {
      "pose": { /* ... */ }
    },
    "timeout": 30000               // мс, опционально
  }
}
```

##### `cancel-action`

Отменить выполняемую цель действия.

```jsonc
{
  "channel": "ros",
  "type": "cancel-action",
  "payload": {
    "callId": "act_1"
  }
}
```

##### `start-bag`

Начать запись топиков в bag-файл.

```jsonc
{
  "channel": "ros",
  "type": "start-bag",
  "seq": 23,
  "payload": {
    "bagId": "bag_1",
    "topics": ["/chatter", "/tf"], // пусто = записывать все
    "path": "/home/user/ros_ws/bags/recording",
    "format": "sqlite3"            // "sqlite3" | "mcap"
  }
}
```

##### `stop-bag`

Остановить запись.

```jsonc
{
  "channel": "ros",
  "type": "stop-bag",
  "payload": {
    "bagId": "bag_1"
  }
}
```

#### Сообщения: Сервер → Клиент

##### `subscribed`

Подтверждает подписку на топик (возвращает `seq`).

```jsonc
{
  "channel": "ros",
  "type": "subscribed",
  "seq": 20,
  "payload": {
    "subscriptionId": "sub_1",
    "topic": "/chatter"
  }
}
```

##### `topic-message`

Сообщение, полученное в подписанном топике.

```jsonc
{
  "channel": "ros",
  "type": "topic-message",
  "payload": {
    "subscriptionId": "sub_1",
    "topic": "/chatter",
    "timestamp": 1716201000000000000,  // наносекунды с начала эпохи
    "message": {
      "data": "Hello, world!"
    }
  }
}
```

##### `service-result`

Ответ на `call-service` (возвращает `seq`).

```jsonc
{
  "channel": "ros",
  "type": "service-result",
  "seq": 21,
  "payload": {
    "callId": "call_1",
    "success": true,
    "result": {
      "results": [{ "successful": true, "reason": "" }]
    }
  }
}
```

При ошибке:

```jsonc
{
  "channel": "ros",
  "type": "service-result",
  "seq": 21,
  "payload": {
    "callId": "call_1",
    "success": false,
    "error": {
      "code": "ROS_SERVICE_TIMEOUT",
      "message": "Service call timed out after 5000ms"
    }
  }
}
```

##### `action-feedback`

Обратная связь (feedback) от активной цели действия.

```jsonc
{
  "channel": "ros",
  "type": "action-feedback",
  "payload": {
    "callId": "act_1",
    "feedback": {
      "distance_remaining": 12.5
    }
  }
}
```

##### `action-result`

Финальный результат цели действия.

```jsonc
{
  "channel": "ros",
  "type": "action-result",
  "seq": 22,
  "payload": {
    "callId": "act_1",
    "status": "succeeded",       // "succeeded" | "aborted" | "cancelled"
    "result": { /* ... */ }
  }
}
```

##### `node-event`

Уведомление в реальном времени об изменениях жизненного цикла узлов ROS2.

```jsonc
{
  "channel": "ros",
  "type": "node-event",
  "payload": {
    "event": "started",          // "started" | "stopped"
    "node": {
      "name": "/new_node",
      "namespace": "/",
      "pid": 12345
    }
  }
}
```

##### `bag-status`

Обновление статуса сессии записи.

```jsonc
{
  "channel": "ros",
  "type": "bag-status",
  "payload": {
    "bagId": "bag_1",
    "status": "recording",       // "recording" | "stopped" | "error"
    "duration": 120,             // секунды
    "messageCount": 5000,
    "sizeBytes": 10485760
  }
}
```

---

### 3.4 Канал TF — `channel: "tf"`

Предоставляет данные дерева преобразований ROS2.

#### Сообщения: Клиент → Сервер

##### `subscribe-tf`

Подписаться на обновления преобразований.

```jsonc
{
  "channel": "tf",
  "type": "subscribe-tf",
  "seq": 30,
  "payload": {
    "subscriptionId": "tf_1",
    "frames": [],                  // пусто = все фреймы, или конкретные имена фреймов
    "throttleRate": 10             // макс. обновлений в секунду
  }
}
```

##### `get-tf-tree`

Запросить полное дерево TF (однократный).

```jsonc
{
  "channel": "tf",
  "type": "get-tf-tree",
  "seq": 31,
  "payload": {}
}
```

#### Сообщения: Сервер → Клиент

##### `tf-tree`

Ответ с полным деревом TF.

```jsonc
{
  "channel": "tf",
  "type": "tf-tree",
  "seq": 31,
  "payload": {
    "frames": [
      {
        "name": "base_link",
        "parent": null,
        "children": ["lidar", "camera"]
      },
      {
        "name": "lidar",
        "parent": "base_link",
        "children": []
      }
    ]
  }
}
```

##### `tf-update`

Инкрементальное обновление преобразования.

```jsonc
{
  "channel": "tf",
  "type": "tf-update",
  "payload": {
    "subscriptionId": "tf_1",
    "transforms": [
      {
        "parent": "base_link",
        "child": "lidar",
        "translation": { "x": 0.5, "y": 0.0, "z": 0.3 },
        "rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
        "timestamp": 1716201000000000000
      }
    ]
  }
}
```

---

### 3.5 Канал сборки — `channel: "build"`

Потоковый вывод сборки и запуска.

#### Сообщения: Сервер → Клиент

##### `build-output`

Строка или фрагмент вывода сборки.

```jsonc
{
  "channel": "build",
  "type": "build-output",
  "payload": {
    "buildId": "b_1716201000_abc",
    "target": "my_pkg",            // опционально
    "stream": "stdout",            // "stdout" | "stderr"
    "data": "[ 50%] Building CXX object CMakeFiles/talker.dir/src/talker.cpp.o\n"
  }
}
```

##### `build-status`

Изменение статуса сборки.

```jsonc
{
  "channel": "build",
  "type": "build-status",
  "payload": {
    "buildId": "b_1716201000_abc",
    "status": "completed",         // "running" | "completed" | "failed" | "cancelled"
    "targets": {
      "my_pkg": { "status": "completed", "returnCode": 0 },
      "other_pkg": { "status": "failed", "returnCode": 1 }
    }
  }
}
```

##### `launch-output`

Вывод от запущенного процесса launch.

```jsonc
{
  "channel": "build",
  "type": "launch-output",
  "payload": {
    "launchId": "l_1716201000_xyz",
    "node": "/talker",             // какой узел произвёл этот вывод (опционально)
    "stream": "stdout",
    "data": "[INFO] [talker]: Publishing: Hello, world!\n"
  }
}
```

##### `launch-status`

Изменение статуса процесса launch.

```jsonc
{
  "channel": "build",
  "type": "launch-status",
  "payload": {
    "launchId": "l_1716201000_xyz",
    "status": "stopped",           // "running" | "stopped"
    "exitCode": 0
  }
}
```

---

## 4. Общие определения типов

Эти типы являются общими для бэкенда и фронтенда. Приведены имена TypeScript-интерфейсов; команда C++ должна создать эквивалентные структуры. Имена полей и структура вложенности ДОЛЖНЫ совпадать точно для обеспечения совместимости JSON.

### `FileEntry`

```typescript
interface FileEntry {
  path: string;
  name: string;
  type: "file" | "directory" | "symlink";
  size?: number;               // байты, только для файлов
  modified?: string;           // дата/время в формате ISO 8601
  children?: FileEntry[];      // только для директорий
}
```

### `RosNode`

```typescript
interface RosNode {
  name: string;                // полное имя, например "/talker"
  namespace: string;           // например "/"
  pid: number;
}
```

### `RosTopic`

```typescript
interface RosTopic {
  name: string;
  type: string;                // например "std_msgs/msg/String"
  publisherCount: number;
  subscriberCount: number;
}
```

### `RosService`

```typescript
interface RosService {
  name: string;
  type: string;
  node: string;                // узел, предоставляющий сервис
}
```

### `RosAction`

```typescript
interface RosAction {
  name: string;
  type: string;
  node: string;
}
```

### `RosParameter`

```typescript
interface RosParameter {
  name: string;
  type: "integer" | "double" | "string" | "boolean" | "byte_array" | "array" | "object";
  value: unknown;
  description?: string;
}
```

### `RosInterface`

```typescript
interface RosInterface {
  kind: "msg" | "srv" | "action";
  package: string;
  name: string;
}
```

### `RosInterfaceField`

```typescript
interface RosInterfaceField {
  name: string;
  type: string;                // имя примитивного типа или полное имя типа интерфейса
  isArray: boolean;
  defaultValue?: unknown;
  children?: RosInterfaceField[];  // вложенные поля для сложных типов
}
```

### `RosPackage`

```typescript
interface RosPackage {
  name: string;
  path: string;
  type: "ament_cmake" | "ament_python";
  executables: string[];
}
```

### `LaunchFile`

```typescript
interface LaunchFile {
  path: string;
  package: string;
  arguments: LaunchArgument[];
}

interface LaunchArgument {
  name: string;
  type: string;
  default?: string;
  description?: string;
}
```

### `SystemInfo`

```typescript
interface SystemInfo {
  hostname: string;
  platform: string;
  os: string;
  cpu: CpuInfo;
  memory: MemoryInfo;
  disk: DiskInfo;
}

interface CpuInfo {
  model: string;
  cores: number;
  usagePercent: number;
}

interface MemoryInfo {
  totalBytes: number;
  usedBytes: number;
  availableBytes: number;
}

interface DiskInfo {
  totalBytes: number;
  usedBytes: number;
  availableBytes: number;
  mountPoint: string;
}
```

### `WsEnvelope`

```typescript
interface WsEnvelope<T = unknown> {
  channel: string;
  type: string;
  payload: T;
  seq?: number;
}
```

### `RestResponse`

```typescript
interface RestResponse<T = unknown> {
  ok: boolean;
  data?: T;
  error?: RestError;
}

interface RestError {
  code: string;
  message: string;
  details?: Record<string, unknown>;
}
```

---

## 5. Обработка ошибок

### 5.1 Коды ошибок REST

Поле `error.code` использует строковые константы с пространством имён. Бэкенд ДОЛЖЕН использовать именно эти коды; фронтенд выполняет по ним switch.

| Код | HTTP-статус | Описание |
|---|---|---|
| `FS_PATH_NOT_FOUND` | 404 | Файл или директория не существует |
| `FS_PATH_EXISTS` | 409 | Путь уже существует (при создании) |
| `FS_PERMISSION_DENIED` | 403 | Недостаточно прав файловой системы |
| `FS_IS_DIRECTORY` | 400 | Ожидался файл, получена директория |
| `FS_IS_FILE` | 400 | Ожидалась директория, получен файл |
| `FS_NOT_EMPTY` | 400 | Директория не пуста, recursive не указан |
| `FS_WRITE_FAILED` | 500 | Не удалось записать файл на диск |
| `WS_NOT_OPEN` | 400 | Путь рабочего пространства не задан или некорректен |
| `ROS_NODE_NOT_FOUND` | 404 | Запрашиваемый узел ROS не существует |
| `ROS_SERVICE_UNAVAILABLE` | 503 | Сервис ROS существует, но сервер недоступен |
| `ROS_SERVICE_TIMEOUT` | 504 | Тайм-аут при вызове сервиса/действия |
| `ROS_TOPIC_TYPE_MISMATCH` | 400 | Указанный тип не совпадает с фактическим типом топика |
| `ROS_INVALID_MESSAGE` | 400 | Некорректная полезная нагрузка сообщения |
| `ROS_PARAM_SET_FAILED` | 500 | Не удалось установить параметр на узле |
| `BUILD_IN_PROGRESS` | 409 | Сборка уже выполняется |
| `BUILD_NOT_FOUND` | 404 | ID сборки не существует |
| `LAUNCH_NOT_FOUND` | 404 | ID запуска не существует |
| `INTERNAL_ERROR` | 500 | Непредвиденная ошибка сервера |

### 5.2 Ошибки WebSocket

Ошибки WebSocket отправляются как сообщения на соответствующем канале:

```jsonc
{
  "channel": "terminal",
  "type": "error",
  "seq": 1,                      // возвращает seq неудачного запроса
  "payload": {
    "code": "TERMINAL_LIMIT_REACHED",
    "message": "Maximum number of terminals (10) reached"
  }
}
```

Дополнительные коды ошибок WebSocket:

| Код | Канал | Описание |
|---|---|---|
| `TERMINAL_LIMIT_REACHED` | terminal | Превышено максимальное число одновременных терминалов |
| `TERMINAL_NOT_FOUND` | terminal | Неизвестный `terminalId` |
| `SUBSCRIPTION_NOT_FOUND` | ros | Неизвестный `subscriptionId` |
| `BAG_WRITE_ERROR` | ros | Невозможно записать по указанному пути bag-файла |
| `BAG_NOT_RECORDING` | ros | `stop-bag` для `bagId`, который не записывается |
| `ACTION_NOT_FOUND` | ros | Неизвестное имя действия |
| `INVALID_PAYLOAD` | любой | Полезная нагрузка не соответствует ожидаемой схеме |

---

## 6. Расширяемость и версионирование

### 6.1 Добавление новых функций

Контракт рассчитан на аддитивные (добавочные) изменения:

1. **Новые REST-эндпоинты**: Добавляются новые пути под `/api/v1/`. Существующие эндпоинты не изменяются.
2. **Новые каналы WebSocket**: Добавляется новое значение `channel`. Клиенты, не знающие канал, игнорируют его.
3. **Новые типы сообщений**: Добавляется новый `type` в существующий канал. Неизвестные типы игнорируются.
4. **Новые поля полезной нагрузки**: Добавляются поля в существующие полезные нагрузки. Реализации ДОЛЖНЫ игнорировать неизвестные поля (как C++, так и TS).

### 6.2 Критические (breaking) изменения

Изменения являются **критическими**, если они:
- Удаляют или переименовывают существующий эндпоинт, канал, тип или поле полезной нагрузки.
- Изменяют тип существующего поля.
- Изменяют семантику существующего значения.

Критические изменения требуют:
1. Нового префикса версии API: `/api/v2/`.
2. Нового эндпоинта WebSocket: `ws://{host}:{port}/ws/v2`.
3. Обе версии работают параллельно в период миграции.

### 6.3 Некритические изменения (допускаются свободно)

- Добавление новых эндпоинтов.
- Добавление новых каналов WebSocket или типов сообщений.
- Добавление новых опциональных полей в полезные нагрузки.
- Добавление новых значений в существующие перечисления (например, новые коды ошибок).
- Добавление новых параметров запроса со значениями по умолчанию.

### 6.4 Стратегия версионирования

| Когда | Действие |
|---|---|
| Аддитивное изменение | Обновить этот документ, увеличить минорную версию (1.1, 1.2, ...) |
| Критическое изменение | Создать `/api/v2/`, обновить этот документ, увеличить мажорную версию (2.0) |
| Обязательное ревью обеими командами | Любое изменение этого документа требует согласования обеих команд |

---

## 7. Приложение: История изменений

| Версия | Дата | Изменения |
|---|---|---|
| 1.0.0 | 2026-05-20 | Первоначальный контракт |
