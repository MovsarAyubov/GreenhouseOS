# ТЗ на доработку операторского ПК (модель по блокам)

## 1. Цель
Перейти от отображения "150 обезличенных датчиков" к понятной модели:
- каждый слейв = отдельный блок теплицы,
- в блоке фиксированный набор каналов,
- UI показывает значения в терминах блока.

## 2. Модель данных

### 2.1. Каналы блока (фиксированный порядок)
- `0 AIR_TEMP` — температура воздуха блока, C
- `1 AIR_HUM` — влажность воздуха блока, %
- `2 WATER_RAIL` — температура воды "труба рельса", C
- `3 WATER_GROW` — температура воды "труба роста", C
- `4 WATER_UNDERTRAY` — температура воды "подлотковая труба", C
- `5 WATER_UPPER_HEAT` — температура воды "верхний обогрев", C
- `6 WINDOWS_POS_A` — положение форточек, группа A, %
- `7 WINDOWS_POS_B` — положение форточек, группа B, %
- `8 CURTAIN_POS` — положение штор, %

### 2.2. Источник карты блоков (обязательно)
Карта блоков не хардкодится в ПК. После подключения ПК должен:
1. отправить `GET_BLOCK_LAYOUT_REQ`,
2. получить `BLOCK_LAYOUT_RESP`,
3. построить отображение блоков по `items[]`.

`BLOCK_LAYOUT_RESP`:
- `channels_per_block:uint8`
- `item_count:uint8`
- `items[12]`, элемент:
  - `block_no`
  - `slave_id`
  - `start_reg`
  - `sensor_count`
  - `sensor_base`

Правило индексации датчика:
- `sensor_id = sensor_base + channel_index`, где `channel_index` в диапазоне `0..sensor_count-1`.

### 2.3. Текущее состояние объекта
- активен 1 блок (slave 1)
- активны 3 канала (`AIR_TEMP`, `AIR_HUM`, `WATER_RAIL`)
- остальные каналы блока должны отображаться как `OFFLINE/Н/Д`

## 3. Требования к UI

### 3.1. Экран "Блоки"
Для каждого блока карточка/строка:
- статус связи блока,
- `Air Temp`, `Air Hum`, `Water Rail`, `Water Grow`, `Water Undertray`,
- `Water Upper Heat`, `Windows Pos A`, `Windows Pos B`, `Curtain Pos`,
- время последнего обновления,
- индикатор качества (`OK/STALE/FAULT/OFFLINE`).
- список блоков формируется из `BLOCK_LAYOUT_RESP`, а не из фиксированного числа.

### 3.2. Экран "Датчики"
- оставить таблицу низкого уровня,
- добавить колонки: `Block`, `ChannelName`, `Quality`,
- сортировка/фильтр по блоку и каналу.

### 3.3. События/аварии
- поле `source` декодировать через текущую карту `BLOCK_LAYOUT_RESP` в `Блок N / Канал X`,
- если source не попал в карту — показывать `Unknown source`.

## 4. Логирование

### 4.1. Таблица telemetry
Оставить текущий формат, добавить вычисляемые поля при выводе/экспорте:
- `block_no = sensor_id / channels_per_block + 1`
- `channel_index = sensor_id % channels_per_block`
- `channel_name` по словарю каналов.

### 4.2. Экспорт
В экспорт добавлять колонки:
- `BlockNo`, `ChannelName`, `Value`, `Quality`, `TimeUtc`.

## 5. Сетевой клиент
- `SNAPSHOT` остается прежним (`values[150]`, `quality[150]`),
- добавить обработку `GET_BLOCK_LAYOUT_REQ/BLOCK_LAYOUT_RESP`,
- при reconnect карта блоков запрашивается повторно (resync).

## 6. Валидация и приемка
1. При подключении и получении snapshot:
- блок 1 показывает 3 реальных значения,
- неактивные каналы блока 1 (до `channels_per_block`) = `OFFLINE`.
2. При добавлении блока 2 в мастере:
- UI без перекомпиляции начинает показывать блок 2 после нового `BLOCK_LAYOUT_RESP`.
3. На ленте событий `source` отображается как `Блок/Канал`.

## 7. Нефункциональные
- отрисовка не блокирует UI,
- обновление каждые 5 секунд,
- reconnect/resync без потери отображения структуры блоков.

## 8. Пример функции декодирования для ПК (C#)
```csharp
public sealed class BlockLayoutItem
{
    public byte BlockNo { get; init; }
    public byte SlaveId { get; init; }
    public ushort StartReg { get; init; }
    public ushort SensorCount { get; init; }
    public ushort SensorBase { get; init; }
}

public sealed class SensorDecoded
{
    public bool Found { get; init; }
    public int BlockNo { get; init; }
    public int ChannelIndex { get; init; }
    public string ChannelName { get; init; } = "Unknown";
}

public static class SensorDecoder
{
    private static readonly string[] ChannelNames =
    {
        "AIR_TEMP",
        "AIR_HUM",
        "WATER_RAIL",
        "WATER_GROW",
        "WATER_UNDERTRAY",
        "WATER_UPPER_HEAT",
        "WINDOWS_POS_A",
        "WINDOWS_POS_B",
        "CURTAIN_POS"
    };

    // sensorId -> Block/Channel по текущему BLOCK_LAYOUT_RESP
    public static SensorDecoded DecodeSensorId(
        int sensorId,
        IReadOnlyList<BlockLayoutItem> layoutItems)
    {
        if (sensorId < 0 || layoutItems == null)
        {
            return new SensorDecoded { Found = false };
        }

        foreach (var item in layoutItems)
        {
            var first = item.SensorBase;
            var last = item.SensorBase + item.SensorCount - 1;
            if (item.SensorCount == 0) continue;

            if (sensorId >= first && sensorId <= last)
            {
                var channelIndex = sensorId - first;
                var channelName = channelIndex < ChannelNames.Length
                    ? ChannelNames[channelIndex]
                    : $"CH_{channelIndex}";

                return new SensorDecoded
                {
                    Found = true,
                    BlockNo = item.BlockNo,
                    ChannelIndex = channelIndex,
                    ChannelName = channelName
                };
            }
        }

        return new SensorDecoded { Found = false };
    }
}
```

## 9. Учет автономного режима слейвов (v2)
- У слейвов нет прямой связи с ПК; обмен только через мастер.
- Если мастер недоступен, ПК не может получать онлайн-статус слейвов.
- После восстановления мастера ПК должен запрашивать resync и обновлять:
  - текущие значения каналов,
  - карту блоков (`BLOCK_LAYOUT_RESP`),
  - доступные диагностические статусы слейвов (если мастер их проксирует).

Рекомендация для UI:
- для блока показывать источник управления:
  - `REMOTE` (по мастеру),
  - `AUTONOMOUS` (локально на слейве),
  если этот статус доступен через мастер.
