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

## 8. Расписания света
### 8.1. REMOTE (по мастеру)
- Хранится до 4 расписаний
- Может быть активно несколько расписаний одновременно
- Логика включения света: OR по активным расписаниям
  - Свет включен, если текущее время попадает хотя бы в один активный интервал

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

REMOTE расписания света (до 4):
- 110: SCH0_ENABLE
- 111: SCH0_ON_HHMM
- 112: SCH0_OFF_HHMM
- 113: SCH1_ENABLE
- 114: SCH1_ON_HHMM
- 115: SCH1_OFF_HHMM
- 116: SCH2_ENABLE
- 117: SCH2_ON_HHMM
- 118: SCH2_OFF_HHMM
- 119: SCH3_ENABLE
- 120: SCH3_ON_HHMM
- 121: SCH3_OFF_HHMM

Применение:
- 122: APPLY_CMD
- 123-124: CTRL_CRC
- 125: APPLY_STATUS
- 126-127: ACTIVE_CTRL_VERSION

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
6. В REMOTE поддерживаются до 4 расписаний с одновременной активацией
7. Интервалы через полночь (например 20:00-01:00) обрабатываются корректно
