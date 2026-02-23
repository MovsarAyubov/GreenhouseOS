#ifndef GH_MODBUS_IO_H
#define GH_MODBUS_IO_H

#include "main.h"

#include <stdbool.h>

bool GH_ModbusIo_OnUartTxCplt(UART_HandleTypeDef *huart);
bool GH_ModbusIo_OnUartRxCplt(UART_HandleTypeDef *huart);
bool GH_ModbusIo_OnUartError(UART_HandleTypeDef *huart);

#endif /* GH_MODBUS_IO_H */
