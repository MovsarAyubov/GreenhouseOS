# ТЗ: клиентский transport layer и topology-driven архитектура SCADA

Дата: `2026-03-12`

## 1. Цель

Разработать клиентский `transport layer` и верхние слои доступа к данным/командам так, чтобы клиент:

- стабильно работал с текущей прошивкой контроллера по `Modbus TCP`;
- исключал таймауты, вызванные конкурентным доступом к одному TCP-соединению;
- масштабировался на новые точки телеметрии, новые модули и новые команды без переписывания транспортного слоя;
- строил модель объекта и UI на основе `topology + semantics`, а не на основе hardcoded адресов.

## 2. Основание

Текущая прошивка реализует:

- `Modbus TCP` сервер на порту `502`;
- карту `MAP_VERSION = 4`;
- topology-driven telemetry через unified `points` window;
- generic command ingress через `CMD_BASE`;
- topology metadata и upload pipeline.

Ключевые ограничения текущей прошивки:

- сервер фактически должен считаться `single-flight` по одному TCP connection;
- безопасная модель клиента: не более одного активного Modbus-запроса на одно соединение;
- все новые команды должны строиться поверх topology command profiles;
- telemetry должна читаться через `publish_index`, а не через legacy fixed windows.

Нормативные документы проекта:

- [master_protocol.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\master_protocol.md)
- [topology_config_v2.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\topology_config_v2.md)
- [topology_upload_protocol.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\topology_upload_protocol.md)
- [data_driven_runtime_handover.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\data_driven_runtime_handover.md)
- [conversation_context_2026-03-12.md](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\docs\conversation_context_2026-03-12.md)

## 3. Проблемы, которые должно решить решение

1. Клиент не должен выполнять конкурентные Modbus-запросы через один persistent TCP socket.
2. Клиент не должен смешивать transport, polling, topology resolution и UI-логику.
3. Клиент не должен хранить в UI жёстко заданные `module_id`, `cmd_id`, register addresses и weather blocks.
4. Клиент должен поддерживать расширение:
   - больше датчиков;
   - новые типы модулей;
   - новые workflow-команды;
   - новые уставки и конфигурационные параметры.
5. Клиент должен уметь временно останавливать background polling на время write/poll workflow.

## 4. Область работ

В состав работ входит:

- реализация нового транспортного слоя для `Modbus TCP`;
- реализация очереди запросов и модели `one request in flight`;
- реализация scheduler для чтения telemetry/status/diag;
- реализация topology-driven resolver для `points` и `commands`;
- реализация command executor для многошаговых операций;
- интеграция `localTopology` и `semantics`;
- обновление клиентских сервисов `RTC`, `schedule`, `weather`, `zone telemetry`.

В состав работ не входит:

- изменение протокола прошивки;
- изменение формата `topology_config_v2`;
- расширение register map прошивки сверх текущего `MAP_VERSION=4`.

## 5. Архитектурные требования

Клиент должен быть разделён минимум на следующие слои.

### 5.1 ModbusTransport

Ответственность:

- управление одним persistent TCP connection на контроллер;
- сериализация всех запросов;
- retry/timeout/reconnect;
- логирование request/response;
- единая точка `FC3`, `FC6`, `FC16`.

Требования:

- на одно TCP connection допускается только `1` активный Modbus-запрос;
- все внешние вызовы идут через внутреннюю очередь запросов;
- transport обязан иметь mutex/lock, который делает невозможным параллельный wire access;
- transport обязан поддерживать reconnect после socket error или response timeout;
- transport обязан работать с zero-based адресами как с каноническими;
- `4xxxx`-режим допускается только как внешний compatibility adapter.

### 5.2 RequestQueue

Ответственность:

- постановка Modbus-операций в очередь;
- приоритизация команд над фоновым polling;
- backpressure;
- отмена устаревших polling-задач.

Требования:

- очередь должна различать минимум `priority_high` и `priority_background`;
- `schedule apply`, `RTC set`, `topology upload`, `config apply` всегда ставятся выше telemetry polling;
- повторные фоновые чтения одного и того же ресурса не должны бесконтрольно разрастать очередь;
- очередь должна поддерживать cancellation token для background polling.

### 5.3 PollingScheduler

Ответственность:

- периодически читать:
  - `directory`;
  - `points`;
  - `slave_status`;
  - `diag`;
  - topology metadata;
- уметь ставиться на паузу.

Требования:

- scheduler не должен обращаться к socket напрямую;
- scheduler работает только через `RequestQueue`;
- scheduler должен поддерживать режим `paused` на время write/poll workflows;
- scheduler должен уметь читать `points` chunk'ами;
- scheduler должен поддерживать адаптивное замедление при repeated timeouts.

### 5.4 TopologyStore

Ответственность:

- загрузка и хранение:
  - `localTopology`
  - `semantics`
- сравнение с device topology metadata.

Требования:

- `localTopology` и `semantics` хранятся вне transport;
- store обязан проверять:
  - `generation`
  - `ver_major`
  - `ver_minor`
  - `active_size_bytes`
- store обязан выдавать явный status:
  - `ready`
  - `map_incompatible`
  - `local_topology_missing`
  - `device_topology_inactive`
  - `topology_generation_mismatch`

### 5.5 TopologyResolver

Ответственность:

- поиск `module`, `point`, `command profile`;
- преобразование semantic key -> runtime binding;
- резолвинг `publish_index`, `module_id`, `cmd_id`, `slave_id`.

Требования:

- ни один UI-компонент не должен напрямую использовать адреса карты;
- ни один workflow не должен вручную вводить `module_id/profile_id`;
- все команды и телеметрия должны резолвиться через topology.

### 5.6 CommandExecutor

Ответственность:

- реализация многошаговых операций поверх transport.

Примеры:

- `RTC set`
- `schedule apply`
- будущие `window setpoints`
- будущие `fog setpoints`
- будущие `irrigation parameters`

Требования:

- executor обязан останавливать background polling на время критического workflow;
- executor обязан сериализовать шаги операции;
- executor обязан логировать шаги и результат каждого workflow;
- executor обязан возвращать структурированный result, а не только exception string.

## 6. Логическая модель API клиента

### 6.1 Низкоуровневые операции

Transport обязан предоставить минимум:

- `readHolding(start, qty)`
- `writeSingle(addr, value)`
- `writeMultiple(start, values)`

Требования:

- каждая операция имеет `request_id`;
- каждая операция логирует:
  - timestamp;
  - connection id;
  - transaction id;
  - function code;
  - start addr;
  - qty/value count;
  - duration;
  - result;
- transaction не должен публиковаться наружу как способ синхронизации между слоями.

### 6.2 Среднеуровневые операции

Поверх transport должны быть отдельные сервисы:

- `DirectoryService`
- `PointsService`
- `SlaveStatusService`
- `DiagnosticsService`
- `RtcService`
- `ScheduleService`
- `TopologyUploadService`

### 6.3 Высокоуровневые операции

Поверх topology resolver должны быть workflow-операции:

- `getWeatherSnapshot()`
- `getZoneSnapshot(zoneId)`
- `setRtc(hour, minute)`
- `applySchedule(zoneId, schedulePayload)`
- `setWindowTargets(zoneId, values)`
- `setFogTargets(moduleId, values)`

## 7. Требования к совместимости с текущей прошивкой

Клиент обязан поддерживать:

- `MAP_VERSION = 4`
- `POINT_STRIDE = 6`
- `DIR_BASE = 1264`
- `CMD_BASE = 1240`
- `DIAG_BASE = 1376`
- `TOPO_BASE = 1408`

Точки телеметрии читаются только через unified points window:

- `rowBase = pointsBase + publishIndex * pointStride`

Команды отправляются только через generic command ingress:

- `TARGET_SLAVE_ID`
- `TARGET_MODULE_ID`
- `CMD_PROFILE_ID`
- `PAYLOAD_LEN`
- `PAYLOAD[16]`
- `TRIGGER`

RTC работает только через directory:

- read `+14..15`
- write `+16..18`
- poll `+19..20`

## 8. Требования к масштабированию

Архитектура должна позволять без переписывания transport добавлять:

- новые telemetry points;
- новые weather/zone/fog/irrigation modules;
- новые command profiles;
- новые schedule-like workflows;
- новые уставки;
- новые topology packages.

Расширение должно происходить через:

- новый topology JSON;
- новый semantics JSON;
- новые command builders;
- обновление polling plan.

Transport layer не должен меняться при добавлении:

- новых датчиков;
- новых publish indexes;
- новых module types;
- новых командных payload.

## 9. Требования к будущим функциям

### 9.1 Форточки

Клиент должен поддержать будущие команды уставок форточек:

- не менее `3` отдельных уставок;
- привязка к zone/module через topology;
- отдельный workflow builder;
- передача через generic `CMD_BASE`.

### 9.2 Туманная установка

Клиент должен поддержать:

- команды уставок туманной установки;
- чтение телеметрии модуля тумана;
- построение UI только при наличии topology contract.

### 9.3 Увеличение числа датчиков

Клиент должен:

- динамически строить список доступных точек по topology;
- не предполагать фиксированное количество zone/weather points в коде UI;
- уметь читать telemetry chunk'ами по `publish_index`.

## 10. Конкурентная модель

Обязательная модель выполнения:

- один controller = один `ModbusTransport`;
- один `ModbusTransport` = один active request;
- все внешние запросы идут через очередь;
- polling и command workflows не имеют прямого параллельного доступа к transport.

Запрещено:

- прямой вызов transport из нескольких потоков/тасков без очереди;
- overlap между `schedule write/poll` и background `points/status` reads;
- overlap между `RTC write/poll` и background polling;
- одновременные `topology upload` и обычный telemetry polling.

## 11. Политика паузы polling

Во время следующих операций background polling должен быть поставлен на паузу:

- `RTC set`
- `schedule apply`
- `topology upload`
- `config upload/apply`
- любые будущие multi-step write workflows

После завершения workflow polling должен:

- возобновляться автоматически;
- выполнять принудительный refresh критических окон;
- сбрасывать накопившиеся устаревшие background requests.

## 12. Обработка ошибок

Система должна различать минимум:

- `response_timeout`
- `socket_closed`
- `connection_refused`
- `send_error`
- `malformed_response`
- `exception_response`
- `map_incompatible`
- `topology_generation_mismatch`
- `command_contract_missing`
- `point_contract_missing`

Требования:

- transport error не должен смешиваться с topology error;
- timeout на одном запросе не должен приводить к silent data corruption;
- после timeout transport должен уметь reconnect и повторную инициализацию session state.

## 13. Логирование и диагностика

Клиент обязан логировать:

- connect/disconnect/reconnect;
- request enqueue/dequeue/start/finish;
- timeout/retry/cancel;
- pause/resume polling;
- workflow steps для `RTC`, `schedule`, `topology upload`;
- controller topology status;
- transaction id и request id.

Клиент обязан уметь читать diagnostic counters контроллера:

- `TCP_ACCEPT_ERR_COUNT`
- `TCP_RECV_TIMEOUT_COUNT`
- `TCP_STALE_CLOSE_COUNT`
- `TCP_MALFORMED_MBAP_COUNT`
- `TCP_SEND_ERR_COUNT`
- `TCP_LAST_ERR`

Эти counters должны использоваться в debug UI или diagnostic dump при анализе сетевых проблем.

## 14. Конфигурация клиента

Клиент должен поддерживать конфигурацию:

- `controller_host`
- `controller_port`
- `unit_id`
- `address_mode`
- `request_timeout_ms`
- `retry_count`
- `retry_backoff_ms`
- `poll_period_ms`
- `diag_poll_period_ms`
- `pause_polling_during_write_workflows`
- `local_topology_path`
- `semantics_path`

Рекомендуемые значения по умолчанию:

- `address_mode = zero_based`
- `controller_port = 502`
- `retry_count = 1..2`
- `retry_backoff_ms = 50..100`
- `pause_polling_during_write_workflows = true`

## 15. Требования к данным topology и semantics

### 15.1 Local topology

Клиенту должен передаваться файл:

- [one_zone_one_weather_schedule_topology.json](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\build\topology\one_zone_one_weather_schedule_topology.json)

### 15.2 Semantics

Клиенту должен передаваться файл:

- [one_zone_one_weather_schedule_semantics.json](c:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS\build\topology\one_zone_one_weather_schedule_semantics.json)

### 15.3 Связь topology и semantics

Semantics обязана ссылаться на stable identifiers из topology:

- `module_id`
- `point_id`
- `cmd_id`

UI не должен использовать register addresses как semantic identifiers.

## 16. Требования к UI-интеграции

UI должен работать только через domain services.

UI не должен:

- открывать сокет;
- вызывать `FC3/FC6/FC16` напрямую;
- хранить fixed register addresses;
- хранить ручные `module_id/profile_id` поля в production flow;
- решать, когда можно выполнять параллельный transport access.

UI должен:

- получать telemetry snapshots;
- получать capability flags;
- показывать причину disabled state;
- показывать topology mismatch и transport errors отдельно.

## 17. Этапы реализации

### Этап 1. Transport foundation

- реализовать `ModbusTransport`;
- ввести жесткую сериализацию запросов;
- ввести reconnect/retry/logging;
- убрать прямые обращения разных слоёв к shared socket.

### Этап 2. Polling and services

- реализовать `RequestQueue`;
- реализовать `PollingScheduler`;
- реализовать `DirectoryService`, `PointsService`, `SlaveStatusService`, `DiagnosticsService`.

### Этап 3. Topology integration

- реализовать `TopologyStore`;
- реализовать `TopologyResolver`;
- реализовать bootstrap map/topology validation.

### Этап 4. Workflows

- реализовать `RtcService`;
- реализовать `ScheduleService`;
- ввести pause/resume polling;
- перевести UI на topology-driven команды.

### Этап 5. Scaling

- добавить будущие window/fog workflows;
- ввести расширяемые command builders;
- оптимизировать polling chunk plan.

## 18. Критерии приемки

Решение считается принятым, если выполняются все условия:

1. На одном TCP connection клиент гарантирует не более одного Modbus-запроса одновременно.
2. При background polling и параллельных UI-действиях больше не возникает sporadic `response timeout`, вызванных overlap запросов.
3. `RTC set` выполняется стабильно и не пересекается с polling.
4. `schedule apply` выполняется стабильно и не пересекается с polling.
5. Клиент строит telemetry из `localTopology + semantics`, а не из hardcoded weather/zone windows.
6. Добавление новой точки телеметрии требует изменения topology/semantics, но не transport layer.
7. Добавление новой команды требует нового command builder/profile, но не transport layer.
8. Клиент корректно обрабатывает `topology_generation_mismatch`.
9. Клиент умеет читать diagnostic counters контроллера и выводить их в debug output.
10. В production code отсутствуют direct parallel calls в общий `ModbusTcpClient`.

## 19. Definition of Done

Работа считается завершенной, если:

- transport layer вынесен в отдельный модуль;
- существует очередь запросов и единый worker;
- polling scheduler умеет pausing/resuming;
- `RTC` и `schedule` переведены на workflow executor;
- клиент использует topology и semantics как primary source of truth;
- в логах клиента при типовом цикле работы отсутствуют систематические `response timeout`, вызванные overlap запросов;
- архитектура допускает добавление новых points и commands без изменений transport layer.
