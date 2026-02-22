#ifndef GH_RUNTIME_STATE_H
#define GH_RUNTIME_STATE_H

#include "main.h"
#include "cmsis_os.h"

#include <stdbool.h>
#include <stdint.h>

#define SENSOR_COUNT                  150U
#define MODBUS_MAX_SLAVES             12U
#define STATUS_MODBUS_TIMEOUT_SLOTS   8U
#define MODBUS_MAX_SENSORS_PER_SLAVE  12U
#define MODBUS_MAX_REGS_PER_REQ       32U
#define BLOCK_CHANNEL_COUNT           9U
#define MODBUS_POLL_PERIOD_MS         5000U
#define HEARTBEAT_PERIOD_MS           1000U

#define CONFIG_PAYLOAD_SIZE           128U
#define CONFIG_VALID_MARKER           0xA55A5AA5UL
#define CONFIG_SLOT_A_ADDR            0x08040000UL
#define CONFIG_SLOT_B_ADDR            0x08060000UL
#define CONFIG_SLOT_A_SECTOR          FLASH_SECTOR_6
#define CONFIG_SLOT_B_SECTOR          FLASH_SECTOR_7

#define EVENT_CODE_LINK_DOWN          1000U
#define EVENT_CODE_LINK_UP            1001U
#define EVENT_CODE_CFG_APPLIED        1100U
#define EVENT_CODE_WDG_MISS           1200U
#define EVENT_CODE_CTRL_SYNC_FAIL     1300U

#define MODBUS_DIAG_BASE               128U
#define MODBUS_DIAG_REG_COUNT          6U
#define CTRL_SYNC_PERIOD_MS            5000U

typedef enum
{
  SENSOR_QUALITY_OK = 0U,
  SENSOR_QUALITY_STALE = 1U,
  SENSOR_QUALITY_FAULT = 2U,
  SENSOR_QUALITY_OFFLINE = 3U
} sensor_quality_t;

typedef enum
{
  EVENT_SEV_INFO = 0U,
  EVENT_SEV_WARN = 1U,
  EVENT_SEV_ALARM = 2U,
  EVENT_SEV_CRIT = 3U
} event_severity_t;

typedef enum
{
  APPLY_APPLIED = 0U,
  APPLY_FAILED = 1U
} apply_code_t;

typedef struct
{
  float value;
  uint8_t quality;
  uint32_t timestamp_ms;
} sensor_state_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t payload_crc;
} config_update_req_t;

typedef struct
{
  uint8_t slave_id;
  uint8_t block_no;
  uint16_t start_reg;
  uint16_t sensor_count;
  uint16_t sensor_base;
} modbus_sensor_map_t;

typedef struct __attribute__((packed))
{
  uint32_t link_up_count;
  uint32_t link_down_count;
  uint32_t tcp_connect_count;
  uint32_t tcp_disconnect_count;
  uint32_t crc_errors_rx;
  uint32_t crc_errors_tx;
  uint32_t snapshot_sent_count;
  uint32_t events_generated_count;
  uint32_t events_sent_count;
  uint32_t events_resent_count;
  uint32_t events_acked_count;
  uint32_t modbus_timeouts[STATUS_MODBUS_TIMEOUT_SLOTS];
  uint32_t flash_write_ok_count;
  uint32_t flash_write_fail_count;
  uint32_t last_error_code;
  uint8_t control_mode;
  uint8_t autonomous_reason;
  uint32_t last_master_seen_ms;
  uint16_t good_cycle_streak;
  uint8_t last_apply_status;
  uint8_t reserved0;
} status_payload_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t crc;
} active_config_t;

typedef struct __attribute__((packed))
{
  uint8_t result;
  uint32_t active_version;
} apply_ack_payload_t;

typedef enum
{
  TASK_BIT_CONTROL = (1UL << 0),
  TASK_BIT_MODBUS = (1UL << 1),
  TASK_BIT_CONFIG = (1UL << 2),
  TASK_BIT_WDG = (1UL << 3)
} task_heartbeat_bit_t;

extern UART_HandleTypeDef huart2;

extern osMessageQueueId_t qConfigApplyHandle;
extern osMessageQueueId_t qConfigStoreHandle;

extern sensor_state_t g_sensors[SENSOR_COUNT];
extern status_payload_t g_status;
extern active_config_t g_active_config;
extern uint32_t g_next_event_id;
extern uint32_t g_config_seq;
extern volatile uint8_t g_control_sync_pending;
extern bool g_setpoints_apply_in_progress;

bool modbus_read_holding_registers(uint8_t slave_id,
                                   uint16_t start_reg,
                                   uint16_t reg_count,
                                   uint16_t *out_regs);
bool modbus_write_single_holding_register(uint8_t slave_id, uint16_t reg, uint16_t value);
bool modbus_write_multiple_holding_registers(uint8_t slave_id,
                                             uint16_t start_reg,
                                             uint16_t reg_count,
                                             const uint16_t *regs);
bool apply_control_to_slave(uint8_t slave_id, const active_config_t *cfg);
bool config_write_to_slot(uint32_t slot_addr, uint32_t sector, const active_config_t *cfg);

void task_heartbeat_kick(task_heartbeat_bit_t bit);
void publish_event(uint8_t severity, uint16_t code, uint16_t source, float value);

#endif /* GH_RUNTIME_STATE_H */
