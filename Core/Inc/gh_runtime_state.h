#ifndef GH_RUNTIME_STATE_H
#define GH_RUNTIME_STATE_H

#include "main.h"
#include "cmsis_os.h"

#include <stdbool.h>
#include <stdint.h>

#define SENSOR_COUNT                  180U
#define MODBUS_MAX_SLAVES             20U
#define STATUS_MODBUS_TIMEOUT_SLOTS   MODBUS_MAX_SLAVES
#define MODBUS_MAX_SENSORS_PER_SLAVE  12U
#define MODBUS_MAX_REGS_PER_REQ       32U
#define BLOCK_CHANNEL_COUNT           9U
#define MODBUS_POLL_PERIOD_MS         5000U
#define MODBUS_INTER_SLAVE_DELAY_MS   1U
#define MODBUS_RETRY_COUNT            2U
#define MODBUS_RETRY_BACKOFF_MS       20U
#define MODBUS_UART_TX_TIMEOUT_MS     100U
#define MODBUS_RTU_RESP_TIMEOUT_MS    300U
#define MODBUS_ENABLED_SLAVE_MASK     0x00000001UL
#define MODBUS_OFFLINE_REPROBE_MS     30000U
#define HEARTBEAT_PERIOD_MS           1000U
#define WDG_TIMEOUT_CONTROL_MS        2000U
#define MODBUS_WDG_REQ_BUDGET_MS      ((MODBUS_RETRY_COUNT * \
                                         (MODBUS_UART_TX_TIMEOUT_MS + MODBUS_RTU_RESP_TIMEOUT_MS)) + \
                                        ((MODBUS_RETRY_COUNT > 1U) ? \
                                         ((MODBUS_RETRY_COUNT - 1U) * MODBUS_RETRY_BACKOFF_MS) : 0U))
#define MODBUS_WDG_CYCLE_BUDGET_MS    ((MODBUS_MAX_SLAVES * \
                                         ((2U * MODBUS_WDG_REQ_BUDGET_MS) + MODBUS_INTER_SLAVE_DELAY_MS)) + 5000U)
#define WDG_TIMEOUT_MODBUS_MS         MODBUS_WDG_CYCLE_BUDGET_MS
#define WDG_TIMEOUT_CONFIG_MS         5000U
#define WDG_TIMEOUT_TCP_MS            5000U

#define CONFIG_PAYLOAD_SIZE           128U
#define CONFIG_VALID_MARKER           0xA55A5AA5UL
#define CONFIG_SLOT_A_ADDR            0x08040000UL
#define CONFIG_SLOT_B_ADDR            0x08060000UL
#define CONFIG_SLOT_A_SECTOR          FLASH_SECTOR_6
#define CONFIG_SLOT_B_SECTOR          FLASH_SECTOR_7
#define CONFIG_FLASH_WRITE_RETRIES    3U
#define CONFIG_FLASH_RETRY_DELAY_MS   20U
#define CONFIG_APPLY_QUEUE_RETRIES    3U
#define CONFIG_APPLY_QUEUE_DELAY_MS   20U

#define EVENT_CODE_LINK_DOWN          1000U
#define EVENT_CODE_LINK_UP            1001U
#define EVENT_CODE_CFG_APPLIED        1100U
#define EVENT_CODE_CFG_REJECTED       1101U
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

typedef enum
{
  CFG_RESULT_IDLE = 0U,
  CFG_RESULT_QUEUED = 1U,
  CFG_RESULT_APPLIED = 2U,
  CFG_RESULT_REJECT_BAD_VERSION = 10U,
  CFG_RESULT_REJECT_BAD_CRC = 11U,
  CFG_RESULT_REJECT_RANGE = 12U,
  CFG_RESULT_REJECT_QUEUE_FULL = 13U,
  CFG_RESULT_FLASH_FAIL = 14U,
  CFG_RESULT_APPLY_QUEUE_FAIL = 15U,
  CFG_RESULT_REJECT_TOPOLOGY_SCHEMA = 20U,
  CFG_RESULT_REJECT_TOPOLOGY_BOUNDS = 21U,
  CFG_RESULT_REJECT_TOPOLOGY_CRC = 22U,
  CFG_RESULT_REJECT_TOPOLOGY_COLLISION = 23U,
  CFG_RESULT_REJECT_TOPOLOGY_BUDGET = 24U
} config_result_code_t;

typedef struct
{
  float value;
  uint8_t quality;
  uint32_t timestamp_ms;
} sensor_state_t;

typedef struct
{
  uint16_t request_token;
  uint16_t reserved0;
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
  uint16_t stack_hwm_control_words;
  uint16_t stack_hwm_modbus_words;
  uint16_t stack_hwm_config_words;
  uint16_t stack_hwm_tcp_words;
  uint16_t stack_hwm_wdg_words;
  uint16_t reserved1;
  uint32_t heap_free_bytes;
  uint32_t heap_min_ever_bytes;
} status_payload_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t crc;
} active_config_t;

typedef struct
{
  uint16_t request_token;
  uint16_t reserved0;
  active_config_t config;
} config_apply_req_t;

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
  TASK_BIT_WDG = (1UL << 3),
  TASK_BIT_TCP = (1UL << 4)
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
extern volatile uint32_t g_persist_boot_count;
extern volatile uint32_t g_persist_poweron_count;
extern volatile uint32_t g_persist_error_handler_count;
extern volatile uint32_t g_persist_wdg_miss_count;
extern volatile uint32_t g_persist_fault_reset_count;
extern volatile uint32_t g_persist_last_event_code;
extern volatile uint32_t g_persist_last_reset_reason;
extern volatile uint32_t g_eth_diag_phy_addr;
extern volatile int32_t g_eth_diag_phy_link_state;
extern volatile uint32_t g_eth_diag_phy_scan_ok;
extern volatile uint32_t g_eth_diag_rx_sem_ok;
extern volatile uint32_t g_eth_diag_tx_sem_ok;
extern volatile uint32_t g_eth_diag_input_task_ok;
extern volatile uint8_t g_topology_v2_active;
extern volatile uint16_t g_topology_v2_ver_major;
extern volatile uint16_t g_topology_v2_ver_minor;
extern volatile uint32_t g_topology_v2_generation;
extern volatile uint16_t g_topology_v2_module_count;
extern volatile uint16_t g_topology_v2_req_count;
extern volatile uint16_t g_topology_v2_point_count;
extern volatile uint16_t g_topology_v2_cmd_count;
extern volatile uint16_t g_topology_v2_policy_count;

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
