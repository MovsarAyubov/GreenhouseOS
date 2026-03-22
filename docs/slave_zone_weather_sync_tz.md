# ТЗ для zone-slave: прием weather sync от master по Modbus RTU

Date: `2026-03-22`

## 1. Назначение

Zone-slave должен принимать от master snapshot данных метеостанции и использовать его в локальной логике зоны.

Цель:
- передавать наружные погодные данные на все zone-slave без broadcast
- иметь подтверждение доставки по каждому slave
- не зависеть от немедленного изменения значений метеостанции
- корректно отмечать свежесть и валидность weather sync без обязательного перехода в safe/autonomous режим

## 2. Транспортный контракт

Master работает по схеме:
- источник weather: отдельный weather-module на RTU
- получатели: zone-slave
- период отправки: примерно `5 s`
- доставка: адресная, по каждому slave отдельно
- подтверждение: через `token/result`

Запись от master:
- `FC16`
- `start_reg = 158`
- `reg_count = 11`

Чтение подтверждения:
- `FC3`
- `start_reg = 169`
- `reg_count = 2`

Отдельный `APPLY_CMD` для weather sync не используется.

## 3. Регистр-карта weather sync

База: `158`

- `158`: `WEATHER_OUT_TEMP` (`int16`, `x0.1 degC`)
- `159`: `WEATHER_OUT_HUM` (`uint16`, `x0.1 %RH`)
- `160`: `WEATHER_WIND_SPEED` (`uint16`, `x0.1 m/s`)
- `161`: `WEATHER_WIND_DIR` (`uint16`, `deg`)
- `162`: `WEATHER_RAIN_FLAG` (`uint16`, `0/1`)
- `163`: `WEATHER_SOLAR_RAD` (`uint16`, `W/m^2`)
- `164`: `WEATHER_BARO_PRESS` (`uint16`, `x0.1 hPa`)
- `165`: `WEATHER_DEW_POINT` (`int16`, `x0.1 degC`)
- `166`: `WEATHER_STATUS_BITS` (`uint16`, bit mask)
- `167`: `WEATHER_AGE_S` (`uint16`, возраст source snapshot на master в секундах)
- `168`: `WEATHER_SET_TOKEN` (`uint16`, новый ненулевой token запускает обработку)
- `169`: `WEATHER_SET_APPLIED_TOKEN` (`uint16`, echo последнего обработанного token)
- `170`: `WEATHER_SET_RESULT` (`uint16`, результат обработки)

## 4. Семантика результата

Master считает успешной обработкой только:
- `2`: `APPLIED`
- `5`: `NOOP`

Рекомендуемые коды на slave:
- `2`: snapshot принят и применен
- `5`: snapshot эквивалентен уже активному, повторное применение не требуется
- `4`: generic failed

Любой код, отличный от `2` и `5`, считается ошибкой на стороне master.

## 5. Требования к логике slave

### 5.1. Общий принцип

Slave не должен применять weather snapshot прямо в Modbus callback.

Нужно:
- быстро принять запись в holding registers
- зафиксировать pending request
- обработать request в отдельном task/control loop
- после завершения выставить `APPLIED_TOKEN/RESULT`

Это тот же подход, который уже используется для RTC sync.

### 5.2. Детекция нового запроса

Новый запрос считается принятым, если:
- `WEATHER_SET_TOKEN != 0`
- token не совпадает с уже обработанным token
- token не совпадает с token, который уже находится в pending

Если token совпал с последним обработанным:
- повторно не применять
- повторно не портить состояние

### 5.3. Что считать данными snapshot

Полезная нагрузка weather snapshot:
- регистры `158..166`

Транспортные метаданные:
- `167`: `WEATHER_AGE_S`
- `168`: `WEATHER_SET_TOKEN`

При определении `NOOP` нужно сравнивать активное состояние с `158..166`.

`WEATHER_AGE_S` не должен сам по себе заставлять slave каждый раз считать пакет новым содержательно, иначе `NOOP` потеряет смысл.

### 5.4. Поведение при новом token

При получении нового token slave должен:
1. Считать `158..168` как единый snapshot.
2. Проверить, что token ненулевой.
3. Скопировать snapshot во внутренний pending-буфер.
4. Вынести применение в control task.
5. После завершения обновить `169..170`.

### 5.5. Применение snapshot

При успешном применении slave должен:
- обновить внутреннее состояние `external_weather`
- обновить время последнего успешного weather sync
- обновить флаг валидности weather snapshot
- выставить:
  - `WEATHER_SET_APPLIED_TOKEN = token`
  - `WEATHER_SET_RESULT = 2`

Если данные эквивалентны уже активным:
- обновить время последнего успешного weather sync
- выставить:
  - `WEATHER_SET_APPLIED_TOKEN = token`
  - `WEATHER_SET_RESULT = 5`

Если произошла ошибка:
- выставить:
  - `WEATHER_SET_APPLIED_TOKEN = token`
  - `WEATHER_SET_RESULT = 4`

## 6. Внутреннее состояние slave

Рекомендуется хранить:
- `last_weather_token`
- `weather_sync_pending`
- `pending_weather_snapshot`
- `active_weather_snapshot`
- `last_weather_rx_ms`
- `weather_valid`
- `weather_stale`

Рекомендуемая структура active snapshot:
- `out_temp`
- `out_hum`
- `wind_speed`
- `wind_dir`
- `rain_flag`
- `solar_rad`
- `baro_press`
- `dew_point`
- `status_bits`
- `source_age_s`

## 7. Требования к stale-логике

Slave должен иметь локальную stale-логику для внешней погоды.

Рекомендуемое поведение:
- если accepted weather sync не было дольше `20..30 s`, считать weather stale
- если `WEATHER_AGE_S` уже пришел слишком большим, это тоже может быть причиной пометки stale

При stale weather:
- нельзя считать наружные погодные данные свежими и достоверными
- slave не должен из-за одного stale weather snapshot сам переводить систему в safe/autonomous режим
- отсутствие свежей погоды не должно интерпретироваться как "условия хорошие"

Обязательное требование:
- stale weather должно отражаться во внутреннем состоянии slave как отдельный флаг/признак свежести

Рекомендуемое поведение:
- master-controlled режим сохраняется, пока master жив и продолжает обмен
- решение о том, как именно использовать stale weather в локальных алгоритмах, принимается отдельно в прикладной логике зоны
- если прикладной алгоритм не умеет надежно работать со stale weather, он должен либо игнорировать weather-входы, либо использовать последнее согласованное поведение, но не инициировать autonomous только по этой причине

## 8. Требования к устойчивости

Slave должен быть устойчив к таким сценариям:
- одинаковый snapshot с новым token
- повторная запись того же token
- потеря одного или нескольких периодов sync
- master reconnect
- длительно неизменные weather values

Обязательные свойства:
- обработка idempotent
- duplicate token не вызывает повторного применения
- одинаковые данные с новым token могут возвращать `NOOP`
- новое успешное подтверждение всегда обновляет freshness

## 9. Ограничения по времени

Master после записи ждет подтверждение достаточно быстро.

Требование к slave:
- `APPLIED_TOKEN/RESULT` должны обновляться без долгой задержки
- обработка weather sync не должна занимать секунды
- целевая реакция: в рамках одного control cycle

Практически:
- если weather sync обрабатывается асинхронно, цикл обработки должен быть существенно меньше периода `5 s`

## 10. Рекомендации по валидации

Не нужно делать слишком агрессивную фильтрацию входных weather данных.

Рекомендуется:
- обязательно валидировать только целостность protocol state
- не отклонять snapshot без крайней необходимости
- спорные или пограничные измерения лучше помечать во внутренней логике как suspect/stale, чем ломать сам sync

Это важно, потому что master передает сырые значения weather-модуля, а не нормализованный "идеальный" набор.

## 11. Ожидаемый псевдоалгоритм

1. Modbus запись обновляет registers `158..168`.
2. Если `token == 0`, ничего не делать.
3. Если token уже applied или pending, игнорировать.
4. Иначе сохранить pending snapshot.
5. Control task берет pending snapshot.
6. Сравнивает `158..166` с active snapshot.
7. Если данные изменились:
   - применяет в `active_weather_snapshot`
   - обновляет freshness
   - пишет `result=2`
8. Если данные не изменились:
   - обновляет freshness
   - пишет `result=5`
9. В обоих случаях пишет `applied_token = token`.

## 12. Приемка

Zone-slave считается соответствующим ТЗ, если:

1. При `FC16(158, 11)` с новым token slave корректно обрабатывает weather snapshot.
2. После обработки slave выставляет `169=token`, `170=2` или `170=5`.
3. Повтор того же token не вызывает повторной обработки.
4. Тот же weather snapshot с новым token допускает `NOOP`.
5. После серии одинаковых snapshot freshness все равно поддерживается.
6. При отсутствии новых weather sync дольше порога snapshot помечается stale.
7. stale weather не переводит slave в safe/autonomous режим само по себе.
8. master-controlled режим сохраняется, пока master продолжает штатный обмен.
9. Обработка weather sync не блокирует Modbus stack и не требует долгих задержек в callback.

## 13. Совместимость

Это ТЗ соответствует текущей реализации master-side weather sync:
- source weather module определяется по `module_id 200..299`
- zone modules определяются по `module_id 100..199`
- отправка идет адресно, не broadcast
- cadence примерно `5 s`
- подтверждение требуется по `token/result`
