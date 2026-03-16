# ТЗ: серверная оптимизация runtime snapshot для SCADA

Дата: `2026-03-15`

## 1. Цель

Разработать изменения на стороне контроллера `STM32 master / Modbus TCP server`, которые обеспечат:

- стабильную выдачу данных для не менее чем `20` slave-устройств;
- выдачу не менее `200` аналоговых точек телеметрии;
- фактическое обновление данных на клиенте в пределах `5..10` секунд;
- отсутствие систематических `ModbusTcp response timeout`, вызванных тяжёлым чтением register map;
- сохранение topology-driven архитектуры и текущего generic command ingress.

## 2. Основание

Текущая карта `MAP_VERSION = 4` описана в [master_protocol.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\master_protocol.md) и использует:

- окно `points` с форматом `6` регистров на точку;
- окно `slave_status` с форматом `8` регистров на slave;
- ограничение `MAX_POINTS = 180`;
- единый `Modbus TCP` сервер на порту `502`.

Для целевой нагрузки текущая модель недостаточна:

- `200` аналоговых точек в текущем формате требуют `200 * 6 = 1200` регистров;
- `20` slave status в текущем формате требуют `20 * 8 = 160` регистров;
- клиент вынужден делать слишком много `FC3` запросов для одного цикла обновления;
- в `points` окне передаётся повторяющаяся метаинформация (`module_id`, `flags`, `quality`) ценой лишних регистров;
- Modbus TCP чтение не должно зависеть от живой работы runtime и ожидания RS485-ответов.

## 3. Целевые показатели

Серверная часть обязана обеспечить одновременно все условия:

1. Поддержка не менее `20` slave-устройств в production-конфигурации.
2. Поддержка не менее `200` аналоговых runtime points.
3. Резерв по масштабу:
   - не менее `32` slave slots;
   - не менее `256` fast analog points.
4. Полный внутренний цикл сбора данных со slave:
   - номинально не более `5` секунд;
   - в деградированном режиме при отказе одного slave не более `8` секунд.
5. Публикация целостного fast snapshot для клиента:
   - без torn reads;
   - без блокировки Modbus TCP обработчика длительными копированиями.
6. Нормальная работа клиента с чтением fast snapshot не более чем за `5` запросов `FC3` на один полный снимок fast-данных.
7. Один неответивший slave не должен останавливать обновление остальных данных и не должен приводить к TCP-flapping.

## 4. Область работ

В состав работ входит:

- расширение register map контроллера;
- введение отдельного fast runtime snapshot bank;
- введение двойного буфера или эквивалентного механизма безопасной публикации snapshot;
- переработка master runtime так, чтобы Modbus TCP читал только опубликованный cache;
- расширение directory/meta информации о fast runtime bank;
- расширение диагностических счётчиков;
- host/unit/integration/HIL проверки под целевую нагрузку.

В состав работ не входит:

- изменение generic command ingress окна `CMD_BASE`, если это не требуется для совместимости;
- удаление старых окон `points` и `slave_status` из `MAP_VERSION=4` совместимости;
- изменение клиентского UI в рамках данного ТЗ;
- переработка topology JSON, кроме минимально необходимой валидации лимитов.

## 5. Архитектурные требования

### 5.1 Разделение producer и consumer

Сервер обязан быть разделён на два контура:

- `runtime acquisition`: опрашивает RS485 slave и наполняет staging cache;
- `Modbus TCP serving`: только отдаёт уже опубликованный snapshot и не инициирует живой опрос slave.

Запрещено:

- выполнять ожидание ответа slave внутри пути `GH_ModbusMap_ReadHolding(...)`;
- строить fast snapshot "на лету" во время TCP-чтения;
- держать длительные блокировки, которые задерживают ответ `Modbus TCP`.

### 5.2 Публикация snapshot

Должен использоваться один из безопасных вариантов:

- double-buffer с атомарным swap указателя на опубликованный bank;
- либо эквивалентный механизм, гарантирующий целостность чтения.

Требования:

- клиент не должен видеть частично обновлённый snapshot;
- переключение опубликованного snapshot должно занимать константное время;
- bank должен содержать `SEQ_BEGIN` и `SEQ_END` для проверки целостности на стороне клиента;
- опубликованный `SEQ` должен меняться только после завершения полного цикла публикации.

### 5.3 Политика внутреннего опроса slave

Master runtime обязан:

- работать по модели `один RS485 запрос в полёте`;
- использовать round-robin или эквивалентный детерминированный планировщик;
- читать данные со slave максимально крупными contiguous-блоками;
- не выполнять опрос "по одному датчику";
- при timeout одного slave помечать его данные stale и продолжать цикл дальше;
- ограничивать retry для одного slave, чтобы не разрушать общий budget цикла.

Рекомендуемая политика:

- `1` основной запрос на fast runtime payload со slave за цикл;
- `0..1` повтор при timeout;
- после неуспеха переход к следующему slave без остановки полного sweep.

## 6. Изменения register map

### 6.1 Версия карты

Необходимо ввести `MAP_VERSION = 5`.

Требования к совместимости:

- legacy окна `points`, `slave_status`, `cmd`, `dir`, `diag`, `topology` сохраняются;
- новый клиент использует fast bank при `MAP_VERSION >= 5`;
- старый клиент может продолжать работать через legacy окна в режиме совместимости.

### 6.2 Лимиты fast runtime

Сервер обязан зарезервировать:

- `FAST_MAX_ANALOG_POINTS = 256`;
- `FAST_MAX_SLAVES = 32`.

Эти лимиты должны валидироваться при инициализации runtime и при валидации topology/runtime contract.

### 6.3 Новые окна

Предлагается добавить следующие окна после текущего `GH_MB_TOTAL_REGS = 1552`.

#### `FAST_DIR`

- `GH_MB_FAST_DIR_BASE = 1552`
- `GH_MB_FAST_DIR_REGS = 32`

Назначение:

- публикация capability flags;
- публикация размеров и баз fast bank;
- публикация числовых лимитов;
- публикация признака поддержки fast snapshot.

Минимальный состав полей:

- `+0` `FAST_CAPS`
- `+1` `FAST_BANK_BASE`
- `+2` `FAST_BANK_REGS`
- `+3` `FAST_MAX_ANALOG_POINTS`
- `+4` `FAST_MAX_SLAVES`
- `+5` `FAST_ANALOG_WORDS_PER_POINT` (`2`)
- `+6` `FAST_QUALITY_PACKING_KIND`
- `+7` `FAST_SLAVE_WORDS_PER_ENTRY`
- `+8..+9` `ACTIVE_GENERATION`
- `+10` `ACTIVE_FAST_ANALOG_COUNT`
- `+11` `ACTIVE_FAST_SLAVE_COUNT`
- `+12..+31` reserved

#### `FAST_BANK`

- `GH_MB_FAST_BANK_BASE = 1584`
- `GH_MB_FAST_BANK_REGS = 617`

Layout:

- `+0` `SEQ_BEGIN`
- `+1` `CAPS`
- `+2` `ANALOG_COUNT`
- `+3` `SLAVE_COUNT`
- `+4` `LAST_CYCLE_DURATION_MS`
- `+5` `LAST_PUBLISH_AGE_MS`
- `+6..+7` `ACTIVE_GENERATION`
- `+8..+519` `ANALOG_VALUE[256]` as `float32 hi/lo`
- `+520..+551` `ANALOG_QUALITY_PACKED[256]`
- `+552..+615` `SLAVE_FAST_STATUS[32]`
- `+616` `SEQ_END`

### 6.4 Формат fast analog data

Для каждой analog point:

- `2` регистра на значение `float32`;
- порядок точек: строго по `publish_index`;
- точки с индексами `0..ANALOG_COUNT-1` считаются валидной fast-последовательностью.

Требования:

- fast bank должен экспортировать только быстрые runtime-значения;
- повторяющиеся `module_id` и прочая метаинформация не должны включаться в fast path;
- mapping `publish_index -> semantic/module` по-прежнему задаётся topology.

### 6.5 Формат packed quality

Необходимо упаковать quality в компактный массив:

- `2` бита на точку;
- `8` quality значений на один holding register;
- кодирование quality должно совпадать с текущей логикой:
  - `0 = OK`
  - `1 = STALE`
  - `2 = FAULT`
  - `3 = OFFLINE`

`256` точек требуют `32` регистра.

### 6.6 Формат fast slave status

Для каждого slave fast-путь должен содержать не полный legacy status block, а компактный статус:

- `2` регистра на slave;
- `32` slave требуют `64` регистра.

Предлагаемый формат:

- `reg0` `FLAGS`
  - bit0 `online`
  - bit1 `stale`
  - bit2 `timeout_seen`
  - bit3 `crc_seen`
  - bit4 `exception_seen`
  - bit5 `config_error`
  - bit6 `data_invalid`
  - остальные биты reserved
- `reg1` `LAST_OK_AGE_SEC`

Детальные counters `ERR_TIMEOUT/ERR_CRC/ERR_EXCEPTION/DATA_VERSION/...` остаются в legacy `slave_status` окне для медленного polling/debug.

## 7. Поведение runtime

### 7.1 Fast snapshot update contract

Каждый опубликованный fast snapshot обязан представлять собой консистентный срез состояния на момент завершения очередного internal sweep.

Требования:

- `SEQ_BEGIN == SEQ_END`;
- новое значение sequence публикуется только после готовности всего bank;
- если client прочитал bank с разными `SEQ_BEGIN/SEQ_END`, такой snapshot считается недействительным;
- `LAST_PUBLISH_AGE_MS` обязан отражать возраст опубликованного snapshot относительно текущего runtime времени.

### 7.2 Деградация при проблемах со slave

При потере связи с одним или несколькими slave:

- master не должен зависать в одном slave;
- master продолжает обновлять доступные slave;
- значения недоступного slave и связанных points переходят в `STALE` или `OFFLINE` по детерминированной политике;
- `LAST_PUBLISH_AGE_MS` продолжает обновляться, если snapshot опубликован;
- должны обновляться специальные internal diagnostic counters.

### 7.3 Retry budget

Сервер обязан ограничивать retry budget внутри одного полного sweep.

Рекомендуемое правило:

- не более `1` повторного опроса на slave в рамках одного sweep;
- после превышения budget slave помечается stale;
- следующий шанс на успешный опрос slave получает в следующем sweep.

## 8. Изменения diagnostics

Необходимо добавить диагностические поля и/или счётчики, доступные через `diag` окно или отдельные runtime counters:

- `FAST_PUBLISH_COUNT`
- `FAST_PUBLISH_FAIL_COUNT`
- `FAST_LAST_SWEEP_MS`
- `FAST_MAX_SWEEP_MS`
- `FAST_OVERRUN_COUNT`
- `SLAVE_TIMEOUT_DURING_SWEEP_COUNT`
- `SLAVE_RETRY_COUNT`
- `SNAPSHOT_SEQ_LAST`

Требования:

- диагностические счётчики не должны обновляться ценой ухудшения fast path;
- клиент и инженерные утилиты должны иметь возможность быстро понять, тормозит ли runtime sweep или тормозит TCP transport.

## 9. Требования к производительности

Система должна быть спроектирована так, чтобы при целевой нагрузке:

- клиент получал весь fast snapshot не более чем за `5` чтений `FC3`;
- одно чтение fast bank размером до `125` регистров не требовало живой синхронизации с runtime;
- полная публикация fast snapshot происходила чаще, чем клиентский polling interval `5..10` секунд.

Расчётный budget fast snapshot:

- `ANALOG_VALUE[256]` = `512` регистров;
- `ANALOG_QUALITY_PACKED[256]` = `32` регистра;
- `SLAVE_FAST_STATUS[32]` = `64` регистра;
- служебный header/footer = `9` регистров;
- суммарно `617` регистров;
- это укладывается в `5` запросов `FC3` по `125` регистров максимум.

## 10. Влияние на topology и runtime contract

Требования:

- topology-driven модель сохраняется;
- fast bank публикует данные по `publish_index`;
- сервер обязан валидировать, что число fast analog points не превышает `256`;
- сервер обязан явно сигнализировать через `FAST_CAPS`, что fast snapshot поддерживается.

Допускается:

- в `MAP_VERSION = 5` fast bank публикует все runtime analog points по `publish_index`;
- detailed legacy `points` окно остаётся для совместимости и инженерной диагностики.

## 11. Требования к совместимости

Сервер обязан:

- сохранить поддержку текущих `FC3`, `FC6`, `FC16`;
- не ломать generic command ingress `CMD_BASE`;
- не ломать topology metadata/upload pipeline;
- оставить legacy `MAP_VERSION = 4` окна совместимыми по адресам и семантике.

Новый fast bank должен быть добавлен как расширение, а не как замена обязательных старых окон на первом этапе.

## 12. Изменения в кодовой базе сервера

Минимально ожидаются изменения в следующих подсистемах:

- `Core/Src/gh_modbus_map.c`
- `Core/Inc/gh_modbus_map.h`
- `Core/Src/gh_modbus_master.c`
- `Core/Inc/gh_runtime_state.h`
- `Core/Src/Modbus.c`
- host tests / integration tests / HIL scripts

Ожидаемые типы изменений:

- новые константы register map;
- новые runtime structures для fast bank;
- staging/published buffers;
- логика atomic publish;
- compact packing quality/status;
- новые diagnostics counters;
- проверка лимитов runtime/topology.

## 13. Предпосылки и зависимости

Данное ТЗ предполагает, что master может получать fast runtime данные со slave достаточно крупными блоками.

Если текущий slave-side протокол не позволяет:

- получить все fast analog значения slave одним contiguous-запросом;
- получить краткий slave health/status без множества мелких запросов;

то потребуется отдельное ТЗ на slave-side protocol extension. Без этого достижение целевого budget `20 slave / 200 analog / 5..10 s` может оказаться недостижимым даже при корректной реализации fast bank на master.

## 14. Этапы реализации

### Этап 1. Register map v5

- ввести `MAP_VERSION = 5`;
- добавить `FAST_DIR`;
- добавить `FAST_BANK`;
- обновить документацию по map layout.

### Этап 2. Runtime cache and publish

- внедрить staging/published buffers;
- реализовать safe publish;
- реализовать packed quality;
- реализовать compact slave fast status.

### Этап 3. Internal sweep optimization

- оптимизировать мастер-опрос slave под крупные contiguous-блоки;
- ввести retry budget;
- ввести non-blocking деградацию при отказе части slave.

### Этап 4. Diagnostics and validation

- добавить counters по sweep/publish;
- покрыть unit/host tests;
- провести bench/HIL сценарии под целевой нагрузкой.

## 15. Критерии приёмки

Решение считается принятым, если выполнены все условия:

1. `MAP_VERSION = 5` реализован и документирован.
2. Сервер экспортирует `FAST_DIR` и `FAST_BANK` по зафиксированному контракту.
3. Сервер поддерживает не менее `256` fast analog point slots и `32` fast slave slots.
4. При конфигурации `20` slave и `200` analog points полный internal sweep выполняется:
   - не более `5` секунд в штатном режиме;
   - не более `8` секунд при недоступности одного slave.
5. Клиент может получить полный fast snapshot не более чем за `5` чтений `FC3`.
6. Fast snapshot не содержит torn reads и корректно валидируется по `SEQ_BEGIN/SEQ_END`.
7. Один проблемный slave не блокирует обновление остальных данных.
8. Legacy `CMD_BASE`, `DIAG`, `TOPOLOGY`, `DIR` логика не деградирует.
9. В HIL/bench сценарии под целевой нагрузкой не наблюдаются систематические `response timeout`, вызванные перегрузкой Modbus TCP server path.
10. Добавлены и документированы diagnostics counters, позволяющие отделить:
   - перегрузку runtime sweep;
   - проблемы RS485;
   - проблемы TCP transport.

## 16. Definition of Done

Работа считается завершённой, если:

- реализована новая карта `MAP_VERSION = 5`;
- runtime публикует fast snapshot из заранее подготовленного cache;
- fast snapshot читается клиентом без тяжёлого legacy `points` polling;
- есть автоматические тесты на packing/layout/publish consistency;
- есть bench или HIL подтверждение работы на нагрузке `20 slave / 200 analog`;
- документация проекта обновлена и описывает новый fast runtime contract.
