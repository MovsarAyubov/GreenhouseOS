# ТЗ для прошивок слейвов (ESP32): Modbus, автономный режим, расписания

## 1. Назначение
Слейв обслуживает один блок теплицы и отдает мастеру актуальные показания датчиков по RS485/Modbus RTU.

## 2. Сеть и протокол
- Физика: RS485
- Протокол: Modbus RTU
- Чтение мастером: функция 0x03 (Holding Registers)
- Запись мастером уставок/команд: функция 0x10 (Write Multiple Registers), при необходимости 0x06
- UART: 9600, 8N1
- Каждый слейв имеет уникальный slave_id

## 3. Модель
- 1 слейв = 1 блок
- Мастер читает телеметрию из фиксированного диапазона регистров
- Слейв локально обновляет регистры датчиков по своим таймерам

## 4. Карта регистров телеметрии (обязательная)
Базовый диапазон: start_register + 0..8 (9 каналов)

1. reg+0: AIR_TEMP
2. reg+1: AIR_HUM
3. reg+2: WATER_RAIL
4. reg+3: WATER_GROW
5. reg+4: WATER_UNDERTRAY
6. reg+5: WATER_UPPER_HEAT
7. reg+6: WINDOWS_POS_A
8. reg+7: WINDOWS_POS_B
9. reg+8: CURTAIN_POS

Формат значений:
- int16 x10 (fixed point)
- Пример: 25.3 C -> 253, 60.0% -> 600

## 5. Частоты локального опроса датчиков (на слейве)
- AIR_TEMP, AIR_HUM: 1 раз в 5 сек
- WATER_RAIL, WATER_GROW, WATER_UNDERTRAY, WATER_UPPER_HEAT: 1 раз в 5 сек
- WINDOWS_POS_A, WINDOWS_POS_B, CURTAIN_POS: каждые 200 мс

Важно: 200 мс относится к внутреннему контуру слейва, а не к частоте Modbus-опроса мастером.

## 6. Контур позиционирования
- Мастер задает целевую позицию (например, 70%)
- Слейв управляет мотором локально до достижения цели по позициономеру
- Концевые выключатели присутствуют

## 7. Автономный режим
### 7.1. Детекция отказа мастера
- Отдельный heartbeat-кадр не обязателен
- Любой валидный Modbus-запрос от мастера считается heartbeat
- Если валидных запросов нет 30 секунд -> переход в AUTONOMOUS

### 7.2. Поведение в AUTONOMOUS
- На входе: форточки закрываются полностью
- Дальше слейв управляет локально:
  - отопление
  - форточки/шторы
  - освещение
- Локальные автономные уставки и автономное расписание:
  - зашиты в прошивку
  - мастером не изменяются

### 7.3. Возврат в REMOTE
- Не мгновенно
- Условие: 3 подряд успешных цикла связи с мастером
- После возврата слейв работает только по уставкам мастера

## 8. Управление светом
### 8.1. REMOTE (по мастеру)
- Хранятся два независимых remote-канала света, по одному на каждый relay
- Для каждого relay мастер задает свои `ENABLE/ON/OFF/THRESHOLD_WM2/DLI_LIMIT`
- Общий `LIGHT_HYST_SEC` задается через регистр `122`
- Фактическое решение по свету slave принимает локально с учетом weather-данных, в том числе мгновенной радиации из `163`
- Логика по порогу бинарная:
  - если `THRESHOLD_WM2 == 0` или `solar_rad < THRESHOLD_WM2`, relay стремится включиться
  - если `solar_rad >= THRESHOLD_WM2`, relay стремится выключиться
  - `DLI_LIMIT` по-прежнему имеет приоритет над порогом

### 8.2. AUTONOMOUS
- Используется отдельное фиксированное автономное расписание
- Автономное расписание не редактируется мастером

### 8.3. Интервалы через полночь
Поддерживаются.
- Если start < end: обычный интервал в пределах суток
- Если start > end: интервал через полночь
  - активен от start до 23:59:59 и от 00:00 до end
- Пример: 20:00-01:00 поддерживается как один интервал

### 8.4. Дни недели
- Расписания одинаковы для всех дней недели
- Разделения по дням нет

## 9. Регистр-карта управления (R/W, рекомендуемая)
Пример базы: CTRL_BASE=100

- 100-101: CTRL_VERSION (u32 split)
- 102: MODE_CMD (0=REMOTE, 1=AUTONOMOUS_FORCED [опц.])
- 103: WINDOWS_POS_A_TARGET (x10 %)
- 104: WINDOWS_POS_B_TARGET (x10 %)
- 105: CURTAIN_POS_TARGET (x10 %)
- 106: SP_WATER_RAIL (x10 C)
- 107: SP_WATER_GROW (x10 C)
- 108: SP_WATER_UPPER (x10 C)
- 109: SP_WATER_UNDERTRAY (x10 C)

REMOTE lighting setpoints (master -> slave, two independent relay channels):
- 110: LIGHT_RELAY_1_ENABLE
- 111: LIGHT_RELAY_1_ON_HHMM
- 112: LIGHT_RELAY_1_OFF_HHMM
- 113: LIGHT_RELAY_1_THRESHOLD_WM2
- 114: LIGHT_RELAY_1_RESERVED
- 115: LIGHT_RELAY_1_DLI_LIMIT
- 116: LIGHT_RELAY_2_ENABLE
- 117: LIGHT_RELAY_2_ON_HHMM
- 118: LIGHT_RELAY_2_OFF_HHMM
- 119: LIGHT_RELAY_2_THRESHOLD_WM2
- 120: LIGHT_RELAY_2_RESERVED
- 121: LIGHT_RELAY_2_DLI_LIMIT
- 122: LIGHT_HYST_SEC

Текущее lighting state:
- 134: CURRENT_DLI_FROM_MASTER
- 135: CURRENT_LIGHT_OUTPUT (`0/50/100`)
- 136: LIGHT_STATUS_BITS

Для этого lighting profile отдельный `APPLY_CMD` не используется.
Рекомендуемый режим для мастера: писать полный блок `110..122` одной `FC16` транзакцией.

Ожидаемое поведение slave:
- полная запись `110..122` должна применяться сразу
- частичная запись `110..122` допустима, но применяется только после паузы `250 ms` без новых записей
- регистры `114` и `120` зарезервированы и должны игнорироваться slave; master рекомендует писать туда `0`
- физическая задержка включения света считается локально как `slave_id * 10 s`
- master не должен пытаться компенсировать эту задержку на своей стороне

RTC sync (master -> slave):
- 140: RTC_SET_HOUR (0..23)
- 141: RTC_SET_MINUTE (0..59)
- 142: RTC_SET_TOKEN (new non-zero token triggers processing)
- 143: RTC_SET_APPLIED_TOKEN (echo token after processing)
- 144: RTC_SET_RESULT
  - 2: APPLIED (slave RTC updated)
  - 5: NOOP (drift below threshold, update skipped)
  - any other value: treated as failed sync on master side

Weather sync (master -> zone slave):
- 158: WEATHER_OUT_TEMP (`int16`, x0.1 degC)
- 159: WEATHER_OUT_HUM (`uint16`, x0.1 %RH)
- 160: WEATHER_WIND_SPEED (`uint16`, x0.1 m/s)
- 161: WEATHER_WIND_DIR (`uint16`, deg)
- 162: WEATHER_RAIN_FLAG (`uint16`, 0/1)
- 163: WEATHER_SOLAR_RAD (`uint16`, W/m^2)
- 164: WEATHER_BARO_PRESS (`uint16`, x0.1 hPa)
- 165: WEATHER_DEW_POINT (`int16`, x0.1 degC)
- 166: WEATHER_STATUS_BITS (`uint16`, bit mask)
- 167: WEATHER_AGE_S (age of source snapshot on master, seconds)
- 168: WEATHER_SET_TOKEN (new non-zero token triggers processing)
- 169: WEATHER_SET_APPLIED_TOKEN (echo token after processing)
- 170: WEATHER_SET_RESULT
  - 2: APPLIED (weather snapshot accepted)
  - 5: NOOP (snapshot equivalent to active one)
  - any other value: treated as failed sync on master side

Ожидаемая частота:
- мастер рассылает weather snapshot примерно раз в 5-10 секунд
- slave должен считать данные weather stale при длительном отсутствии новых подтвержденных обновлений

## 10. Приемка
1. Мастер стабильно читает reg+0..8
2. Масштаб x10 корректен
3. Позиционные каналы обновляются локально с шагом 200 мс
4. При отсутствии запросов мастера 30 сек слейв переходит в AUTONOMOUS
5. После 3 успешных циклов связи слейв возвращается в REMOTE
6. В REMOTE поддерживаются два независимых relay-канала света с уставками `110..122`
7. Интервалы через полночь (например 20:00-01:00) обрабатываются корректно
