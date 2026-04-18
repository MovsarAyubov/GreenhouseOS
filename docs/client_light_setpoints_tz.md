# ТЗ для клиента: отправка lighting setpoints и чтение feedback по Modbus TCP

Date: `2026-03-24`

## 1. Назначение

Клиент работает только с master по `Modbus TCP`.

Задачи клиента:
- передавать уставки освещения в zone-slave через master
- читать обратно опубликованные значения:
  - текущее `DLI`
  - текущий выход света
  - `status bits`

Master сам:
- принимает команду от клиента по `Modbus TCP`
- маршрутизирует ее в zone-slave по `Modbus RTU`
- публикует feedback slave-регистров `134..136` обратно в unified `points window`

## 2. Область действия

Это ТЗ соответствует текущему контракту в репозитории:
- firmware map version: `4`
- topology profile: `one_zone`
- topology generation: `107`
- zone module: `module_id=101`
- zone slave: `slave_id=1`
- command profile: `cmd_profile_id=5001`

Если на master будет загружена другая topology, `cmd_profile_id`, `publish_index` и состав точек могут измениться.

## 3. Базовые параметры соединения

- transport: `Modbus TCP`
- host: IP master-контроллера
- port: `502`
- unit id: `1`
- поддерживаемые функции: `FC3`, `FC6`, `FC16`

Адреса можно использовать:
- либо как zero-based offsets
- либо в `41000`-нотации SCADA

## 4. Окно команд клиента

Command ingress window master:

| Назначение | Offset | SCADA address | Access |
|---|---:|---:|---|
| `TARGET_SLAVE_ID` | `1240` | `42240` | W |
| `TARGET_MODULE_ID` | `1241` | `42241` | W |
| `CMD_PROFILE_ID` | `1242` | `42242` | W |
| `PAYLOAD_LEN` | `1243` | `42243` | W |
| `PAYLOAD[0]` | `1244` | `42244` | W |
| `PAYLOAD[1]` | `1245` | `42245` | W |
| `PAYLOAD[2]` | `1246` | `42246` | W |
| `PAYLOAD[3]` | `1247` | `42247` | W |
| `PAYLOAD[4]` | `1248` | `42248` | W |
| `PAYLOAD[5]` | `1249` | `42249` | W |
| `PAYLOAD[6]` | `1250` | `42250` | W |
| `PAYLOAD[7]` | `1251` | `42251` | W |
| `PAYLOAD[8]` | `1252` | `42252` | W |
| `PAYLOAD[9]` | `1253` | `42253` | W |
| `PAYLOAD[10]` | `1254` | `42254` | W |
| `PAYLOAD[11]` | `1255` | `42255` | W |
| `PAYLOAD[12]` | `1256` | `42256` | W |
| `PAYLOAD[13..15]` | `1257..1259` | `42257..42259` | W |
| `TRIGGER` | `1260` | `42260` | W |
| `LAST_APPLIED_TRIGGER` | `1261` | `42261` | R |
| `RESULT` | `1262` | `42262` | R |
| `IO_ERR` | `1263` | `42263` | R |

## 5. Фиксированные значения для текущей topology

Для отправки lighting setpoints клиент должен всегда задавать:

- `TARGET_SLAVE_ID = 1`
- `TARGET_MODULE_ID = 101`
- `CMD_PROFILE_ID = 5001`
- `PAYLOAD_LEN = 13` для полного обновления

Частичные обновления допустимы, если `PAYLOAD_LEN <= 13`, но для промышленной работы рекомендуется всегда писать полный блок.

## 6. Payload lighting setpoints

Назначение payload-слов:

| Payload word | Назначение | Slave reg |
|---|---|---:|
| `PAYLOAD[0]` | `LIGHT_RELAY_1_ENABLE` | `110` |
| `PAYLOAD[1]` | `LIGHT_RELAY_1_ON_HHMM` | `111` |
| `PAYLOAD[2]` | `LIGHT_RELAY_1_OFF_HHMM` | `112` |
| `PAYLOAD[3]` | `LIGHT_RELAY_1_THRESHOLD_WM2` | `113` |
| `PAYLOAD[4]` | `LIGHT_RELAY_1_RESERVED` | `114` |
| `PAYLOAD[5]` | `LIGHT_RELAY_1_DLI_LIMIT` | `115` |
| `PAYLOAD[6]` | `LIGHT_RELAY_2_ENABLE` | `116` |
| `PAYLOAD[7]` | `LIGHT_RELAY_2_ON_HHMM` | `117` |
| `PAYLOAD[8]` | `LIGHT_RELAY_2_OFF_HHMM` | `118` |
| `PAYLOAD[9]` | `LIGHT_RELAY_2_THRESHOLD_WM2` | `119` |
| `PAYLOAD[10]` | `LIGHT_RELAY_2_RESERVED` | `120` |
| `PAYLOAD[11]` | `LIGHT_RELAY_2_DLI_LIMIT` | `121` |
| `PAYLOAD[12]` | `LIGHT_HYST_SEC` | `122` |

Ограничения:
- `PAYLOAD_LEN` должен быть в диапазоне `1..13`
- `LIGHT_RELAY_1_ENABLE` и `LIGHT_RELAY_2_ENABLE` должны передаваться как `0` или `1`
- `LIGHT_RELAY_1_ON_HHMM`, `LIGHT_RELAY_1_OFF_HHMM`, `LIGHT_RELAY_2_ON_HHMM`, `LIGHT_RELAY_2_OFF_HHMM` должны передаваться в формате `HHMM`
- `LIGHT_RELAY_1_THRESHOLD_WM2` и `LIGHT_RELAY_2_THRESHOLD_WM2` передаются как raw `uint16` в `W/m^2`
- `LIGHT_RELAY_1_RESERVED` и `LIGHT_RELAY_2_RESERVED` должны передаваться как `0`
- `HHMM` должен быть валиден:
  - `0000..2359`
  - минуты `< 60`

Master не накладывает дополнительного масштабирования.
Клиент должен передавать эти значения как raw `uint16` согласно прикладному контракту zone-slave.

Логика на стороне slave:
- если `THRESHOLD_WM2 == 0` или текущая радиация `solar_rad < THRESHOLD_WM2`, relay стремится включиться
- если `solar_rad >= THRESHOLD_WM2`, relay стремится выключиться
- `DLI_LIMIT` по-прежнему имеет приоритет над порогом
- общий временной debounce для обоих relay задается через `LIGHT_HYST_SEC`

## 7. Алгоритм отправки команды

Последовательность должна быть такой:

1. Записать request fields без `TRIGGER`.
2. Записать `TRIGGER` отдельной операцией последним.
3. Опрашивать `LAST_APPLIED_TRIGGER`, `RESULT`, `IO_ERR`.
4. Считать команду завершенной только после совпадения `LAST_APPLIED_TRIGGER == TRIGGER`.

Рекомендуемая реализация для полного обновления:

1. `FC16` запись начиная с `1240`, `17` слов:
   - `TARGET_SLAVE_ID`
   - `TARGET_MODULE_ID`
   - `CMD_PROFILE_ID`
   - `PAYLOAD_LEN`
   - `PAYLOAD[0..12]`
2. `FC6` запись `TRIGGER` в `1260`
3. `FC3` чтение `1261..1263` до завершения

Пример состава `FC16` payload:

```text
[1, 101, 5001, 13, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12]
```

## 8. Частичные обновления

Slave больше не использует отдельный `APPLY_CMD`.

Поведение slave:
- полная запись `110..122` применяется сразу
- частичные изменения применяются после паузы `250 ms` без новых записей
- регистры `114` и `120` зарезервированы, при полном write клиент должен отправлять туда `0`
- из-за fixed `start_reg=110` частичный write затрагивает только префикс `110..(110+PAYLOAD_LEN-1)`
- если нужно изменить поля `relay_2`, клиент должен отправлять полный блок из `13` слов

Важно для клиента:
- `RESULT == 2` означает, что master успешно доставил запись на slave
- при частичной записи это не гарантирует, что локальная логика света уже пересчитана в тот же момент
- если нужен детерминированный результат, использовать только полный `13`-словный write

## 9. Критерий успеха

Команда считается успешно обработанной, если:
- `LAST_APPLIED_TRIGGER == TRIGGER`
- `RESULT == 2`

`IO_ERR` при успешной команде должен быть `0`.

## 10. Коды результата команды

`RESULT`:

| Code | Значение |
|---:|---|
| `0` | `IDLE` |
| `1` | `QUEUED` |
| `2` | `APPLIED` |
| `10` | `REJECT_BOUNDS` |
| `11` | `REJECT_TOPOLOGY` |
| `12` | `REJECT_FC` |
| `13` | `REJECT_BUSY` |
| `14` | `REJECT_PARTIAL` |
| `15` | `TRANSPORT_FAIL` |
| `16` | `ACK_FAIL` |

Интерпретация:
- `QUEUED` не является ошибкой, это промежуточное состояние
- `REJECT_BOUNDS` означает, что клиент отправил `PAYLOAD_LEN > 13`
- `REJECT_BUSY` означает, что предыдущая команда еще не завершена
- `TRANSPORT_FAIL` означает сбой доставки между master и slave
- `ACK_FAIL` означает, что мастер не завершил путь подтверждения команды

## 11. Коды транспортной ошибки

`IO_ERR`:

| Code | Значение |
|---:|---|
| `0` | `NONE` |
| `1` | `TIMEOUT` |
| `2` | `CRC` |
| `3` | `FRAME` |
| `4` | `UART` |

`IO_ERR` значим в первую очередь при `RESULT=15`.

## 12. Требования к trigger

Клиент обязан:
- использовать только ненулевой `TRIGGER`
- использовать новый `TRIGGER` для каждого submit
- писать `TRIGGER` строго последним

Нельзя:
- повторно отправлять тот же `TRIGGER` в расчете на новый submit
- писать runtime-owned поля `1261..1263`

## 13. Рекомендуемые тайминги клиента

Рекомендуемое поведение:
- poll result каждые `100..200 ms`
- общий deadline на одну команду: `2000..3000 ms`

Если в течение deadline:
- `LAST_APPLIED_TRIGGER` не совпал с `TRIGGER`
- или `RESULT` так и остался `QUEUED`

клиент должен считать операцию неуспешной и показывать timeout.

Отдельно учитывать:
- локальная физическая задержка включения на slave считается как `slave_id * 10 s`
- клиент не должен ожидать фактического включения реле сразу после подтверждения записи

## 14. Чтение feedback значений из slave-регистров 134..136

Клиент не читает slave напрямую.

Master публикует эти значения в `points window`.

Для topology `one_zone`:

| Точка | Slave reg | Publish index | Row base offset | SCADA row base |
|---|---:|---:|---:|---:|
| `zone_1.current_dli` | `134` | `18` | `108` | `41108` |
| `zone_1.light_output` | `135` | `19` | `114` | `41114` |
| `zone_1.light_status_bits` | `136` | `20` | `120` | `41120` |

Структура каждой строки points window:

| Row offset | Назначение |
|---:|---|
| `+0` | `VALUE_HI` |
| `+1` | `VALUE_LO` |
| `+2` | `QUALITY` |
| `+3` | `AGE_SEC` |
| `+4` | `MODULE_ID` |
| `+5` | `FLAGS` |

Полные диапазоны:

| Точка | Offsets | SCADA addresses |
|---|---|---|
| `zone_1.current_dli` | `108..113` | `41108..41113` |
| `zone_1.light_output` | `114..119` | `41114..41119` |
| `zone_1.light_status_bits` | `120..125` | `41120..41125` |

## 15. Формат значений в points window

Все значения в points window публикуются как `float32` raw bits в двух регистрах:
- `VALUE_HI`
- `VALUE_LO`

Даже если исходный slave-регистр был `uint16`, клиент должен:

1. Прочитать `VALUE_HI` и `VALUE_LO`.
2. Собрать `uint32`.
3. Интерпретировать этот `uint32` как IEEE754 `float32`.

Дополнительные правила:
- `light_output` после декодирования ожидается как `0`, `50` или `100`
- `light_status_bits` после декодирования нужно приводить к целому типу (`uint16`)
- `current_dli` публикуется как raw value без дополнительного масштабирования на стороне master

## 16. Примечание для topology `two_zones`

Если на master активна `two_zones` topology, меняются только адресация модуля, publish-индексы и generation:
- `zone_1`: `TARGET_SLAVE_ID=1`, `TARGET_MODULE_ID=101`, `CMD_PROFILE_ID=5001`
- `zone_2`: `TARGET_SLAVE_ID=2`, `TARGET_MODULE_ID=102`, `CMD_PROFILE_ID=5003`
- `two_zones` topology generation: `108`
- `zone_1.current_dli` -> `publish_index=27`
- `zone_1.light_output` -> `publish_index=28`
- `zone_1.light_status_bits` -> `publish_index=29`
- `zone_2.current_dli` -> `publish_index=30`
- `zone_2.light_output` -> `publish_index=31`
- `zone_2.light_status_bits` -> `publish_index=32`
