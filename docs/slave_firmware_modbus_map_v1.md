# ТЗ на прошивку слейвов: Modbus RTU map v1

Дата: 2026-06-25.

Документ задает новый контракт Modbus RTU для слейвов GreenhouseOS. Он заменяет старую схему, где телеметрия зоны начиналась с регистров `0..8`. В новой схеме регистры `0..99` зарезервированы под общий паспорт, статус и диагностику любого слейва.

Старые адреса `0..336` не поддерживаются как алиасы.

## 1. Назначение

Слейв является автономным контроллером своего узла теплицы. Он локально читает датчики, хранит уставки, принимает решения управления исполнительными устройствами и отдает мастеру состояние по Modbus RTU.

Мастер/хаб на Linux:

- является единственным Modbus RTU master на RS485;
- сканирует слейвы;
- читает телеметрию;
- записывает уставки и команды от SCADA;
- не содержит климатическую логику конкретного слейва;
- выбирает карту регистров по `device_type + modbus_map_version`.

SCADA не работает с RS485 напрямую.

## 2. Протокол

- Физический уровень: RS485.
- Протокол: Modbus RTU.
- Формат UART: `8N1`.
- Baudrate задается конфигурацией объекта; рекомендуемый стартовый вариант: `19200`.
- Обязательное чтение: `FC03 Read Holding Registers`.
- Обязательная запись блока: `FC16 Write Multiple Registers`.
- Допустимая одиночная запись: `FC06 Write Single Register`.
- Адреса регистров в документе указаны как zero-based holding register address.
- Каждый слейв имеет уникальный `slave_id` на шине.

Любой валидный Modbus-запрос от мастера считается heartbeat.

## 3. Подход к картам

Используется подход 1: жесткий каталог карт в хабе.

Слейв не передает мастеру описание всех своих точек. Слейв передает только:

- `device_type`;
- `firmware_version`;
- `modbus_map_version`;
- `zone_id`;
- `capability_low/high`.

Хаб содержит локальный каталог:

```text
slave_maps/
  zone_v1.json
  irrigation_v1.json
  fog_v1.json
  boiler_v1.json
  osmosis_v1.json
```

Алгоритм мастера:

1. Прочитать у адреса `0..15`.
2. Получить `device_type` и `modbus_map_version`.
3. Найти карту в своем каталоге.
4. Проверить capabilities.
5. Читать/писать регистры согласно выбранной карте.

Если карта неизвестна, хаб помечает устройство как unsupported и не выполняет рабочие команды.

## 4. Типы слейвов

| device_type | Name |
|---:|---|
| 1 | Zone Slave |
| 2 | Irrigation Slave |
| 3 | Fog Slave |
| 4 | Boiler Slave |
| 5 | Osmosis Slave |

## 5. Общая карта всех слейвов

Диапазон `0..99` одинаковый для всех типов устройств.

| Reg | Name | Type | Access | Description |
|---:|---|---|---|---|
| 0 | `device_type` | u16 | R | Тип слейва |
| 1 | `firmware_version` | u16 | R | `major << 8 | minor`, например `0x0102` |
| 2 | `modbus_map_version` | u16 | R | Для этого документа: `1` |
| 3 | `zone_id` | u16 | R | Для Zone Slave v1: текущий `slave_id`; для общих устройств `65535` |
| 4 | `capability_low` | u16 | R | Bits `0..15` |
| 5 | `capability_high` | u16 | R | Bits `16..31` |
| 6 | `status_bits` | u16 | R | Общий статус |
| 7 | `fault_code` | u16 | R | Главная ошибка, `0 = OK` |
| 8 | `uptime_hi` | u16 | R | Uptime seconds, high word |
| 9 | `uptime_lo` | u16 | R | Uptime seconds, low word |
| 10 | `last_master_age_s` | u16 | R | Секунд с последнего валидного запроса мастера |
| 11 | `restart_counter` | u16 | R | Счетчик перезапусков |
| 12 | `config_version` | u16 | R | Версия локальной конфигурации слейва |
| 13 | `serial_hi` | u16 | R | Серийный номер, high word |
| 14 | `serial_lo` | u16 | R | Серийный номер, low word |
| 15 | `identity_crc` | u16 | R | CRC/контрольная сумма identity, можно `0` в v1 |
| 16..31 | reserved | u16 | R | Должны возвращать `0` |
| 32 | `diag_timeout_count` | u16 | R | Таймауты/потери связи на стороне слейва |
| 33 | `diag_crc_error_count` | u16 | R | Ошибки CRC RX |
| 34 | `diag_exception_count` | u16 | R | Количество Modbus exception |
| 35 | `diag_last_exception` | u16 | R | Последний exception code |
| 36 | `diag_rx_count_hi` | u16 | R | Валидные запросы, high word |
| 37 | `diag_rx_count_lo` | u16 | R | Валидные запросы, low word |
| 38 | `diag_tx_count_hi` | u16 | R | Ответы, high word |
| 39 | `diag_tx_count_lo` | u16 | R | Ответы, low word |
| 40..99 | reserved | u16 | R | Должны возвращать `0` |

### 5.1 status_bits

| Bit | Meaning |
|---:|---|
| 0 | ready |
| 1 | remote mode active |
| 2 | autonomous mode active |
| 3 | manual mode active |
| 4 | emergency mode active |
| 5 | has active fault |
| 6 | sensor fault present |
| 7 | actuator fault present |
| 8 | master timeout detected |
| 9 | configuration invalid |
| 10 | maintenance required |
| 11 | safe state active |
| 12..15 | reserved |

### 5.2 command_result

Все типовые команды используют одинаковые коды результата.

| Code | Name |
|---:|---|
| 0 | `IDLE` |
| 1 | `QUEUED` |
| 2 | `APPLIED` |
| 3 | `REJECT_RANGE` |
| 4 | `REJECT_MODE` |
| 5 | `NOOP` |
| 6 | `REJECT_TOKEN` |
| 7 | `BUSY` |
| 10 | `FAULT` |

## 6. Общая структура диапазонов

| Range | Purpose |
|---:|---|
| `0..99` | Common identity/status/diagnostics |
| `100..399` | Reserved/common service |
| `400..699` | Telemetry/status by device type |
| `700..999` | Calculated state/statistics |
| `1000..1299` | Setpoints/commands from hub |
| `1300..1599` | Schedules/profiles |
| `1600..1799` | Shared external data |
| `1800..1999` | Advanced diagnostics/debug |

Слейв должен отвечать нулями на зарезервированные регистры внутри поддерживаемого диапазона. Чтение вне реализованной карты может возвращать Modbus exception `0x02 Illegal Data Address`.

## 7. Zone Slave map v1

`device_type = 1`, `modbus_map_version = 1`.

### 7.1 Capabilities

| Bit | Meaning |
|---:|---|
| 0 | air temperature |
| 1 | air humidity |
| 2 | CO2 |
| 3 | water rail temperature |
| 4 | water grow temperature |
| 5 | water undertray temperature |
| 6 | water upper heat temperature |
| 7 | windows group A |
| 8 | windows group B |
| 9 | curtain |
| 10 | heating output |
| 11 | lighting output |
| 12 | weather sync input |
| 13 | autonomous mode |
| 14 | manual mode |
| 15 | emergency stop |

### 7.2 Telemetry

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 400 | `air_temp` | s16 x0.1 degC | R |
| 401 | `air_humidity` | u16 x0.1 %RH | R |
| 402 | `co2` | u16 ppm | R |
| 403 | `water_rail_temp` | s16 x0.1 degC | R |
| 404 | `water_grow_temp` | s16 x0.1 degC | R |
| 405 | `water_undertray_temp` | s16 x0.1 degC | R |
| 406 | `water_upper_heat_temp` | s16 x0.1 degC | R |
| 407 | `windows_a_position` | u16 x0.1 % | R |
| 408 | `windows_b_position` | u16 x0.1 % | R |
| 409 | `curtain_position` | u16 x0.1 % | R |
| 410 | `heating_output` | u16 x0.1 % | R |
| 411 | `light_output` | u16 x0.1 % | R |
| 412 | `reserved` | u16 | R |
| 413 | `vpd` | u16 x0.01 kPa | R |
| 414 | `current_dli_hi` | u16 | R |
| 415 | `current_dli_lo` | u16 | R |
| 416 | `zone_state_bits` | u16 | R |
| 417 | `sensor_fault_bits` | u16 | R |
| 418 | `actuator_fault_bits` | u16 | R |
| 419 | `reserved` | u16 | R |

### 7.3 Setpoints and commands

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1000 | `control_mode` | u16 | R/W |
| 1001 | `air_temp_day_setpoint` | s16 x0.1 degC | R/W |
| 1002 | `air_temp_night_setpoint` | s16 x0.1 degC | R/W |
| 1003 | `humidity_setpoint` | u16 x0.1 %RH | R/W |
| 1004 | `co2_setpoint` | u16 ppm | R/W |
| 1005 | `windows_a_target` | u16 x0.1 % | R/W |
| 1006 | `windows_b_target` | u16 x0.1 % | R/W |
| 1007 | `curtain_target` | u16 x0.1 % | R/W |
| 1008 | `heating_target` | u16 x0.1 % | R/W |
| 1009 | `lighting_enable` | u16 0/1 | R/W |
| 1010 | `fog_enable` | u16 0/1 | R/W |
| 1011 | `irrigation_enable` | u16 0/1 | R/W |
| 1012 | `manual_command` | u16 | R/W |
| 1013 | `command_arg_1` | u16 | R/W |
| 1014 | `command_arg_2` | u16 | R/W |
| 1015 | `command_token` | u16 | R/W |
| 1016 | `applied_token` | u16 | R |
| 1017 | `command_result` | u16 | R |

`command_token` запускает обработку блока `1000..1014`, если значение новое и не равно `0`.

Расширения текущей Zone Slave прошивки не являются алиасами старых адресов
`0..336`. Старый документ использован только как источник семантики.
Legacy-регистры из старой карты не публикуются в v1.

#### Water setpoints, `1020..1023`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1020 | `water_rail_setpoint` | s16 x0.1 degC | R/W |
| 1021 | `water_grow_setpoint` | s16 x0.1 degC | R/W |
| 1022 | `water_upper_setpoint` | s16 x0.1 degC | R/W |
| 1023 | `water_undertray_setpoint` | s16 x0.1 degC | R/W |

#### Window control/runtime, `1030..1083`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1030 | `windows_ctrl_mode` | u16, `0=AUTO`, `1=MANUAL` | R/W |
| 1031 | `windows_force_safe_cmd` | u16 | R/W |
| 1032 | `windows_temp_setpoint` | s16 x0.1 degC | R/W |
| 1033 | `windows_safe_min_percent` | u16 x0.1 % | R/W |
| 1035 | `windows_wind_storm` | u16 x0.1 m/s | R/W |
| 1036 | `windows_wind_recover` | u16 x0.1 m/s | R/W |
| 1037 | `window_a_azimuth_deg` | u16 deg | R/W |
| 1038 | `windows_wind_sector_half_width_deg` | u16 deg | R/W |
| 1039 | `windows_temp_step_c` | s16 x0.1 degC | R/W |
| 1040 | `windows_temp_step_hyst_c` | s16 x0.1 degC | R/W |
| 1041 | `rll400_target_hyst_percent` | u16 x0.1 % | R/W |
| 1042 | `actuator_motion_delta_percent` | u16 x0.1 % | R/W |
| 1043 | `actuator_no_motion_timeout_ms` | u16 ms, `0` disables no-motion fault | R/W |
| 1044 | `window_a_fault_reset_token` | u16 | R/W |
| 1045 | `window_b_fault_reset_token` | u16 | R/W |
| 1046 | `windows_status_bits` | u16 | R |
| 1047 | `window_a_status_bits` | u16 | R |
| 1048 | `window_b_status_bits` | u16 | R |
| 1049 | `window_a_fault_code` | u16 | R |
| 1050 | `window_b_fault_code` | u16 | R |
| 1051 | `air_temp_sensor_status` | u16 | R |
| 1052 | `window_a_local_manual_active` | u16 | R |
| 1053 | `window_b_local_manual_active` | u16 | R |
| 1054 | `windows_auto_algo_mode` | u16, `0=TEMP`, `1=HUMIDITY` | R/W |
| 1055 | `windows_hum_setpoint` | u16 x0.1 %RH | R/W |
| 1056 | `windows_hum_step` | u16 x0.1 %RH | R/W |
| 1057 | `windows_hum_step_hyst` | u16 x0.1 %RH | R/W |
| 1058 | `windows_cold_close_delta` | s16 x0.1 degC | R/W |
| 1059 | `windows_cold_close_hyst` | s16 x0.1 degC | R/W |
| 1060 | `windows_windward_min_percent` | u16 x0.1 % | R/W |
| 1061 | `windows_windward_max_percent` | u16 x0.1 % | R/W |
| 1062 | `windows_windward_speed_threshold` | u16 x0.1 m/s | R/W |
| 1063 | `windows_windward_reduction_percent_per_ms` | u16 x0.1 %/m/s | R/W |
| 1064 | `windows_leeward_min_percent` | u16 x0.1 % | R/W |
| 1065 | `windows_leeward_max_percent` | u16 x0.1 % | R/W |
| 1066 | `windows_leeward_speed_threshold` | u16 x0.1 m/s | R/W |
| 1067 | `windows_leeward_reduction_percent_per_ms` | u16 x0.1 %/m/s | R/W |
| 1068 | `windows_windward_lag_percent` | u16 x0.1 % | R/W |
| 1069 | `windows_rain_mode` | u16, `0=OFF`, `1=WINDWARD` | R/W |
| 1070 | `windows_rain_windward_percent` | u16 x0.1 % | R/W |
| 1071 | `windows_weather_stale_policy` | u16, `0=CLOSE_SAFE`, `1=IGNORE` | R/W |
| 1072 | `windows_base_target_a` | u16 x0.1 % | R |
| 1073 | `windows_base_target_b` | u16 x0.1 % | R |
| 1074 | `windows_effective_target_a` | u16 x0.1 % | R |
| 1075 | `windows_effective_target_b` | u16 x0.1 % | R |
| 1076 | `windows_active_protection_bits` | u16 | R |
| 1077 | `windows_windward_side` | u16, `0=NONE`, `1=A`, `2=B`, `3=BOTH_UNKNOWN` | R |
| 1078 | `windows_temp_step_target_percent` | u16 x0.1 % | R/W |
| 1080 | `windows_hum_step_target_percent` | u16 x0.1 % | R/W |
| 1082 | `windows_weather_stale_timeout_ms` | u16 ms | R/W |
| 1083 | `windows_weather_source_age_s` | u16 s | R |

#### Heating control/runtime, `1100..1117`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1100 | `heating_ctrl_mode` | u16, `0=AUTO`, `1=OFF`, `2=MANUAL` | R/W |
| 1101 | `heating_air_setpoint` | s16 x0.1 degC | R/W |
| 1102 | `heating_air_hyst` | s16 x0.1 degC | R/W |
| 1103 | `heating_stage_delta_1` | s16 x0.1 degC | R/W |
| 1104 | `heating_stage_delta_2` | s16 x0.1 degC | R/W |
| 1105 | `heating_stage_delta_3` | s16 x0.1 degC | R/W |
| 1106 | `heating_stage_delta_4` | s16 x0.1 degC | R/W |
| 1107 | `heating_min_on_s` | u16 s | R/W |
| 1108 | `heating_min_off_s` | u16 s | R/W |
| 1109 | `heating_manual_pump_mask` | u16 | R/W |
| 1110 | `heating_manual_valve_open_mask` | u16 | R/W |
| 1111 | `heating_manual_valve_close_mask` | u16 | R/W |
| 1112 | `heating_status_bits` | u16 | R |
| 1113 | `heating_active_stage` | u16 | R |
| 1114 | `heating_pump_mask` | u16 | R |
| 1115 | `heating_valve_open_mask` | u16 | R |
| 1116 | `heating_valve_close_mask` | u16 | R |
| 1117 | `heating_sensor_status_bits` | u16 | R |

#### Curtain control/runtime

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1130 | `curtain_ctrl_mode` | u16, `0=AUTO`, `1=MANUAL`, `2=OFF` | R/W |
| 1131 | `curtain_manual_target` | u16 x0.1 % | R/W |
| 1132 | `curtain_schedule_start_hhmm` | u16 HHMM | R/W |
| 1133 | `curtain_schedule_end_hhmm` | u16 HHMM | R/W |
| 1134 | `curtain_outside_target` | u16 x0.1 % | R/W |
| 1135 | `curtain_min_position` | u16 x0.1 % | R/W |
| 1136 | `curtain_max_position` | u16 x0.1 % | R/W |
| 1137 | `curtain_position_hyst` | u16 x0.1 % | R/W |
| 1138 | `curtain_radiation_threshold` | u16 W/m2 | R/W |
| 1139 | `curtain_radiation_step_wm2` | u16 W/m2 | R/W |
| 1140 | `curtain_radiation_step_percent` | u16 x0.1 % | R/W |
| 1141 | `curtain_radiation_hyst` | u16 W/m2 | R/W |
| 1142 | `curtain_cold_delta` | s16 x0.1 degC | R/W |
| 1143 | `curtain_cold_hyst` | s16 x0.1 degC | R/W |
| 1144 | `curtain_cold_target` | u16 x0.1 % | R/W |
| 1145 | `curtain_heat_delta` | s16 x0.1 degC | R/W |
| 1146 | `curtain_heat_hyst` | s16 x0.1 degC | R/W |
| 1147 | `curtain_heat_target` | u16 x0.1 % | R/W |
| 1148 | `curtain_hum_low_delta` | u16 x0.1 %RH | R/W |
| 1149 | `curtain_hum_low_hyst` | u16 x0.1 %RH | R/W |
| 1150 | `curtain_hum_low_target` | u16 x0.1 % | R/W |
| 1151 | `curtain_hum_high_delta` | u16 x0.1 %RH | R/W |
| 1152 | `curtain_hum_high_hyst` | u16 x0.1 %RH | R/W |
| 1153 | `curtain_hum_high_target` | u16 x0.1 % | R/W |
| 1154 | `curtain_fault_reset_token` | u16 | R/W |
| 700 | `curtain_target` | u16 x0.1 % | R |
| 701 | `curtain_base_target` | u16 x0.1 % | R |
| 702 | `curtain_current_ma` | u16 x0.1 mA | R |
| 703 | `curtain_status_bits` | u16 | R |
| 704 | `curtain_reason_bits` | u16 | R |
| 705 | `curtain_position_status_bits` | u16 | R |
| 706 | `curtain_fault_code` | u16 | R |

#### Global greenhouse targets, `1160..1161`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1160 | `air_temp_target` | s16 x0.1 degC | R/W |
| 1161 | `air_hum_target` | u16 x0.1 %RH | R/W |

#### CO2 control/runtime, `1170..1202`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1170 | `co2_measured_ppm` | u16 ppm | R |
| 1171 | `co2_sensor_valid` | u16 | R |
| 1172 | `co2_ctrl_mode` | u16, `0=AUTO`, `1=OFF`, `2=MANUAL` | R/W |
| 1173 | `co2_manual_outputs` | u16 | R/W |
| 1174 | `co2_schedule_start_hhmm` | u16 HHMM | R/W |
| 1175 | `co2_schedule_end_hhmm` | u16 HHMM | R/W |
| 1176 | `co2_low_light_threshold_wm2` | u16 W/m2 | R/W |
| 1177 | `co2_mid_light_threshold_wm2` | u16 W/m2 | R/W |
| 1178 | `co2_high_light_threshold_wm2` | u16 W/m2 | R/W |
| 1179 | `co2_low_light_target_ppm` | u16 ppm | R/W |
| 1180 | `co2_mid_light_target_ppm` | u16 ppm | R/W |
| 1181 | `co2_high_light_target_ppm` | u16 ppm | R/W |
| 1182 | `co2_vent_limit_low_percent` | u16 x0.1 % | R/W |
| 1183 | `co2_vent_limit_high_percent` | u16 x0.1 % | R/W |
| 1184 | `co2_vent_cutoff_percent` | u16 x0.1 % | R/W |
| 1185 | `co2_dosing_hyst_ppm` | u16 ppm | R/W |
| 1186 | `co2_max_safe_ppm` | u16 ppm | R/W |
| 1187 | `co2_max_dosing_time_s` | u16 s | R/W |
| 1188 | `co2_min_pause_time_s` | u16 s | R/W |
| 1189 | `co2_no_rise_check_time_s` | u16 s | R/W |
| 1190 | `co2_no_rise_min_delta_ppm` | u16 ppm | R/W |
| 1191 | `co2_temp_high_delta` | s16 x0.1 degC | R/W |
| 1192 | `co2_temp_critical_delta` | s16 x0.1 degC | R/W |
| 1193 | `co2_hum_high_delta` | u16 x0.1 %RH | R/W |
| 1194 | `co2_external_alarm` | u16 | R |
| 1195 | `co2_target_ppm` | u16 ppm | R |
| 1196 | `co2_effective_target_ppm` | u16 ppm | R |
| 1197 | `co2_status_bits` | u16 | R |
| 1198 | `co2_reason_bits` | u16 | R |
| 1199 | `co2_protection_bits` | u16 | R |
| 1200 | `co2_fault_code` | u16 | R |
| 1201 | `co2_dosing_elapsed_s` | u16 s | R |
| 1202 | `co2_fault_reset_token` | u16 | R/W |

#### Side curtain minimal block, `1230..1237`

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1230 | `side_curtain_ctrl_mode` | u16 | R/W |
| 1231 | `side_curtain_manual_target` | u16 x0.1 % | R/W |
| 1232 | `side_curtain_schedule_start_hhmm` | u16 HHMM | R/W |
| 1233 | `side_curtain_schedule_end_hhmm` | u16 HHMM | R/W |
| 1234 | `side_curtain_min_position` | u16 x0.1 % | R/W |
| 1235 | `side_curtain_max_position` | u16 x0.1 % | R/W |
| 1236 | `side_curtain_target` | u16 x0.1 % | R |
| 1237 | `side_curtain_status_bits` | u16 | R |

#### Circulation fan control/runtime, `1240..1266`

Диапазон начинается с `1240`, чтобы не пересекаться с минимальным блоком
боковой шторы `1230..1237`.

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1240 | `circ_ctrl_mode` | u16, `0=AUTO`, `1=OFF`, `2=MANUAL` | R/W |
| 1241 | `circ_manual_fan_mask` | u16 | R/W |
| 1242 | `circ_available_fan_mask` | u16 | R/W |
| 1243 | `circ_schedule_start_hhmm` | u16 HHMM | R/W |
| 1244 | `circ_schedule_end_hhmm` | u16 HHMM | R/W |
| 1245 | `circ_co2_fan_mask` | u16 | R/W |
| 1246 | `circ_heating_fan_mask` | u16 | R/W |
| 1247 | `circ_humidity_fan_mask` | u16 | R/W |
| 1248 | `circ_day_fan_mask` | u16 | R/W |
| 1249 | `circ_night_fan_mask` | u16 | R/W |
| 1250 | `circ_vent_limited_fan_mask` | u16 | R/W |
| 1251 | `circ_vent_limit_percent` | u16 x0.1 % | R/W |
| 1252 | `circ_vent_cutoff_percent` | u16 x0.1 % | R/W |
| 1253 | `circ_hum_high_delta` | u16 x0.1 %RH | R/W |
| 1254 | `circ_day_cycle_on_s` | u16 s | R/W |
| 1255 | `circ_day_cycle_off_s` | u16 s | R/W |
| 1256 | `circ_night_cycle_on_s` | u16 s | R/W |
| 1257 | `circ_night_cycle_off_s` | u16 s | R/W |
| 1258 | `circ_hum_cycle_on_s` | u16 s | R/W |
| 1259 | `circ_hum_cycle_off_s` | u16 s | R/W |
| 1260 | `circ_min_on_s` | u16 s | R/W |
| 1261 | `circ_min_off_s` | u16 s | R/W |
| 1262 | `circ_output_fan_mask` | u16 | R |
| 1263 | `circ_status_bits` | u16 | R |
| 1264 | `circ_reason_bits` | u16 | R |
| 1265 | `circ_protection_bits` | u16 | R |
| 1266 | `circ_requested_fan_mask` | u16 | R |

### 7.4 Light schedules/config

Диапазон `1300..1599`.

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1300 | `LIGHT_R1_ENABLE` | u16 0/1 | R/W |
| 1301 | `LIGHT_R1_ON_HHMM` | u16 HHMM | R/W |
| 1302 | `LIGHT_R1_OFF_HHMM` | u16 HHMM | R/W |
| 1303 | `LIGHT_R1_THRESHOLD_WM2` | u16 W/m2 | R/W |
| 1305 | `LIGHT_R1_DLI_OFF_LIMIT_JCM2` | u16 J/cm2 | R/W |
| 1306 | `LIGHT_R2_ENABLE` | u16 0/1 | R/W |
| 1307 | `LIGHT_R2_ON_HHMM` | u16 HHMM | R/W |
| 1308 | `LIGHT_R2_OFF_HHMM` | u16 HHMM | R/W |
| 1309 | `LIGHT_R2_THRESHOLD_WM2` | u16 W/m2 | R/W |
| 1311 | `LIGHT_R2_DLI_OFF_LIMIT_JCM2` | u16 J/cm2 | R/W |
| 1312 | `LIGHT_HYST_SEC` | u16 seconds | R/W |

Runtime:

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 707 | `light_status_bits` | u16 | R |
| 1613 | `light_current_dli_jcm2` | u16 J/cm2 | R/W |

Полный `FC16(1300, 13)` применяет light config сразу. Частичные записи применяются после settle-паузы `250 ms`.

### 7.5 Weather sync and external DLI input

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1600 | `weather_out_temp` | s16 x0.1 degC | R/W |
| 1601 | `weather_out_humidity` | u16 x0.1 %RH | R/W |
| 1602 | `weather_wind_speed` | u16 x0.1 m/s | R/W |
| 1603 | `weather_wind_dir` | u16 deg | R/W |
| 1604 | `weather_rain_flag` | u16 0/1 | R/W |
| 1605 | `weather_solar_rad` | u16 W/m2 | R/W |
| 1606 | `weather_baro_press` | u16 x0.1 hPa | R/W |
| 1607 | `weather_dew_point` | s16 x0.1 degC | R/W |
| 1608 | `weather_status_bits` | u16 | R/W |
| 1609 | `weather_age_s` | u16 | R/W |
| 1610 | `weather_token` | u16 | R/W |
| 1611 | `weather_applied_token` | u16 | R |
| 1612 | `weather_result` | u16 | R |
| 1613 | `light_current_dli_jcm2` | u16 J/cm2 | R/W |

### 7.6 Advanced diagnostics / RTC debug

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1800 | `RTC_SET_HOUR` | u16 0..23 | R/W |
| 1801 | `RTC_SET_MINUTE` | u16 0..59 | R/W |
| 1802 | `RTC_SET_TOKEN` | u16 | R/W |
| 421 | `RTC_SET_APPLIED_TOKEN` | u16 | R |
| 422 | `RTC_SET_RESULT` | u16 | R |

## 8. Irrigation Slave map v1

`device_type = 2`, `modbus_map_version = 1`.

### 8.1 Telemetry

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 400 | `tank_level` | u16 x0.1 % | R |
| 401 | `main_pressure` | u16 x0.01 bar | R |
| 402 | `line_pressure_1` | u16 x0.01 bar | R |
| 403 | `line_pressure_2` | u16 x0.01 bar | R |
| 404 | `ec_value` | u16 x0.01 mS/cm | R |
| 405 | `ph_value` | u16 x0.01 pH | R |
| 406 | `water_temp` | s16 x0.1 degC | R |
| 407 | `flow_rate` | u16 x0.1 l/min | R |
| 408 | `total_volume_hi` | u16 | R |
| 409 | `total_volume_lo` | u16 | R |
| 410 | `pump_output` | u16 x0.1 % | R |
| 411 | `valve_state_bits` | u16 | R |
| 412 | `irrigation_state_bits` | u16 | R |
| 417 | `sensor_fault_bits` | u16 | R |
| 418 | `actuator_fault_bits` | u16 | R |

### 8.2 Setpoints and commands

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1000 | `control_mode` | u16 | R/W |
| 1001 | `target_ec` | u16 x0.01 mS/cm | R/W |
| 1002 | `target_ph` | u16 x0.01 pH | R/W |
| 1003 | `target_volume_l` | u16 liters | R/W |
| 1004 | `target_duration_s` | u16 seconds | R/W |
| 1005 | `zone_valve_mask` | u16 | R/W |
| 1006 | `start_irrigation_cmd` | u16 0/1 | R/W |
| 1007 | `stop_irrigation_cmd` | u16 0/1 | R/W |
| 1015 | `command_token` | u16 | R/W |
| 1016 | `applied_token` | u16 | R |
| 1017 | `command_result` | u16 | R |

## 9. Fog Slave map v1

`device_type = 3`, `modbus_map_version = 1`.

### 9.1 Telemetry

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 400 | `water_pressure` | u16 x0.01 bar | R |
| 401 | `pump_output` | u16 x0.1 % | R |
| 402 | `fog_line_pressure` | u16 x0.01 bar | R |
| 403 | `water_level` | u16 x0.1 % | R |
| 404 | `water_temp` | s16 x0.1 degC | R |
| 405 | `fog_state_bits` | u16 | R |
| 406 | `nozzle_state_bits` | u16 | R |
| 407 | `runtime_today_min` | u16 minutes | R |
| 417 | `sensor_fault_bits` | u16 | R |
| 418 | `actuator_fault_bits` | u16 | R |

### 9.2 Setpoints and commands

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1000 | `control_mode` | u16 | R/W |
| 1001 | `humidity_target` | u16 x0.1 %RH | R/W |
| 1002 | `fog_enable` | u16 0/1 | R/W |
| 1003 | `fog_intensity` | u16 x0.1 % | R/W |
| 1004 | `min_on_time_s` | u16 seconds | R/W |
| 1005 | `min_off_time_s` | u16 seconds | R/W |
| 1006 | `zone_mask` | u16 | R/W |
| 1015 | `command_token` | u16 | R/W |
| 1016 | `applied_token` | u16 | R |
| 1017 | `command_result` | u16 | R |

## 10. Boiler Slave map v1

`device_type = 4`, `modbus_map_version = 1`.

### 10.1 Telemetry

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 400 | `boiler_supply_temp` | s16 x0.1 degC | R |
| 401 | `boiler_return_temp` | s16 x0.1 degC | R |
| 402 | `circuit_1_supply_temp` | s16 x0.1 degC | R |
| 403 | `circuit_1_return_temp` | s16 x0.1 degC | R |
| 404 | `circuit_2_supply_temp` | s16 x0.1 degC | R |
| 405 | `circuit_2_return_temp` | s16 x0.1 degC | R |
| 406 | `pressure` | u16 x0.01 bar | R |
| 407 | `burner_output` | u16 x0.1 % | R |
| 408 | `pump_state_bits` | u16 | R |
| 409 | `valve_state_bits` | u16 | R |
| 410 | `boiler_state_bits` | u16 | R |
| 417 | `sensor_fault_bits` | u16 | R |
| 418 | `actuator_fault_bits` | u16 | R |

### 10.2 Setpoints and commands

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1000 | `control_mode` | u16 | R/W |
| 1001 | `boiler_target_temp` | s16 x0.1 degC | R/W |
| 1002 | `circuit_1_target_temp` | s16 x0.1 degC | R/W |
| 1003 | `circuit_2_target_temp` | s16 x0.1 degC | R/W |
| 1004 | `pump_enable_mask` | u16 | R/W |
| 1005 | `heating_enable` | u16 0/1 | R/W |
| 1006 | `emergency_stop` | u16 0/1 | R/W |
| 1015 | `command_token` | u16 | R/W |
| 1016 | `applied_token` | u16 | R |
| 1017 | `command_result` | u16 | R |

## 11. Osmosis Slave map v1

`device_type = 5`, `modbus_map_version = 1`.

### 11.1 Telemetry

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 400 | `inlet_pressure` | u16 x0.01 bar | R |
| 401 | `outlet_pressure` | u16 x0.01 bar | R |
| 402 | `membrane_pressure` | u16 x0.01 bar | R |
| 403 | `permeate_flow` | u16 x0.1 l/min | R |
| 404 | `concentrate_flow` | u16 x0.1 l/min | R |
| 405 | `inlet_ec` | u16 x0.01 mS/cm | R |
| 406 | `permeate_ec` | u16 x0.01 mS/cm | R |
| 407 | `water_temp` | s16 x0.1 degC | R |
| 408 | `tank_level` | u16 x0.1 % | R |
| 409 | `filter_runtime_h` | u16 hours | R |
| 410 | `osmosis_state_bits` | u16 | R |
| 417 | `sensor_fault_bits` | u16 | R |
| 418 | `actuator_fault_bits` | u16 | R |

### 11.2 Setpoints and commands

| Reg | Name | Type / scale | Access |
|---:|---|---|---|
| 1000 | `control_mode` | u16 | R/W |
| 1001 | `target_tank_level` | u16 x0.1 % | R/W |
| 1002 | `max_inlet_ec` | u16 x0.01 mS/cm | R/W |
| 1003 | `flush_interval_min` | u16 minutes | R/W |
| 1004 | `flush_duration_s` | u16 seconds | R/W |
| 1005 | `start_cmd` | u16 0/1 | R/W |
| 1006 | `stop_cmd` | u16 0/1 | R/W |
| 1007 | `flush_cmd` | u16 0/1 | R/W |
| 1015 | `command_token` | u16 | R/W |
| 1016 | `applied_token` | u16 | R |
| 1017 | `command_result` | u16 | R |

## 12. Требования к обработке записей

Слейв должен быстро принимать Modbus-запись и не выполнять долгую физическую операцию внутри Modbus callback.

Рекомендуемая схема:

1. `FC16` записывает уставки в holding-регистры.
2. Если записан новый `command_token`, слейв копирует блок уставок во внутреннюю очередь/буфер.
3. Modbus callback сразу возвращает успешный ответ.
4. Основной цикл прошивки проверяет новый token и применяет уставки.
5. После применения слейв записывает:
   - `applied_token = command_token`;
   - `command_result = APPLIED/REJECT/...`.

Если запись частичная и не содержит `command_token`, слейв только обновляет регистры, но не обязан применять команду немедленно.

## 13. Автономность и безопасность

Если нет валидных запросов мастера дольше `30 s`, слейв должен:

- выставить `status_bits.master_timeout_detected`;
- перейти в автономный или безопасный режим согласно своей логике;
- не выполнять старые команды повторно;
- продолжать локальную защиту исполнительных устройств.

Обязательные защиты, если применимо:

- контроль концевиков;
- контроль времени движения моторов;
- аварийная остановка;
- защита от выхода за диапазон уставок;
- диагностика датчиков;
- safe state при критической ошибке.

## 14. Минимальная приемка прошивки слейва

1. Слейв отвечает на `FC03 0..15`.
2. `device_type` соответствует одному из пяти типов.
3. `modbus_map_version = 1`.
4. `capability_low/high` корректно описывает установленное оборудование.
5. Слейв отвечает на `FC03 400..419` для своей карты.
6. Слейв принимает `FC16 1000..1017`.
7. Новый `command_token` приводит к обновлению `applied_token` и `command_result`.
8. Неверные значения уставок отклоняются с `REJECT_RANGE`.
9. Любой валидный Modbus-запрос обновляет heartbeat мастера.
10. При отсутствии мастера более `30 s` слейв переходит в безопасный/автономный режим.

## 15. Требования к хабу

Хаб должен содержать жесткий каталог карт и выбирать карту по паре:

```text
device_type + modbus_map_version
```

Хаб не должен угадывать смысл регистров по адресу без выбранной карты.

Хаб должен считать устройство неподдерживаемым, если:

- `device_type` неизвестен;
- `modbus_map_version` неизвестна для данного `device_type`;
- обязательные capabilities отсутствуют;
- identity-регистры содержат некорректные значения.
