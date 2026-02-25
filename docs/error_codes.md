# Error Codes Reference

Документ объединяет коды ошибок и диагностических состояний, которые используются в `greenhouseOS`.

## 1) Event Codes (g_status.last_error_code / event log)

Источник: `Core/Inc/gh_runtime_state.h`, публикация через `publish_event(...)`.

| Code | Name | Когда возникает | Что это значит | Что проверить |
|---|---|---|---|---|
| `1000` | `EVENT_CODE_LINK_DOWN` | Потеря сетевого линка/состояния TCP-сервера | Сетевой стек не может обслуживать Modbus TCP стабильно | PHY link, кабель, питание Ethernet, `gnetif.flags`, counters `link_down_count` |
| `1001` | `EVENT_CODE_LINK_UP` | Восстановление линка/готовности TCP | Сеть снова в рабочем состоянии | стабильность линка, отсутствие флаппинга |
| `1100` | `EVENT_CODE_CFG_APPLIED` | Конфигурация успешно применена | Новая версия конфигурации принята и активирована | `active_version`, ACK по конфигу |
| `1101` | `EVENT_CODE_CFG_REJECTED` | Конфиг отклонен на валидации/очереди/flash | Конфигурация не применена, в `value` передается причина (`CFG_RESULT_*`) | см. раздел 2 |
| `1200` | `EVENT_CODE_WDG_MISS` | Watchdog не получил heartbeat от задач | Обнаружено зависание/просрочка задачи | маска пропущенных задач в `value`, counters WDG |
| `1300` | `EVENT_CODE_CTRL_SYNC_FAIL` | Не удалось синхронизировать управление со slave | Контрольный обмен с RTU slave завершился ошибкой/таймаутом | `modbus_timeouts[]`, RS485 линия, slave доступность |

### Severity codes (для event log)

| Code | Name | Значение |
|---|---|---|
| `0` | `EVENT_SEV_INFO` | Информационное событие |
| `1` | `EVENT_SEV_WARN` | Предупреждение |
| `2` | `EVENT_SEV_ALARM` | Тревога |
| `3` | `EVENT_SEV_CRIT` | Критическая ошибка |

## 2) Config Result Codes (результат приема/применения конфигурации)

Источник: `Core/Inc/gh_runtime_state.h`, обработка в `Core/Src/gh_config_storage.c`.

Эти коды возвращаются в Modbus map как результат операции и используются как `value` для `EVENT_CODE_CFG_REJECTED`.

| Code | Name | Когда возникает | Что это значит | Действие |
|---|---|---|---|---|
| `0` | `CFG_RESULT_IDLE` | Нет активной операции | Состояние ожидания | нет |
| `1` | `CFG_RESULT_QUEUED` | Запрос принят в очередь | Ожидается обработка | дождаться финального статуса |
| `2` | `CFG_RESULT_APPLIED` | Применение завершено успешно | Конфиг стал активным | проверить `active_version` |
| `10` | `CFG_RESULT_REJECT_BAD_VERSION` | Неверная версия payload | Формат/версия не поддержана | отправить корректную версию |
| `11` | `CFG_RESULT_REJECT_BAD_CRC` | Ошибка CRC payload | Данные повреждены | пересчитать CRC и отправить снова |
| `12` | `CFG_RESULT_REJECT_RANGE` | Поля вне допустимого диапазона | Значения нарушают ограничения | исправить параметры |
| `13` | `CFG_RESULT_REJECT_QUEUE_FULL` | Очередь занята/переполнена | Запрос не принят к обработке | повторить позже |
| `14` | `CFG_RESULT_FLASH_FAIL` | Ошибка записи в flash | Конфиг не сохранен | проверить flash сектор/питание, retry |
| `15` | `CFG_RESULT_APPLY_QUEUE_FAIL` | Не удалось передать в apply queue | Конфиг не применен после записи | анализ загрузки задач/очередей |

## 3) Reset Reason Codes (сохраненные причины перезапуска)

Источник: `Core/Src/main.c`, `Core/Src/stm32f4xx_it.c`.
Хранятся в backup-регистрах RTC, читаются как `g_persist_last_reset_reason`.

| Code | Name | Когда записывается | Что это значит |
|---|---|---|---|
| `0xE001` | `RESET_REASON_ERROR_HANDLER` | Вызван `Error_Handler()` | Критическая ошибка инициализации/работы HAL |
| `0xE101` | `RESET_REASON_WATCHDOG_MISS` | Пропуск heartbeat в watchdog | Принудительный reset по watchdog recovery |
| `0xE201` | `GH_RESET_REASON_NMI` | В `NMI_Handler` | Невосстанавливаемое NMI-событие |
| `0xE202` | `GH_RESET_REASON_HARDFAULT` | В `HardFault_Handler` | HardFault (ошибка выполнения/доступа) |
| `0xE203` | `GH_RESET_REASON_MEMMANAGE` | В `MemManage_Handler` | Ошибка memory protection |
| `0xE204` | `GH_RESET_REASON_BUSFAULT` | В `BusFault_Handler` | Ошибка шины/доступа к памяти |
| `0xE205` | `GH_RESET_REASON_USAGEFAULT` | В `UsageFault_Handler` | Неверная инструкция/состояние ядра |

## 4) Modbus Stack Error Codes (mHandlers[i]->i8lastError)

Источник: `Core/Inc/Modbus.h` (`mb_err_op_t`).

| Code | Name | Когда возникает | Что это значит | Что проверить |
|---|---|---|---|---|
| `10` | `ERR_NOT_MASTER` | Master API вызвана не master handler'ом | Неверная роль обработчика | инициализацию `uModbusType/u8id` |
| `11` | `ERR_POLLING` | Запрос в момент активного обмена | Handler занят | корректность state machine/период poll |
| `12` | `ERR_BUFF_OVERFLOW` | Переполнение входного буфера | Кадр больше лимита/ошибка приема | `MAX_BUFFER`, длины кадров, шум линии |
| `13` | `ERR_BAD_CRC` | CRC не совпал | Повреждение кадра | RS485 качество, скорость/паритет, помехи |
| `14` | `ERR_EXCEPTION` | Slave вернул exception или внутренняя exception-ошибка | Протокольная ошибка запроса/исполнения | адреса регистров, function code, slave логи |
| `15` | `ERR_BAD_SIZE` | Некорректная длина фрейма | Неверный формат данных | MBAP/RTU длина, клиентскую реализацию |
| `16` | `ERR_BAD_ADDRESS` | Неверный адрес/карта регистров | Запрос вне карты | соответствие map и запроса |
| `17` | `ERR_TIME_OUT` | Ответ не получен за timeout | Таймаут устройства/сети | slave online, timeout policy, сеть/RS485 |
| `18` | `ERR_BAD_SLAVE_ID` | Неверный slave id | ID запроса некорректен | диапазон 1..247, master id=0 |
| `19` | `ERR_BAD_TCP_ID` | Некорректный client ID для TCP handler | Ошибка идентификации TCP-клиента | логику подключения/идентификатор |
| `20` | `OP_OK_QUERY` | Успешное завершение операции | Не ошибка, служебный код успеха | нет |

## 5) Modbus Exception Codes (протокольные)

Источник: `Core/Inc/Modbus.h` (`EXC_*`).

| Code | Name | Смысл |
|---|---|---|
| `1` | `EXC_FUNC_CODE` | Неподдерживаемый function code |
| `2` | `EXC_ADDR_RANGE` | Адрес вне допустимого диапазона |
| `3` | `EXC_REGS_QUANT` | Некорректное количество регистров/coil |
| `4` | `EXC_EXECUTE` | Ошибка выполнения команды на стороне slave |

## 6) Где смотреть в отладке

- `g_status.last_error_code`: последний event/error код уровня приложения.
- `g_persist_last_reset_reason`: причина последнего reset (из RTC backup).
- `mHandlers[0]->i8lastError`: последняя ошибка Modbus стека.
- `g_status.modbus_timeouts[n]`: таймауты обмена по каждому slave (индекс `n = slave_id - 1`).

