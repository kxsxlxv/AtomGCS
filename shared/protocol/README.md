

# Документация: Бинарный протокол обмена GCS ↔ Модуль управления

## Версия протокола: 0x01

---

## Оглавление

1. [Общее описание](#1-общее-описание)
2. [Архитектура обмена](#2-архитектура-обмена)
3. [Формат пакета](#3-формат-пакета)
   - 3.1 [Заголовок (PacketHeader)](#31-заголовок-packetheader)
   - 3.2 [Полезная нагрузка (Payload)](#32-полезная-нагрузка-payload)
   - 3.3 [Контрольная сумма (CRC-8)](#33-контрольная-сумма-crc-8)
4. [Типы сообщений (MsgType)](#4-типы-сообщений-msgtype)
5. [Сообщения UI → Модуль управления](#5-сообщения-ui--модуль-управления)
   - 5.1 [CMD_COMMAND — Отправка команды](#51-cmd_command--отправка-команды)
   - 5.2 [CMD_SET_PARAMS — Параметры миссии](#52-cmd_set_params--параметры-миссии)
   - 5.3 [CMD_SET_MODE — Режим полёта](#53-cmd_set_mode--режим-полёта)
   - 5.4 [CMD_SIM_OBSTACLES — Имитация препятствий](#54-cmd_sim_obstacles--имитация-препятствий)
   - 5.5 [CMD_SIM_LIDAR — Имитация LiDAR](#55-cmd_sim_lidar--имитация-lidar)
6. [Сообщения Модуль управления → UI](#6-сообщения-модуль-управления--ui)
   - 6.1 [TEL_STATE — Состояние дрона](#61-tel_state--состояние-дрона)
   - 6.2 [TEL_POSITION — Позиция и скорость](#62-tel_position--позиция-и-скорость)
   - 6.3 [TEL_POINT_CLOUD — Облако точек LiDAR](#63-tel_point_cloud--облако-точек-lidar)
   - 6.4 [TEL_ACK — Подтверждение/Отказ](#64-tel_ack--подтверждениеотказ)
7. [Состояния дрона (DroneState)](#7-состояния-дрона-dronestate)
8. [Битовая маска доступных команд](#8-битовая-маска-доступных-команд)
9. [Утилиты протокола (protocol_utils.h)](#9-утилиты-протокола-protocol_utilsh)
   - 9.1 [Сериализация пакета](#91-сериализация-пакета)
   - 9.2 [Десериализация пакета](#92-десериализация-пакета)
   - 9.3 [Потоковый парсер TCP (PacketStreamParser)](#93-потоковый-парсер-tcp-packetstreamparser)
   - 9.4 [Вспомогательные функции](#94-вспомогательные-функции)
10. [Примеры использования](#10-примеры-использования)
11. [Как расширить протокол](#11-как-расширить-протокол)
12. [Важные технические детали](#12-важные-технические-детали)

---

## 1. Общее описание

Протокол предназначен для обмена данными между двумя программами:

| Программа | Роль | Где работает |
|---|---|---|
| **UI-интерфейс** (наземная станция) | Отправляет команды и параметры оператора | Компьютер оператора |
| **Модуль управления** (бортовое ПО) | Управляет дроном, обрабатывает LiDAR, шлёт телеметрию | Бортовой компьютер дрона |

Протокол — **бинарный** (не текстовый). Каждое сообщение — это последовательность байт фиксированной структуры. Это компактнее и быстрее, чем JSON/XML, что важно для передачи телеметрии в реальном времени.

### Транспорт

| Канал | Протокол | Что передаётся |
|---|---|---|
| **TCP** | Надёжный, с гарантией доставки | Команды, параметры, ACK, состояние |
| **UDP** | Быстрый, без гарантии доставки | Телеметрия позиции, облако точек LiDAR |

---

## 2. Архитектура обмена

```
UI-интерфейс                                    Модуль управления
┌──────────┐                                     ┌──────────────┐
│          │ ── CMD_COMMAND ──────────TCP──────→  │              │
│          │ ── CMD_SET_PARAMS ───────TCP──────→  │              │
│          │ ── CMD_SET_MODE ─────────TCP──────→  │              │
│          │ ── CMD_SIM_OBSTACLES ────TCP──────→  │              │
│          │ ── CMD_SIM_LIDAR ────────TCP──────→  │              │
│          │                                      │              │
│          │ ←──────────TCP──────── TEL_ACK ────  │              │
│          │ ←──────────TCP──────── TEL_STATE ──  │              │
│          │ ←──────────UDP──────── TEL_POSITION  │              │
│          │ ←──────────UDP──── TEL_POINT_CLOUD   │              │
└──────────┘                                     └──────────────┘
```

### Принцип взаимодействия

1. Оператор нажимает кнопку в UI (например, «Взлёт»)
2. UI формирует бинарный пакет `CMD_COMMAND` с `CommandId::TAKEOFF`
3. UI отправляет пакет по TCP
4. Модуль управления принимает пакет, проверяет CRC, парсит
5. Модуль проверяет, допустима ли команда в текущем состоянии
6. Модуль отправляет `TEL_ACK` — принято или отклонено
7. Модуль периодически шлёт `TEL_STATE` с текущим состоянием
8. UI отображает актуальное состояние дрона

---

## 3. Формат пакета

Каждое сообщение (в обе стороны) оборачивается в **пакет** одинаковой структуры:

```
Байт:  0     1     2       3        4     5       6 ... N-2    N-1
     ┌─────┬─────┬───────┬────────┬─────┬─────┬──────────────┬─────┐
     │ 0xAA│ 0x55│Version│MsgType │ Len │ Len │   Payload    │ CRC │
     │     │     │       │        │ (lo)│ (hi)│   (0..N байт)│     │
     └─────┴─────┴───────┴────────┴─────┴─────┴──────────────┴─────┘
     │◄──── Заголовок (6 байт) ────►│    │◄── Переменная ──►│     │
     │◄───────────────── Всё, кроме CRC ───────────────────►│     │
     │◄──────────────────── Весь пакет ──────────────────────────►│
```

### Минимальный размер пакета

Пакет без полезной нагрузки (payload = 0 байт):

```
Заголовок (6 байт) + CRC (1 байт) = 7 байт
```

### 3.1 Заголовок (PacketHeader)

```cpp
struct PacketHeader {
    uint8_t magic[2];      // Всегда 0xAA, 0x55 — маркер начала пакета
    uint8_t version;       // Версия протокола (сейчас 0x01)
    uint8_t msgType;       // Тип сообщения (см. MsgType)
    uint16_t payloadLen;   // Длина payload в байтах (little-endian)
};
// Размер: ровно 6 байт
```

**Пояснение по каждому полю:**

| Поле | Размер | Описание |
|---|---|---|
| `magic` | 2 байта | Фиксированные байты `0xAA 0x55`. Нужны для поиска начала пакета в потоке байт. Приёмник ищет эту последовательность, чтобы понять «тут начинается пакет» |
| `version` | 1 байт | Номер версии протокола. Если формат пакета изменится в будущем, версия увеличится. Приёмник может отклонить пакет с неизвестной версией |
| `msgType` | 1 байт | Тип сообщения из enum `MsgType`. Определяет, какая структура лежит в payload |
| `payloadLen` | 2 байта | Длина payload в байтах (0–65535). Записывается в формате **little-endian**: сначала младший байт, потом старший |

> **Little-endian** означает: число `0x0100` (256) записывается как байты `0x00 0x01`, а не `0x01 0x00`.

### 3.2 Полезная нагрузка (Payload)

Payload — это тело сообщения. Его содержимое зависит от `msgType`:

| MsgType | Payload-структура | Размер payload |
|---|---|---|
| `CMD_COMMAND` | `PayloadCommand` | 1 байт |
| `CMD_SET_PARAMS` | `PayloadMissionParams` | 12 байт |
| `CMD_SET_MODE` | `PayloadSetMode` | 1 байт |
| `CMD_SIM_OBSTACLES` | `PayloadSimObstacles` | 9 байт |
| `CMD_SIM_LIDAR` | `PayloadSimLidar` | 1 байт |
| `TEL_STATE` | `PayloadTelemetryState` | 7 байт |
| `TEL_POSITION` | `PayloadTelemetryPosition` | 32 байта |
| `TEL_POINT_CLOUD` | `PayloadPointCloudHeader` + массив `PointCloudPoint` | 8 + 13×N байт |
| `TEL_ACK` | `PayloadAck` | 67 байт |

### Контрольная сумма (CRC-8)

Последний байт пакета — контрольная сумма **CRC-8/ITU** (полином `0x07`).

**Как вычисляется:**
1. Берём все байты пакета **кроме последнего** (заголовок + payload)
2. Пропускаем через алгоритм CRC-8
3. Записываем результат в последний байт

**Зачем нужен:**
- Обнаружение повреждений данных при передаче
- Если CRC не совпал — пакет отбрасывается

**Алгоритм (побитовый):**

```
Начальное значение CRC = 0x00

Для каждого байта данных:
    CRC = CRC XOR байт
    Повторить 8 раз:
        Если старший бит CRC установлен:
            CRC = (CRC сдвиг влево на 1) XOR 0x07
        Иначе:
            CRC = CRC сдвиг влево на 1
```

**Пример на пальцах:**

```
Данные пакета (без CRC): [0xAA, 0x55, 0x01, 0x01, 0x01, 0x00, 0x02]
                                                                      ↑ payload = 1 байт (CommandId = 0x02)

CRC = crc8Atm([0xAA, 0x55, 0x01, 0x01, 0x01, 0x00, 0x02])
CRC = 0x?? (вычисленное значение)

Итоговый пакет: [0xAA, 0x55, 0x01, 0x01, 0x01, 0x00, 0x02, 0x??]
```

---

## 4. Типы сообщений (MsgType)

```cpp
enum class MsgType : uint8_t {
    // === UI → Модуль управления (диапазон 0x01 – 0x7F) ===
    CMD_COMMAND       = 0x01,  // Отправка команды (взлёт, посадка...)
    CMD_SET_PARAMS    = 0x02,  // Установка параметров миссии
    CMD_SET_MODE      = 0x03,  // Установка режима полёта
    CMD_SIM_OBSTACLES = 0x04,  // Имитация препятствий (отладка)
    CMD_SIM_LIDAR     = 0x05,  // Имитация вкл/выкл LiDAR (отладка)

    // === Модуль управления → UI (диапазон 0x81 – 0xFF) ===
    TEL_STATE         = 0x81,  // Состояние дрона + доступные команды
    TEL_POSITION      = 0x82,  // Координаты, скорость, курс
    TEL_POINT_CLOUD   = 0x83,  // Облако точек LiDAR
    TEL_ACK           = 0x84,  // Ответ на команду (принято/отклонено)
};
```

**Правило именования:**
- `CMD_*` (0x01–0x7F) — команды **от UI к модулю**
- `TEL_*` (0x81–0xFF) — телеметрия **от модуля к UI**

Это соглашение позволяет по одному байту понять направление сообщения: если `msgType < 0x80` — это команда, если `≥ 0x80` — телеметрия.

---

## 5. Сообщения UI → Модуль управления

### 5.1 CMD_COMMAND — Отправка команды

Самый частый тип сообщения. Пользователь нажимает кнопку — UI отправляет команду.

```cpp
struct PayloadCommand {
    uint8_t commandId;  // Значение из enum CommandId
};
// Размер: 1 байт
```

**Доступные команды:**

| CommandId | Значение | Название | Что делает |
|---|---|---|---|
| `PREPARE` | 0x01 | Подготовка | Переводит дрон в режим проверки систем |
| `TAKEOFF` | 0x02 | Взлёт | Набирает высоту из параметров миссии |
| `START_MISSION` | 0x03 | Начало миссии | Запускает выполнение миссии |
| `PAUSE_RESUME` | 0x04 | Пауза/Продолжить | Приостанавливает или возобновляет миссию |
| `RETURN_HOME` | 0x05 | Возврат домой | Летит к точке старта |
| `LAND` | 0x06 | Посадка | Выполняет посадку |
| `EMERGENCY_STOP` | 0x07 | Аварийная остановка | Экстренное прекращение полёта |

**Пример бинарного пакета** (команда TAKEOFF):

```
Байт:   0     1     2     3     4     5     6     7
       [0xAA][0x55][0x01][0x01][0x01][0x00][0x02][CRC]
        ╰magic─╯   vers  type  ╰─len=1──╯  cmd   crc
                          ↑                  ↑
                     CMD_COMMAND         TAKEOFF
```

### 5.2 CMD_SET_PARAMS — Параметры миссии

Отправляет настройки миссии перед полётом.

```cpp
struct PayloadMissionParams {
    uint32_t delayedStartTimeSec;  // Задержка перед стартом, секунды (0 = немедленно)
    float    takeoffAltitudeM;     // Высота взлёта, метры
    float    flightSpeedMS;        // Скорость полёта, м/с
};
// Размер: 12 байт (4 + 4 + 4)
```

**Раскладка по байтам:**

```
Смещение:  0  1  2  3    4  5  6  7    8  9  10  11
         [─delay────]  [─altitude──]  [──speed────]
          uint32 LE      float LE       float LE
```

> **float** в протоколе — стандартный IEEE 754 single precision (4 байта, little-endian).

### 5.3 CMD_SET_MODE — Режим полёта

```cpp
struct PayloadSetMode {
    uint8_t mode;  // Значение из enum FlightMode
};
// Размер: 1 байт

enum class FlightMode : uint8_t {
    AUTOMATIC      = 0x01,  // Полностью автоматический полёт
    SEMI_AUTOMATIC = 0x02,  // Полуавтоматический (оператор подтверждает действия)
};
```

### 5.4 CMD_SIM_OBSTACLES — Имитация препятствий

Отладочное сообщение. Позволяет из UI имитировать наличие препятствий вокруг дрона.

```cpp
struct PayloadSimObstacles {
    bool front;       // Препятствие спереди
    bool frontRight;  // Препятствие спереди-справа
    bool right;       // Препятствие справа
    bool backRight;   // Препятствие сзади-справа
    bool back;        // Препятствие сзади
    bool backLeft;    // Препятствие сзади-слева
    bool left;        // Препятствие слева
    bool frontLeft;   // Препятствие спереди-слева
    uint8_t vertical; // Вертикальное препятствие (VerticalObstacle)
};
// Размер: 9 байт
```

**Визуальное соответствие сетке 3×3 в UI:**

```
┌───────────┬──────────┬────────────┐
│ frontLeft │  front   │ frontRight │
│  (байт 7) │ (байт 0) │  (байт 1)  │
├───────────┼──────────┼────────────┤
│   left    │  ДРОН    │   right    │
│  (байт 6) │ (байт 8) │  (байт 2)  │
├───────────┼──────────┼────────────┤
│ backLeft  │  back    │ backRight  │
│  (байт 5) │ (байт 4) │  (байт 3)  │
└───────────┴──────────┴────────────┘

Дрон всегда «смотрит» вверх (на front).
```

**Вертикальное препятствие (центральная клетка):**

| Значение `vertical` | Состояние | Описание |
|---|---|---|
| `0` (NONE) | Нет вертикального препятствия | — |
| `1` (ABOVE) | Препятствие сверху | Над дроном по оси Z |
| `2` (BELOW) | Препятствие снизу | Под дроном по оси Z |

### 5.5 CMD_SIM_LIDAR — Имитация LiDAR

```cpp
struct PayloadSimLidar {
    bool lidarActive;  // true = LiDAR «включён», false = «выключен»
};
// Размер: 1 байт
```

Когда `lidarActive = false`, модуль управления перестаёт отправлять облако точек.

---

## 6. Сообщения Модуль управления → UI

### 6.1 TEL_STATE — Состояние дрона

Отправляется **периодически** (обычно 10 Гц) по TCP или UDP. Содержит текущее состояние и маску разрешённых команд.

```cpp
struct PayloadTelemetryState {
    uint8_t  currentState;       // Текущее состояние (DroneState)
    uint32_t availableCommands;  // Битовая маска доступных команд
    uint8_t  flightMode;         // Текущий режим полёта (FlightMode)
    uint8_t  batteryPercent;     // Заряд батареи, 0–100%
};
// Размер: 7 байт (1 + 4 + 1 + 1)
```

**Раскладка по байтам:**

```
Смещение:  0        1  2  3  4      5       6
         [state]  [─avail_cmds─]  [mode]  [batt]
          uint8     uint32 LE      uint8   uint8
```

### 6.2 TEL_POSITION — Позиция и скорость

Отправляется по UDP с высокой частотой (10–50 Гц).

```cpp
struct PayloadTelemetryPosition {
    float posX, posY, posZ;    // Координаты, метры
    float velX, velY, velZ;    // Скорости, м/с
    float headingDeg;          // Курс (направление носа), градусы 0–360
    float altitudeAglM;        // Высота над землёй (AGL), метры
};
// Размер: 32 байта (8 × float)
```

**Раскладка по байтам:**

```
Смещение:  0   4   8   12  16  20  24  28
         [pX][pY][pZ][vX][vY][vZ][hd][al]
          ╰─позиция──╯ ╰─скорость──╯ ╰──╯
          float × 3     float × 3   float×2
```

### 6.3 TEL_POINT_CLOUD — Облако точек LiDAR

Самый большой тип сообщения. Отправляется по **UDP**.

**Структура payload:**

```
┌──────────────────────────┬──────────────────────────────────────┐
│ PayloadPointCloudHeader  │   PointCloudPoint[0..numPoints-1]    │
│ (8 байт)                │   (13 байт × numPoints)              │
└──────────────────────────┴──────────────────────────────────────┘
```

```cpp
struct PayloadPointCloudHeader {
    uint32_t timestampMs;  // Метка времени, миллисекунды
    uint32_t numPoints;    // Количество точек в массиве
};
// Размер: 8 байт

struct PointCloudPoint {
    float x, y, z;         // Координаты точки, метры
    uint8_t intensity;     // Яркость отражения, 0–255
};
// Размер: 13 байт
```

**Пример:** пакет с 100 точками:

```
Заголовок пакета:  6 байт
Payload:           8 + (13 × 100) = 1308 байт
CRC:               1 байт
Итого:             1315 байт
```

**Максимум точек в одном пакете:**

```
payloadLen — максимум uint16 = 65535 байт
(65535 - 8) / 13 = 5040 точек
```

Это значение доступно как константа `maxPointCloudPointsPerPacket`.

### 6.4 TEL_ACK — Подтверждение/Отказ

Модуль управления отправляет в ответ на каждую полученную команду.

```cpp
struct PayloadAck {
    uint8_t originalMsgType;    // На какой тип сообщения ответ (MsgType)
    uint8_t originalCommandId;  // Какая команда (CommandId), или 0 если не CMD_COMMAND
    uint8_t result;             // Результат (AckResult)
    char    message[64];        // Текстовое пояснение (null-terminated)
};
// Размер: 67 байт (1 + 1 + 1 + 64)
```

**Результаты:**

| AckResult | Значение | Описание |
|---|---|---|
| `SUCCESS` | 0x00 | Команда принята и выполняется |
| `REJECTED` | 0x01 | Команда отклонена (не тот этап полёта) |
| `INVALID_PARAM` | 0x02 | Неверные параметры |
| `ERROR` | 0x03 | Внутренняя ошибка модуля |

**Пример:** UI отправил `CMD_COMMAND(TAKEOFF)`, но дрон ещё не прошёл подготовку:

```
PayloadAck {
    originalMsgType  = 0x01 (CMD_COMMAND)
    originalCommandId = 0x02 (TAKEOFF)
    result           = 0x01 (REJECTED)
    message          = "Drone is not in READY state\0..."
}
```

---

## 7. Состояния дрона (DroneState)

Дрон в каждый момент времени находится в одном из состояний:

```
DISCONNECTED ──(TCP подключение)──→ CONNECTED
CONNECTED ────(автоматически)──────→ IDLE
IDLE ─────────(PREPARE)────────────→ PREPARING
PREPARING ────(таймер ~3сек)───────→ READY
READY ────────(TAKEOFF)────────────→ ARMING
ARMING ───────(таймер ~2сек)───────→ TAKING_OFF
TAKING_OFF ───(таймер ~5сек)───────→ IN_FLIGHT
IN_FLIGHT ────(START_MISSION)──────→ EXECUTING_MISSION
IN_FLIGHT ────(RETURN_HOME)────────→ RETURNING_HOME
IN_FLIGHT ────(LAND)───────────────→ LANDING
EXECUTING_MISSION ─(PAUSE_RESUME)──→ PAUSED
EXECUTING_MISSION ─(RETURN_HOME)───→ RETURNING_HOME
PAUSED ───────(PAUSE_RESUME)───────→ EXECUTING_MISSION
PAUSED ───────(RETURN_HOME)────────→ RETURNING_HOME
RETURNING_HOME ─(таймер ~10сек)────→ LANDING
LANDING ──────(таймер ~5сек)───────→ LANDED
LANDED ───────(PREPARE)────────────→ PREPARING

Любое состояние ─(ошибка)──────────→ ERROR
ERROR ────────(PREPARE)────────────→ PREPARING
```

**Таблица всех состояний:**

| DroneState | Код | Описание | Цвет индикатора |
|---|---|---|---|
| `DISCONNECTED` | 0x00 | Нет соединения с модулем | ⚫ Серый |
| `CONNECTED` | 0x01 | TCP подключён, инициализация | 🟡 Жёлтый |
| `IDLE` | 0x02 | Ожидание команд | 🔵 Синий |
| `PREPARING` | 0x03 | Проверка систем | 🟡 Жёлтый |
| `READY` | 0x04 | Готов к взлёту | 🟢 Зелёный |
| `ARMING` | 0x05 | Запуск моторов | 🟡 Жёлтый |
| `TAKING_OFF` | 0x06 | Набор высоты | 🟡 Жёлтый |
| `IN_FLIGHT` | 0x07 | В воздухе, ожидает команды | 🟢 Зелёный |
| `EXECUTING_MISSION` | 0x08 | Выполняет миссию | 🟢 Зелёный |
| `PAUSED` | 0x09 | Миссия на паузе, зависает в воздухе | 🟡 Жёлтый |
| `RETURNING_HOME` | 0x0A | Летит к точке старта | 🟡 Жёлтый |
| `LANDING` | 0x0B | Снижается для посадки | 🟡 Жёлтый |
| `LANDED` | 0x0C | На земле после полёта | 🔵 Синий |
| `ERROR` | 0x0D | Ошибка, требуется вмешательство | 🔴 Красный |
| `EMERGENCY_LANDING` | 0x0E | Аварийная посадка | 🔴 Красный |

> **Важно:** UI не реализует логику переходов! UI только отображает состояние, полученное от модуля управления в `TEL_STATE`.

---

## 8. Битовая маска доступных команд

Поле `availableCommands` в `PayloadTelemetryState` — это 32-битное число, где каждый бит соответствует одной команде.

### Соответствие битов и команд

```
Бит 0 (значение 0x00000001) → CommandId::PREPARE       (0x01)
Бит 1 (значение 0x00000002) → CommandId::TAKEOFF        (0x02)
Бит 2 (значение 0x00000004) → CommandId::START_MISSION   (0x03)
Бит 3 (значение 0x00000008) → CommandId::PAUSE_RESUME    (0x04)
Бит 4 (значение 0x00000010) → CommandId::RETURN_HOME     (0x05)
Бит 5 (значение 0x00000020) → CommandId::LAND            (0x06)
Бит 6 (значение 0x00000040) → CommandId::EMERGENCY_STOP  (0x07)
```

### Формула

```
Номер бита = CommandId - 1
Маска бита = 1 << (CommandId - 1)
```

Это реализовано функцией:

```cpp
uint32_t commandBit(CommandId commandId) {
    return 1u << (static_cast<uint8_t>(commandId) - 1u);
}
```

### Пример

Модуль управления в состоянии `READY` разрешает команды `PREPARE` и `TAKEOFF`:

```
availableCommands = commandBit(PREPARE) | commandBit(TAKEOFF)
                  = (1 << 0) | (1 << 1)
                  = 0x01 | 0x02
                  = 0x00000003
```

UI проверяет, доступна ли кнопка:

```cpp
if (isCommandAvailable(telemetry.availableCommands, CommandId::TAKEOFF)) {
    // Кнопка активна
} else {
    // Кнопка серая (disabled)
}
```

---

## 9. Утилиты протокола (protocol_utils.h)

### 9.1 Сериализация пакета

**Задача:** взять тип сообщения и структуру payload, собрать готовый пакет (массив байт).

```cpp
// Вариант 1: из структуры (наиболее частый)
PayloadCommand cmd;
cmd.commandId = static_cast<uint8_t>(CommandId::TAKEOFF);
std::vector<uint8_t> packet = serializePacket(MsgType::CMD_COMMAND, cmd);

// Вариант 2: из сырых байт
std::vector<uint8_t> packet = serializePacket(MsgType::CMD_COMMAND, rawBytes);
```

**Что делает `serializePacket` внутри:**

```
1. Вычисляет размер пакета = 6 (заголовок) + sizeof(payload) + 1 (CRC)
2. Записывает magic:    0xAA, 0x55
3. Записывает version:  0x01
4. Записывает msgType:  (переданный тип)
5. Записывает payloadLen: sizeof(payload) в little-endian
6. Копирует payload как байты (memcpy)
7. Вычисляет CRC-8 от байт [0..N-2]
8. Записывает CRC в последний байт
9. Возвращает vector<uint8_t> — готовый пакет для отправки в сокет
```

### 9.2 Десериализация пакета

**Задача:** из массива байт (полученного из сети) извлечь тип сообщения и payload.

**Шаг 1: Парсинг пакета**

```cpp
PacketParseError error;
auto view = tryParsePacket(packetBytes, &error);

if (!view.has_value()) {
    // Пакет некорректный, причина в error:
    // TooShort, InvalidMagic, InvalidVersion, LengthMismatch, CrcMismatch
    return;
}

// view->msgType  — тип сообщения
// view->payload  — span на байты payload (ссылка, не копия!)
```

**Что проверяет `tryParsePacket`:**

```
1. Достаточно ли байт? (минимум 7)
2. Magic = 0xAA55?
3. Version = 0x01?
4. payloadLen + 7 == общий размер?
5. CRC совпадает?
→ Если всё ок — возвращает PacketView {msgType, payload}
→ Если нет — возвращает nullopt + код ошибки
```

**Шаг 2: Парсинг payload в структуру**

```cpp
switch (view->msgType) {
    case MsgType::TEL_STATE: {
        PayloadTelemetryState state;
        if (parsePayload(view->payload, state)) {
            // Используем state.currentState, state.availableCommands, ...
        }
        break;
    }
    case MsgType::TEL_ACK: {
        PayloadAck ack;
        if (parsePayload(view->payload, ack)) {
            // Используем ack.result, ack.message, ...
        }
        break;
    }
}
```

`parsePayload` просто проверяет размер и делает `memcpy`:

```cpp
template <typename Payload>
bool parsePayload(span<const uint8_t> bytes, Payload& payload) {
    if (bytes.size() != sizeof(Payload)) return false;
    memcpy(&payload, bytes.data(), sizeof(Payload));
    return true;
}
```

### 9.3 Потоковый парсер TCP (PacketStreamParser)

**Проблема TCP:** данные приходят произвольными кусками. Один `recv()` может вернуть:
- Половину пакета
- Два пакета склеенных вместе
- Полтора пакета
- Мусор + пакет + мусор

**Решение: `PacketStreamParser`** — накапливает байты в буфере и извлекает целые пакеты.

```
Вызов 1: recv() → [0xAA 0x55 0x01 0x01 0x01]     ← неполный пакет
Вызов 2: recv() → [0x00 0x02 0xCR 0xAA 0x55 ...]  ← конец первого + начало второго

PacketStreamParser объединяет всё в буфер:
[0xAA 0x55 0x01 0x01 0x01 0x00 0x02 0xCR 0xAA 0x55 ...]
 ╰────────── пакет 1 ──────────────╯ ╰─── начало пакета 2...
```

**Использование:**

```cpp
PacketStreamParser parser;

// В цикле приёма:
char buf[4096];
int bytesRead = socket.recv(buf, sizeof(buf));
parser.append({reinterpret_cast<uint8_t*>(buf), bytesRead});

// Извлечь все готовые пакеты:
auto packets = parser.extractPackets();
for (auto& packetBytes : packets) {
    auto view = tryParsePacket(packetBytes);
    if (view) {
        handlePacket(*view);
    }
}
```

**Алгоритм `extractPackets`:**

```
ЦИКЛ:
  1. Ищем magic (0xAA 0x55) в буфере
  2. Не нашли → очищаем буфер, выходим
  3. Нашли не в начале → удаляем мусор до magic
  4. Читаем payloadLen из байт [4:5]
  5. Вычисляем полный размер = 6 + payloadLen + 1
  6. Не хватает байт → ждём следующий recv(), выходим
  7. Хватает → пробуем tryParsePacket()
     - Успех: добавляем в результат, удаляем из буфера
     - Ошибка CRC: удаляем первый байт (magic был ложный), повторяем
КОНЕЦ ЦИКЛА
```

### 9.4 Вспомогательные функции

**Преобразование enum → строка:**

```cpp
const char* droneStateToString(DroneState state);   // IDLE → "Ожидание"
const char* commandIdToString(CommandId cmd);        // TAKEOFF → "ВЗЛЁТ"
const char* flightModeToString(FlightMode mode);     // AUTOMATIC → "АВТО"
const char* ackResultToString(AckResult result);      // REJECTED → "Отклонено"
const char* msgTypeToString(MsgType msgType);         // CMD_COMMAND → "CMD_COMMAND"
```

**Работа с ACK-сообщениями:**

```cpp
// Безопасно извлечь текст из PayloadAck::message
std::string text = ackMessageToString(ack);
// Ищет '\0' в массиве char[64], возвращает string до терминатора
```

---

## 10. Примеры использования

### Пример 1: Отправить команду «Взлёт»

```cpp
#include "shared/protocol/protocol.h"
#include "shared/protocol/protocol_utils.h"

using namespace gcs::protocol;

// 1. Создаём payload
PayloadCommand cmd;
cmd.commandId = static_cast<uint8_t>(CommandId::TAKEOFF);

// 2. Сериализуем в пакет
std::vector<uint8_t> packet = serializePacket(MsgType::CMD_COMMAND, cmd);

// 3. Отправляем по TCP
socket.send(packet.data(), packet.size());

// packet содержит: [AA 55 01 01 01 00 02 CRC] — 8 байт
//                   magic  v  t  len=1  cmd crc
```

### Пример 2: Отправить параметры миссии

```cpp
PayloadMissionParams params;
params.delayedStartTimeSec = 0;       // Без задержки
params.takeoffAltitudeM    = 15.0f;   // 15 метров
params.flightSpeedMS       = 5.0f;    // 5 м/с

auto packet = serializePacket(MsgType::CMD_SET_PARAMS, params);
socket.send(packet.data(), packet.size());
// Размер пакета: 6 + 12 + 1 = 19 байт
```

### Пример 3: Принять и обработать телеметрию

```cpp
PacketStreamParser parser;

// Приём данных
char buf[4096];
int n = socket.recv(buf, sizeof(buf));
parser.append({reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(n)});

// Извлечение пакетов
for (auto& raw : parser.extractPackets()) {
    auto pkt = tryParsePacket(raw);
    if (!pkt) continue;

    switch (pkt->msgType) {
        case MsgType::TEL_STATE: {
            PayloadTelemetryState state;
            if (parsePayload(pkt->payload, state)) {
                std::cout << "Состояние: " 
                          << droneStateToString(static_cast<DroneState>(state.currentState))
                          << ", батарея: " << (int)state.batteryPercent << "%\n";
            }
            break;
        }
        case MsgType::TEL_ACK: {
            PayloadAck ack;
            if (parsePayload(pkt->payload, ack)) {
                std::cout << "ACK: " << ackResultToString(static_cast<AckResult>(ack.result))
                          << " — " << ackMessageToString(ack) << "\n";
            }
            break;
        }
        case MsgType::TEL_POSITION: {
            PayloadTelemetryPosition pos;
            if (parsePayload(pkt->payload, pos)) {
                std::cout << "Позиция: (" << pos.posX << ", " << pos.posY 
                          << ", " << pos.posZ << ")\n";
            }
            break;
        }
    }
}
```

### Пример 4: Обработка облака точек

```cpp
case MsgType::TEL_POINT_CLOUD: {
    // Payload = заголовок (8 байт) + массив точек (13 × N байт)
    if (pkt->payload.size() < sizeof(PayloadPointCloudHeader)) break;

    PayloadPointCloudHeader header;
    std::memcpy(&header, pkt->payload.data(), sizeof(header));

    size_t expectedSize = sizeof(header) + header.numPoints * sizeof(PointCloudPoint);
    if (pkt->payload.size() != expectedSize) break;

    // Указатель на массив точек
    const auto* points = reinterpret_cast<const PointCloudPoint*>(
        pkt->payload.data() + sizeof(header)
    );

    // Обработка точек
    for (uint32_t i = 0; i < header.numPoints; ++i) {
        float x = points[i].x;
        float y = points[i].y;
        float z = points[i].z;
        uint8_t intensity = points[i].intensity;
        // Загрузить в OpenGL буфер для визуализации...
    }
    break;
}
```

### Пример 5: Отправить имитацию препятствий

```cpp
PayloadSimObstacles obs{};          // Обнулить все поля
obs.front     = true;               // Препятствие спереди
obs.frontLeft = true;               // Препятствие спереди-слев��
obs.vertical  = static_cast<uint8_t>(VerticalObstacle::ABOVE);  // Сверху

auto packet = serializePacket(MsgType::CMD_SIM_OBSTACLES, obs);
socket.send(packet.data(), packet.size());
```

---

## 11. Как расширить протокол

### Добавить новую команду

**Шаг 1:** Добавить значение в `CommandId`:

```cpp
enum class CommandId : uint8_t {
    // ... существующие ...
    EMERGENCY_STOP = 0x07,
    HOLD_POSITION  = 0x08,  // ← Новая команда
};
```

**Шаг 2:** Добавить строковое представление:

```cpp
// protocol_utils.h
inline const char* commandIdToString(CommandId commandId) {
    // ... существующие ...
    case CommandId::HOLD_POSITION: return "HOLD_POSITION";
}
```

**Шаг 3:** Обработать в модуле управления (стейт-машина).

**Шаг 4:** Добавить бит в `availableCommands` — происходит автоматически через `commandBit()`.

### Добавить новый параметр миссии

**Шаг 1:** Добавить поле в структуру:

```cpp
struct PayloadMissionParams {
    uint32_t delayedStartTimeSec;
    float    takeoffAltitudeM;
    float    flightSpeedMS;
    float    maxRangeM;          // ← Новое поле
};
```

**Шаг 2:** Обновить `static_assert`:

```cpp
static_assert(sizeof(PayloadMissionParams) == 16);  // Было 12, стало 16
```

> ⚠️ **Важно:** при добавлении полей в payload меняется размер. Обе стороны (UI и модуль) должны быть пересобраны с одним и тем же `protocol.h`.

### Добавить новый тип сообщения

**Шаг 1:** Добавить `MsgType`:

```cpp
enum class MsgType : uint8_t {
    // ...
    CMD_UPLOAD_WAYPOINTS = 0x06,  // ← Новый тип (UI → модуль)
};
```

**Шаг 2:** Определить payload-структуру:

```cpp
struct PayloadWaypoint {
    float latitude;
    float longitude;
    float altitude;
};

struct PayloadUploadWaypoints {
    uint8_t count;
    PayloadWaypoint waypoints[16];  // Максимум 16 точек
};
```

**Шаг 3:** Добавить `static_assert`.

**Шаг 4:** Добавить `msgTypeToString`.

**Шаг 5:** Обработать в обеих программах.

---

## 12. Важные технические детали

### Порядок байт (Endianness)

Протокол использует **little-endian** для всех многобайтовых полей. На x86 и ARM (Linux, Windows) это native порядок, поэтому `memcpy` работает корректно без конвертации.

### Выравнивание структур

Все payload-структуры обёрнуты в `#pragma pack(push, 1)`. Это гарантирует, что компилятор **не** вставит padding-байты между полями. Без этого `sizeof(PayloadTelemetryState)` мог бы быть 8 вместо 7 (из-за выравнивания `uint32_t`).

### Потокобезопасность

Функции в `protocol_utils.h` — stateless (не используют глобальные переменные). Их можно вызывать из любого потока без синхронизации. Исключение — `PacketStreamParser`, у которого есть внутренний буфер: один экземпляр — один поток.

### Максимальные размеры

| Параметр | Значение |
|---|---|
| Максимальный payload | 65535 байт |
| Максимальный пакет | 65542 байта (6 + 65535 + 1) |
| Максимум точек в одном пакете | 5040 |
| Размер заголовка | 6 байт |
| Размер CRC | 1 байт |
| Минимальный пакет (пустой payload) | 7 байт |