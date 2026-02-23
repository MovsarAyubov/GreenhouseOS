#ifndef GH_MODBUS_MAP_H
#define GH_MODBUS_MAP_H

#include "gh_runtime_state.h"

#include <stdbool.h>
#include <stdint.h>

#define GH_MB_MAX_SLAVES      20U
#define GH_MB_BLOCK_SIZE      64U
#define GH_MB_HOLDING_BASE    41000U
#define GH_MB_DATA_REGS       (GH_MB_MAX_SLAVES * GH_MB_BLOCK_SIZE)
#define GH_MB_CFG_BASE        GH_MB_DATA_REGS
#define GH_MB_CFG_REGS        80U
#define GH_MB_TOTAL_REGS      (GH_MB_DATA_REGS + GH_MB_CFG_REGS)

typedef struct
{
  uint16_t trigger;
  uint16_t setpoints[7];
  uint16_t out_cmd_mask;
} gh_slave_apply_request_t;

void GH_ModbusMap_Init(void);
void GH_ModbusMap_UpdateAges(uint32_t now_ms);

bool GH_ModbusMap_ReadRange(uint16_t start_addr, uint16_t qty, uint16_t *out_regs);
bool GH_ModbusMap_WriteSingle(uint16_t addr, uint16_t value);
bool GH_ModbusMap_WriteRange(uint16_t start_addr, uint16_t qty, const uint16_t *values);

void GH_ModbusMap_UpdateTelemetry(uint8_t slave_id,
                                  const uint16_t *sensors_9,
                                  uint16_t valid_mask,
                                  uint16_t out_state_mask,
                                  uint32_t now_ms);
void GH_ModbusMap_ReportTimeout(uint8_t slave_id, uint32_t now_ms);
void GH_ModbusMap_UpdateDiag(uint8_t slave_id,
                             uint16_t err_timeout,
                             uint16_t err_crc,
                             uint16_t err_exception);

bool GH_ModbusMap_GetApplyRequest(uint8_t slave_id, gh_slave_apply_request_t *out_req);
void GH_ModbusMap_MarkApplyResult(uint8_t slave_id, uint16_t trigger, bool applied);
void GH_ModbusMap_ReportConfigResult(uint16_t token,
                                     config_result_code_t result,
                                     uint32_t active_version);

uint16_t *GH_ModbusMap_GetBackingStore(void);
uint16_t GH_ModbusMap_GetBackingStoreSize(void);

#endif /* GH_MODBUS_MAP_H */
