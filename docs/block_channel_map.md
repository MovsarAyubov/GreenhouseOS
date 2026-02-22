# Block Channel Map (Master side)

Источник: `Core/Src/main.c`
Источник для ПК во время работы: `BLOCK_LAYOUT_RESP` (msg_type `16`).

## Каналы одного блока
- 0 `AIR_TEMP`
- 1 `AIR_HUM`
- 2 `WATER_RAIL`
- 3 `WATER_GROW`
- 4 `WATER_UNDERTRAY`
- 5 `WATER_UPPER_HEAT`
- 6 `WINDOWS_POS_A`
- 7 `WINDOWS_POS_B`
- 8 `CURTAIN_POS`

## Расчет индекса в snapshot
- `sensor_id = block_index * channels_per_block + channel_index`
- `block_no = block_index + 1`

## Текущая конфигурация мастера
Таблица `kModbusMap`:
- `{slave_id=1, block_no=1, start_reg=0, sensor_count=3, sensor_base=0}`

Это означает:
- значения slave 1 регистров 0..2 попадают в `sensor_id 0..2`:
  - `0` -> Block1 AIR_TEMP
  - `1` -> Block1 AIR_HUM
  - `2` -> Block1 WATER_RAIL
- `sensor_id 3..8` (каналы блока 1, которых пока нет) помечаются `OFFLINE`.

## Как расширять
При добавлении нового блока добавляется строка в `kModbusMap`.
Пример:
- `{2U, 2U, 0U, 6U, 6U}`

Значит блок 2 читает 6 каналов и пишет в `sensor_id 6..11`.
