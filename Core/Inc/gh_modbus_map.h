#ifndef GH_MODBUS_MAP_H
#define GH_MODBUS_MAP_H

#include "gh_runtime_state.h"

#include <stdbool.h>
#include <stdint.h>

#define GH_MB_MAX_SLAVES      20U
#define GH_MB_POINT_MAX       SENSOR_COUNT
#define GH_MB_POINT_STRIDE    6U
#define GH_MB_MAP_VERSION     4U
#define GH_MB_HOLDING_BASE    41000U
#define GH_MB_POINTS_BASE     0U
#define GH_MB_POINTS_REGS     (GH_MB_POINT_MAX * GH_MB_POINT_STRIDE)
#define GH_MB_SLAVE_STATUS_BLOCK_SIZE 8U
#define GH_MB_SLAVE_STATUS_BASE (GH_MB_POINTS_BASE + GH_MB_POINTS_REGS)
#define GH_MB_SLAVE_STATUS_REGS (GH_MB_MAX_SLAVES * GH_MB_SLAVE_STATUS_BLOCK_SIZE)
#define GH_MB_CMD_PAYLOAD_WORDS TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS
#define GH_MB_CMD_BLOCK_SIZE  (4U + GH_MB_CMD_PAYLOAD_WORDS + 4U)
#define GH_MB_CMD_BASE        (GH_MB_SLAVE_STATUS_BASE + GH_MB_SLAVE_STATUS_REGS)
#define GH_MB_CMD_REGS        GH_MB_CMD_BLOCK_SIZE
#define GH_MB_DIR_BASE        (GH_MB_CMD_BASE + GH_MB_CMD_REGS)
#define GH_MB_DIR_REGS        32U
#define GH_MB_DIR_OFF_RTC_HOUR   14U
#define GH_MB_DIR_OFF_RTC_MINUTE 15U
#define GH_MB_DIR_OFF_RTC_SET_HOUR   16U
#define GH_MB_DIR_OFF_RTC_SET_MINUTE 17U
#define GH_MB_DIR_OFF_RTC_SET_TOKEN  18U
#define GH_MB_DIR_OFF_RTC_SET_APPLIED_TOKEN 19U
#define GH_MB_DIR_OFF_RTC_SET_RESULT 20U
#define GH_MB_DIR_OFF_RTC_SYNC_ATTEMPT_HI 21U
#define GH_MB_DIR_OFF_RTC_SYNC_ATTEMPT_LO 22U
#define GH_MB_DIR_OFF_RTC_SYNC_OK_HI      23U
#define GH_MB_DIR_OFF_RTC_SYNC_OK_LO      24U
#define GH_MB_DIR_OFF_RTC_SYNC_FAIL_HI    25U
#define GH_MB_DIR_OFF_RTC_SYNC_FAIL_LO    26U
#define GH_MB_DIR_OFF_RTC_SYNC_LAST_SLAVE 27U
#define GH_MB_DIR_OFF_RTC_SYNC_LAST_TOKEN 28U
#define GH_MB_DIR_OFF_RTC_SYNC_LAST_RESULT 29U
#define GH_MB_RTC_SET_RESULT_IDLE         0U
#define GH_MB_RTC_SET_RESULT_QUEUED       1U
#define GH_MB_RTC_SET_RESULT_APPLIED      2U
#define GH_MB_RTC_SET_RESULT_REJECT_RANGE 3U
#define GH_MB_RTC_SET_RESULT_FAILED       4U
#define GH_MB_CFG_BASE        (GH_MB_DIR_BASE + GH_MB_DIR_REGS)
#define GH_MB_CFG_REGS        80U
#define GH_MB_DIAG_BASE       (GH_MB_CFG_BASE + GH_MB_CFG_REGS)
#define GH_MB_DIAG_REGS       32U
#define GH_MB_TOPO_BASE       (GH_MB_DIAG_BASE + GH_MB_DIAG_REGS)
#define GH_MB_TOPO_REGS       144U
#define GH_MB_TCP_TRACE_BASE  (GH_MB_TOPO_BASE + GH_MB_TOPO_REGS)
#define GH_MB_TCP_TRACE_ENTRY_REGS 20U
#define GH_MB_TCP_TRACE_REGS  121U
#define GH_MB_TOTAL_REGS      (GH_MB_POINTS_REGS + GH_MB_SLAVE_STATUS_REGS + GH_MB_CMD_REGS + \
                               GH_MB_DIR_REGS + GH_MB_CFG_REGS + GH_MB_DIAG_REGS + GH_MB_TOPO_REGS + \
                               GH_MB_TCP_TRACE_REGS)
#define GH_MB_SCHED_CMD_KIND_REMOTE_SCHEDULE 1U
#define GH_MB_DCMD_RESULT_IDLE             0U
#define GH_MB_DCMD_RESULT_QUEUED           1U
#define GH_MB_DCMD_RESULT_APPLIED          2U
#define GH_MB_DCMD_RESULT_REJECT_BOUNDS    10U
#define GH_MB_DCMD_RESULT_REJECT_TOPOLOGY  11U
#define GH_MB_DCMD_RESULT_REJECT_FC        12U
#define GH_MB_DCMD_RESULT_REJECT_BUSY      13U
#define GH_MB_DCMD_RESULT_REJECT_PARTIAL   14U
#define GH_MB_DCMD_RESULT_TRANSPORT_FAIL   15U
#define GH_MB_DCMD_RESULT_ACK_FAIL         16U

typedef struct
{
  uint16_t trigger;
  uint8_t slave_id;
  uint16_t module_id;
  uint16_t cmd_profile_id;
  uint16_t payload_len;
  uint16_t payload[GH_MB_CMD_PAYLOAD_WORDS];
} gh_data_driven_command_request_t;

typedef struct
{
  uint16_t trigger;
  uint16_t result;
  modbus_io_error_t io_error;
} gh_data_driven_command_result_t;

typedef struct
{
  uint16_t token;
  uint8_t hour;
  uint8_t minute;
} gh_rtc_set_request_t;

void GH_ModbusMap_Init(void);
void GH_ModbusMap_UpdateAges(uint32_t now_ms);
void GH_ModbusMap_UpdateRtcTime(uint8_t hour, uint8_t minute);
bool GH_ModbusMap_GetRtcSetRequest(gh_rtc_set_request_t *out_req);
void GH_ModbusMap_MarkRtcSetResult(uint16_t token, bool applied, uint8_t hour, uint8_t minute);
void GH_ModbusMap_ReportRtcSyncDiag(uint32_t attempts,
                                    uint32_t success,
                                    uint32_t failed,
                                    uint16_t last_slave_id,
                                    uint16_t last_token,
                                    uint16_t last_result);

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

bool GH_ModbusMap_GetDataDrivenCommandRequest(gh_data_driven_command_request_t *out_req);
void GH_ModbusMap_MarkDataDrivenCommandResult(const gh_data_driven_command_result_t *result);
void GH_ModbusMap_ReportConfigResult(uint16_t token,
                                     config_result_code_t result,
                                     uint32_t active_version);
void GH_ModbusMap_ReportTopologyResult(uint16_t token,
                                       config_result_code_t result,
                                       uint32_t generation,
                                       uint32_t active_size);

/* Legacy bootstrap pointer for third-party Modbus stack. Avoid direct writes. */
uint16_t *GH_ModbusMap_GetBackingStore(void);
uint16_t GH_ModbusMap_GetBackingStoreSize(void);

#endif /* GH_MODBUS_MAP_H */
