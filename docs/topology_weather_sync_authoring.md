# Памятка по составлению topology для weather sync

Date: `2026-03-22`

## 1. Назначение

Этот документ нужен для сборки новой topology, в которой:
- есть один источник данных метеостанции
- есть один или несколько zone-slave
- master автоматически раздает weather snapshot на zone-slave

Документ описывает только те правила topology, которые реально влияют на работу weather sync в текущей реализации master.

## 2. Ключевой принцип

Weather sync в текущем master реализован не через `commands[]` topology.

Он работает так:
1. master читает weather-module через обычный poll request
2. сохраняет последний weather snapshot
3. после успешного обмена с zone-slave адресно отправляет этот snapshot на slave зоны

Следствие:
- для weather sync не нужно добавлять отдельные command rows в topology
- weather sync не настраивается через `cmd_id`, `payload_offset`, `start_reg` в `commands[]`
- topology должна только правильно описать weather-module и zone-modules в `modules[]` и `requests[]`

## 3. Что master считает zone и weather

В текущей реализации master определяет роли по `module_id` range:

- zone modules: `100..199`
- weather modules: `200..299`

Это важно:
- не только `module_type`, но и сам `module_id` должен попадать в правильный диапазон
- если `module_type=1`, но `module_id` не в диапазоне `100..199`, weather sync на такой модуль не пойдет
- если `module_type=2`, но `module_id` не в диапазоне `200..299`, master не будет считать его weather source

Практическое правило:
- zone controller: `module_type=1`, `module_id=1xx`
- weather station: `module_type=2`, `module_id=2xx`

## 4. Обязательная структура topology

Для рабочего weather sync topology должна содержать:
- минимум один zone module
- минимум один weather module
- минимум один poll request для каждого zone module
- минимум один poll request для weather module

Если в topology нет weather-module:
- weather sync не работает

Если у zone module нет poll request:
- такой slave не будет участвовать в штатном poll cycle
- weather sync на него не будет выполняться

## 5. Требования к weather module

### 5.1. Module row

Рекомендуемая схема:
- `module_id`: `201..299`
- `module_type`: `2`
- `bus_type`: `1`
- `bus_index`: `0`
- `slave_id`: фактический RTU slave id метеостанции
- `zone_id`: `65535`

### 5.2. Request row

Рекомендуемый poll request для weather-module:
- `fc = 3`
- `start_reg = 0`
- `reg_count = 9`
- `period_ms = 5000`
- `timeout_ms = 300`
- `retries = 2`
- `backoff_ms = 20`

Именно эти 9 регистров master ожидает как weather snapshot:
- `OUT_TEMP`
- `OUT_HUM`
- `WIND_SPEED`
- `WIND_DIR`
- `RAIN_FLAG`
- `SOLAR_RAD`
- `BARO_PRESS`
- `DEW_POINT`
- `STATUS_BITS`

Если weather request не дает эти 9 слов:
- snapshot для zone-slave не будет сформирован корректно

Практическое правило:
- делайте один weather request `reg 0..8`
- не дробите weather source на несколько request rows

## 6. Требования к zone module

### 6.1. Module row

Рекомендуемая схема:
- `module_id`: `101..199`
- `module_type`: `1`
- `bus_type`: `1`
- `bus_index`: `0`
- `slave_id`: фактический RTU slave id zone-slave
- `zone_id`: номер зоны

### 6.2. Request row

У каждого zone-slave должен быть минимум один штатный poll request.

Рекомендуемый вариант:
- `fc = 3`
- `start_reg = 0`
- `reg_count = 9`
- `period_ms = 5000`
- `timeout_ms = 300`
- `retries = 2`
- `backoff_ms = 20`

Почему это обязательно:
- weather sync вызывается после успешного poll exchange с zone-slave
- если slave не участвует в poll cycle, master не будет инициировать weather sync на него

## 7. Сколько weather modules можно задавать

Для текущей реализации рекомендуется:
- ровно один weather module в active topology

Причина:
- master выбирает weather source из модулей диапазона `200..299`
- при нескольких weather modules логика выбора источника становится неоднозначной для authoring и сопровождения

Практическое правило:
- один weather module на одну активную topology

Если weather modules несколько:
- authoring считается нестабильным
- поведение будет зависеть от состава и порядка poll plan

## 8. Что не нужно добавлять в topology

Для работы weather sync не нужно:
- отдельные `commands[]` rows для weather fanout
- отдельные `ack_point_id`
- отдельные `schedule`-команды для погоды
- `slave_id=0`
- broadcast-модуль или broadcast-request

Weather sync выполняется внутренней логикой master, а не generic command ingress.

## 9. Рекомендуемые тайминги

Для промышленно приемлемого поведения рекомендуется:

- weather-module `period_ms = 5000`
- zone-module `period_ms = 5000`
- `timeout_ms = 300`
- `retries = 2`
- `backoff_ms = 20`

Практический смысл:
- master получает свежий weather snapshot примерно раз в `5 s`
- zone-slave получают fanout примерно раз в `5 s`
- при одном пропуске следующий успешный цикл быстро восстанавливает доставку

Если требуется более редкий poll:
- не поднимайте period weather-module слишком высоко без причины
- для weather sync лучше держать источник в диапазоне `5..10 s`

## 10. Минимальный рабочий пример

### 10.1. Modules

```json
{
  "modules": [
    {
      "module_id": 101,
      "module_type": 1,
      "bus_type": 1,
      "bus_index": 0,
      "slave_id": 1,
      "zone_id": 1,
      "req_first": 0,
      "req_count": 1,
      "cmd_first": 0,
      "cmd_count": 2
    },
    {
      "module_id": 102,
      "module_type": 1,
      "bus_type": 1,
      "bus_index": 0,
      "slave_id": 2,
      "zone_id": 2,
      "req_first": 1,
      "req_count": 1,
      "cmd_first": 2,
      "cmd_count": 2
    },
    {
      "module_id": 201,
      "module_type": 2,
      "bus_type": 1,
      "bus_index": 0,
      "slave_id": 20,
      "zone_id": 65535,
      "req_first": 2,
      "req_count": 1,
      "cmd_first": 4,
      "cmd_count": 0
    }
  ]
}
```

### 10.2. Requests

```json
{
  "requests": [
    {
      "req_id": 1010,
      "module_id": 101,
      "fc": 3,
      "priority": 1,
      "start_reg": 0,
      "reg_count": 9,
      "period_ms": 5000,
      "timeout_ms": 300,
      "retries": 2,
      "backoff_ms": 20,
      "point_first": 0,
      "point_count": 9,
      "flags": 0
    },
    {
      "req_id": 1020,
      "module_id": 102,
      "fc": 3,
      "priority": 1,
      "start_reg": 0,
      "reg_count": 9,
      "period_ms": 5000,
      "timeout_ms": 300,
      "retries": 2,
      "backoff_ms": 20,
      "point_first": 9,
      "point_count": 9,
      "flags": 0
    },
    {
      "req_id": 2010,
      "module_id": 201,
      "fc": 3,
      "priority": 1,
      "start_reg": 0,
      "reg_count": 9,
      "period_ms": 5000,
      "timeout_ms": 300,
      "retries": 2,
      "backoff_ms": 20,
      "point_first": 18,
      "point_count": 9,
      "flags": 0
    }
  ]
}
```

## 11. Checklist перед сборкой topology

Перед генерацией topology проверьте:

1. Все zone modules имеют `module_id` в диапазоне `100..199`.
2. Все weather modules имеют `module_id` в диапазоне `200..299`.
3. Есть ровно один weather module.
4. У weather module есть `FC3` request на `reg 0..8`.
5. У каждого zone module есть минимум один `FC3` poll request.
6. Все модули weather и zone находятся на `bus_type=1`, если речь про RTU1.
7. `slave_id` уникальны и соответствуют реальным устройствам на линии.
8. `req_first/req_count` у module rows корректно покрывают свои request rows.
9. `cmd_first/cmd_count` не используются для weather sync и не должны вводить в заблуждение.

## 12. Что проверять на стенде

После загрузки новой topology проверьте:

1. Метеостанция стабильно читается мастером.
2. Zone-slave стабильно читаются мастером.
3. После успешного poll weather-module данные fanout доходят до zone-slave.
4. При кратком timeout одного zone-slave остальные зоны продолжают получать weather sync.
5. При восстановлении связи zone-slave снова получает weather snapshot без ручного вмешательства.

## 13. Связанные документы

- `docs/topology_module_contract_v1.md`
- `docs/topology_config_v2.md`
- `docs/modbus_slave_table.md`
- `docs/slave_zone_weather_sync_tz.md`
