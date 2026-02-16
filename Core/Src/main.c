/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "gh_net_adapter.h"
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define RTC_BKP_INIT_MARKER 0x32F2U

#define SENSOR_COUNT                  150U
#define MODBUS_MAX_SLAVES             12U
#define MODBUS_MAX_SENSORS_PER_SLAVE  12U
#define BLOCK_CHANNEL_COUNT           6U
#define EVENT_BUFFER_SIZE             512U
#define PROTO_MAGIC                   0xA55AU
#define PROTO_VERSION                 1U
#define PROTO_PORT                    5000U
#define PROTO_MAX_PAYLOAD             800U
#define SNAPSHOT_PERIOD_MS            5000U
#define HEARTBEAT_PERIOD_MS           1000U
#define EVENT_ACK_TIMEOUT_MS          2000U
#define EVENT_RETRY_MAX_MS            60000U

#define CONFIG_PAYLOAD_SIZE           128U
#define CONFIG_VALID_MARKER           0xA55A5AA5UL
#define CONFIG_SLOT_A_ADDR            0x08040000UL /* Sector 6, STM32F407VE */
#define CONFIG_SLOT_B_ADDR            0x08060000UL /* Sector 7, STM32F407VE */
#define CONFIG_SLOT_A_SECTOR          FLASH_SECTOR_6
#define CONFIG_SLOT_B_SECTOR          FLASH_SECTOR_7

#define EVENT_CODE_LINK_DOWN          1000U
#define EVENT_CODE_LINK_UP            1001U
#define EVENT_CODE_BAD_FRAME          1002U
#define EVENT_CODE_CFG_APPLIED        1100U
#define EVENT_CODE_CFG_REJECTED       1101U
#define EVENT_CODE_WDG_MISS           1200U

#define RS485_DE_RE_PORT              GPIOD
#define RS485_DE_RE_PIN               GPIO_PIN_7
#define MODBUS_FUNC_READ_HOLDING      0x03U
#define MODBUS_MAX_REGS_PER_REQ       32U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RTC_HandleTypeDef hrtc;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

osThreadId_t controlTaskHandle;
osThreadId_t modbusMasterTaskHandle;
osThreadId_t netServerTaskHandle;
osThreadId_t telemetryTaskHandle;
osThreadId_t eventTaskHandle;
osThreadId_t configStorageTaskHandle;
osThreadId_t healthWatchdogTaskHandle;

static const osThreadAttr_t controlTask_attributes = {
  .name = "ControlTask",
  .stack_size = 768U * 4U,
  .priority = (osPriority_t)osPriorityHigh,
};

static const osThreadAttr_t modbusMasterTask_attributes = {
  .name = "ModbusMasterTask",
  .stack_size = 768U * 4U,
  .priority = (osPriority_t)osPriorityAboveNormal,
};

static const osThreadAttr_t netServerTask_attributes = {
  .name = "NetServerTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t telemetryTask_attributes = {
  .name = "TelemetryTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t eventTask_attributes = {
  .name = "EventTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityAboveNormal,
};

static const osThreadAttr_t configStorageTask_attributes = {
  .name = "ConfigStorageTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityBelowNormal,
};

static const osThreadAttr_t healthWatchdogTask_attributes = {
  .name = "HealthWatchdogTask",
  .stack_size = 512U * 4U,
  .priority = (osPriority_t)osPriorityLow,
};

osMessageQueueId_t qEventsHandle;
osMessageQueueId_t qNetTxHiHandle;
osMessageQueueId_t qNetTxLoHandle;
osMessageQueueId_t qConfigApplyHandle;
osMessageQueueId_t qConfigStoreHandle;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RTC_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */

void StartControlTask(void *argument);
void StartModbusMasterTask(void *argument);
void StartNetServerTask(void *argument);
void StartTelemetryTask(void *argument);
void StartEventTask(void *argument);
void StartConfigStorageTask(void *argument);
void StartHealthWatchdogTask(void *argument);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MSG_HELLO = 1U,
  MSG_HELLO_ACK = 2U,
  MSG_HEARTBEAT = 3U,
  MSG_STATUS_REQ = 4U,
  MSG_STATUS_RESP = 5U,
  MSG_SNAPSHOT = 6U,
  MSG_EVENT = 7U,
  MSG_EVENT_ACK = 8U,
  MSG_GET_CONFIG_REQ = 9U,
  MSG_GET_CONFIG_RESP = 10U,
  MSG_SETPOINTS_PUT = 11U,
  MSG_SETPOINTS_VALIDATE_ACK = 12U,
  MSG_SETPOINTS_APPLY_REQ = 13U,
  MSG_SETPOINTS_APPLY_ACK = 14U,
  MSG_GET_BLOCK_LAYOUT_REQ = 15U,
  MSG_BLOCK_LAYOUT_RESP = 16U
} msg_type_t;

typedef enum
{
  VALIDATE_OK = 0U,
  VALIDATE_ERR_LEN = 1U,
  VALIDATE_ERR_CRC = 2U,
  VALIDATE_ERR_RANGE = 3U,
  VALIDATE_ERR_BUSY = 4U
} validate_code_t;

typedef enum
{
  APPLY_APPLIED = 0U,
  APPLY_FAILED = 1U
} apply_code_t;

typedef struct __attribute__((packed))
{
  uint16_t magic;
  uint8_t proto_ver;
  uint8_t msg_type;
  uint32_t seq;
  uint16_t len;
  uint32_t crc32;
} proto_header_t;

typedef struct
{
  float value;
  uint8_t quality;
  uint32_t timestamp_ms;
} sensor_state_t;

typedef struct
{
  uint32_t event_id;
  uint8_t severity;
  uint16_t code;
  uint16_t source;
  float value;
  uint32_t timestamp_ms;
  uint8_t acked;
  uint32_t retry_ms;
  uint32_t next_retry_ms;
  uint32_t resend_count;
} event_record_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t payload_crc;
} config_update_req_t;

typedef struct
{
  uint8_t slave_id;
  uint8_t block_no;      /* Logical greenhouse block number: 1..12 */
  uint16_t start_reg;
  uint16_t sensor_count; /* Number of active channels read from this block */
  uint16_t sensor_base;
} modbus_sensor_map_t;

typedef enum
{
  BLOCK_CH_AIR_TEMP = 0U,
  BLOCK_CH_AIR_HUM = 1U,
  BLOCK_CH_WATER_RAIL = 2U,
  BLOCK_CH_WATER_GROW = 3U,
  BLOCK_CH_WATER_UNDERTRAY = 4U,
  BLOCK_CH_WINDOW_POS = 5U
} block_channel_t;

typedef struct
{
  uint8_t msg_type;
  uint16_t len;
  uint8_t payload[PROTO_MAX_PAYLOAD];
} net_tx_item_t;

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
  uint32_t modbus_timeouts[MODBUS_MAX_SLAVES];
  uint32_t flash_write_ok_count;
  uint32_t flash_write_fail_count;
  uint32_t last_error_code;
} status_payload_t;

typedef struct __attribute__((packed))
{
  uint32_t snapshot_id;
  uint32_t timestamp_ms;
  float values[SENSOR_COUNT];
  uint8_t quality[SENSOR_COUNT];
} snapshot_payload_t;

typedef struct __attribute__((packed))
{
  uint32_t event_id;
  uint8_t severity;
  uint16_t code;
  uint16_t source;
  float value;
  uint32_t timestamp_ms;
} event_payload_t;

typedef struct __attribute__((packed))
{
  uint32_t version;
  uint32_t payload_crc;
} setpoints_put_hdr_t;

typedef struct __attribute__((packed))
{
  uint8_t result;
} validate_ack_payload_t;

typedef struct __attribute__((packed))
{
  uint8_t result;
  uint32_t active_version;
} apply_ack_payload_t;

typedef struct __attribute__((packed))
{
  uint8_t build_id[16];
  uint32_t uptime_ms;
  uint32_t active_config_version;
  uint32_t last_event_id;
} hello_ack_payload_t;

typedef struct __attribute__((packed))
{
  uint32_t version;
  uint32_t len;
  uint32_t crc;
  uint32_t seq;
  uint32_t valid_marker;
} config_slot_header_t;

typedef struct __attribute__((packed))
{
  uint8_t block_no;
  uint8_t slave_id;
  uint16_t start_reg;
  uint16_t sensor_count;
  uint16_t sensor_base;
} block_layout_item_t;

typedef struct __attribute__((packed))
{
  uint8_t channels_per_block;
  uint8_t item_count;
  uint16_t reserved;
  block_layout_item_t items[MODBUS_MAX_SLAVES];
} block_layout_payload_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t crc;
} active_config_t;

typedef struct
{
  uint32_t last_event_id_seen;
} hello_payload_t;

typedef enum
{
  TASK_BIT_CONTROL = (1UL << 0),
  TASK_BIT_MODBUS = (1UL << 1),
  TASK_BIT_NET = (1UL << 2),
  TASK_BIT_TELEMETRY = (1UL << 3),
  TASK_BIT_EVENT = (1UL << 4),
  TASK_BIT_CONFIG = (1UL << 5),
  TASK_BIT_WDG = (1UL << 6)
} task_heartbeat_bit_t;

static sensor_state_t g_sensors[SENSOR_COUNT];
static event_record_t g_events[EVENT_BUFFER_SIZE];
static uint32_t g_event_head = 0U;
static uint32_t g_event_count = 0U;
static uint32_t g_next_event_id = 1U;
static uint32_t g_next_snapshot_id = 1U;
static uint32_t g_seq_tx = 1U;

static uint32_t g_watchdog_flags = 0U;
static const uint32_t g_watchdog_all_mask =
  TASK_BIT_CONTROL | TASK_BIT_MODBUS | TASK_BIT_NET | TASK_BIT_TELEMETRY |
  TASK_BIT_EVENT | TASK_BIT_CONFIG;

static status_payload_t g_status = {0};
static active_config_t g_active_config = {0};
static uint32_t g_config_seq = 1U;

static bool g_client_connected = false;
static uint32_t g_last_rx_ms = 0U;
static uint32_t g_last_event_id_seen = 0U;
static void enqueue_block_layout_response(void);

static const uint32_t kEventRetryStepsMs[] = {1000U, 2000U, 5000U, 10000U, 20000U, 30000U, 60000U};

/* Slave polling table:
 * {slave_id, block_no, start_register, sensor_count, global_sensor_base}
 * Channel order in each block:
 * 0 AIR_TEMP, 1 AIR_HUM, 2 WATER_RAIL, 3 WATER_GROW, 4 WATER_UNDERTRAY, 5 WINDOW_POS.
 * Current plant: one block with three active channels (indices 0..2).
 * Future-ready: up to 12 slaves, up to 12 sensors on each slave.
 */
static const modbus_sensor_map_t kModbusMap[] =
{
  {1U, 1U, 0U, 3U, 0U}
};

static uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  uint8_t j;

  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (j = 0U; j < 8U; j++)
    {
      if ((crc & 0x0001U) != 0U)
      {
        crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint8_t bit;
  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1UL) != 0UL)
      {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return ~crc;
}

static void task_heartbeat_kick(task_heartbeat_bit_t bit)
{
  taskENTER_CRITICAL();
  g_watchdog_flags |= (uint32_t)bit;
  taskEXIT_CRITICAL();
}

static bool float_in_range(float v)
{
  return isfinite(v) && (v >= -100.0f) && (v <= 1000.0f);
}

static void rs485_set_tx(bool tx_en)
{
  HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, tx_en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void uart_drain_rx(UART_HandleTypeDef *huart)
{
  uint8_t b;
  while (HAL_UART_Receive(huart, &b, 1U, 1U) == HAL_OK)
  {
    /* Drain stale bytes to keep Modbus frame boundaries clean. */
  }
}

static bool modbus_read_holding_registers(uint8_t slave_id,
                                          uint16_t start_reg,
                                          uint16_t reg_count,
                                          uint16_t *out_regs)
{
  uint8_t req[8];
  uint8_t resp[5U + (MODBUS_MAX_REGS_PER_REQ * 2U)];
  uint16_t req_crc;
  uint16_t resp_crc;
  uint16_t exp_len;
  uint16_t i;

  if ((reg_count == 0U) || (reg_count > MODBUS_MAX_REGS_PER_REQ))
  {
    return false;
  }

  req[0] = slave_id;
  req[1] = MODBUS_FUNC_READ_HOLDING;
  req[2] = (uint8_t)(start_reg >> 8U);
  req[3] = (uint8_t)(start_reg & 0xFFU);
  req[4] = (uint8_t)(reg_count >> 8U);
  req[5] = (uint8_t)(reg_count & 0xFFU);
  req_crc = modbus_crc16(req, 6U);
  req[6] = (uint8_t)(req_crc & 0xFFU);
  req[7] = (uint8_t)(req_crc >> 8U);

  uart_drain_rx(&huart2);
  rs485_set_tx(true);
  if (HAL_UART_Transmit(&huart2, req, sizeof(req), 100U) != HAL_OK)
  {
    rs485_set_tx(false);
    return false;
  }
  rs485_set_tx(false);

  exp_len = (uint16_t)(5U + (reg_count * 2U));
  if (HAL_UART_Receive(&huart2, resp, exp_len, 250U) != HAL_OK)
  {
    return false;
  }

  if ((resp[0] != slave_id) || (resp[1] != MODBUS_FUNC_READ_HOLDING) || (resp[2] != (uint8_t)(reg_count * 2U)))
  {
    return false;
  }

  resp_crc = (uint16_t)resp[exp_len - 2U] | ((uint16_t)resp[exp_len - 1U] << 8U);
  if (modbus_crc16(resp, (uint16_t)(exp_len - 2U)) != resp_crc)
  {
    return false;
  }

  for (i = 0U; i < reg_count; i++)
  {
    out_regs[i] = (uint16_t)(((uint16_t)resp[3U + (2U * i)] << 8U) |
                              (uint16_t)resp[3U + (2U * i) + 1U]);
  }
  return true;
}

static void sensors_init_defaults(void)
{
  uint16_t i;
  uint32_t now = HAL_GetTick();
  for (i = 0U; i < SENSOR_COUNT; i++)
  {
    g_sensors[i].value = 0.0f;
    g_sensors[i].quality = SENSOR_QUALITY_OFFLINE;
    g_sensors[i].timestamp_ms = now;
  }
}

static bool flash_write_words(uint32_t addr, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t word;
  HAL_StatusTypeDef st;

  for (i = 0U; i < len; i += 4U)
  {
    word = 0xFFFFFFFFUL;
    memcpy(&word, &data[i], ((len - i) >= 4U) ? 4U : (len - i));
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
    if (st != HAL_OK)
    {
      return false;
    }
  }
  return true;
}

static bool config_slot_read(uint32_t slot_addr, active_config_t *out_cfg)
{
  const config_slot_header_t *hdr = (const config_slot_header_t *)slot_addr;
  const uint8_t *payload = (const uint8_t *)(slot_addr + sizeof(config_slot_header_t));

  if (hdr->valid_marker != CONFIG_VALID_MARKER)
  {
    return false;
  }
  if (hdr->len != CONFIG_PAYLOAD_SIZE)
  {
    return false;
  }
  if (crc32_compute(payload, hdr->len) != hdr->crc)
  {
    return false;
  }

  out_cfg->version = hdr->version;
  out_cfg->crc = hdr->crc;
  memcpy(out_cfg->payload, payload, CONFIG_PAYLOAD_SIZE);
  return true;
}

static bool config_write_to_slot(uint32_t slot_addr, uint32_t sector, const active_config_t *cfg)
{
  config_slot_header_t hdr = {0};
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t erase_err = 0U;
  bool ok;

  hdr.version = cfg->version;
  hdr.len = CONFIG_PAYLOAD_SIZE;
  hdr.crc = cfg->crc;
  hdr.seq = g_config_seq++;
  hdr.valid_marker = 0xFFFFFFFFUL;

  HAL_FLASH_Unlock();

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = sector;
  erase.NbSectors = 1U;
  if (HAL_FLASHEx_Erase(&erase, &erase_err) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return false;
  }

  ok = flash_write_words(slot_addr, (const uint8_t *)&hdr, sizeof(hdr));
  ok = ok && flash_write_words(slot_addr + sizeof(hdr), cfg->payload, CONFIG_PAYLOAD_SIZE);
  if (ok)
  {
    const uint32_t marker = CONFIG_VALID_MARKER;
    ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                            slot_addr + offsetof(config_slot_header_t, valid_marker),
                            marker) == HAL_OK);
  }

  HAL_FLASH_Lock();
  return ok;
}

static void config_load_or_default(void)
{
  active_config_t cfg_a = {0};
  active_config_t cfg_b = {0};
  bool valid_a = config_slot_read(CONFIG_SLOT_A_ADDR, &cfg_a);
  bool valid_b = config_slot_read(CONFIG_SLOT_B_ADDR, &cfg_b);
  uint32_t i;

  if (valid_a && valid_b)
  {
    g_active_config = (cfg_b.version >= cfg_a.version) ? cfg_b : cfg_a;
  }
  else if (valid_a)
  {
    g_active_config = cfg_a;
  }
  else if (valid_b)
  {
    g_active_config = cfg_b;
  }
  else
  {
    g_active_config.version = 1U;
    for (i = 0U; i < CONFIG_PAYLOAD_SIZE; i++)
    {
      g_active_config.payload[i] = 0U;
    }
    g_active_config.crc = crc32_compute(g_active_config.payload, CONFIG_PAYLOAD_SIZE);
  }
}

static validate_code_t validate_config_payload(const uint8_t *payload, uint16_t len, uint32_t expected_crc)
{
  uint32_t idx;
  float v;

  if (len != CONFIG_PAYLOAD_SIZE)
  {
    return VALIDATE_ERR_LEN;
  }
  if (crc32_compute(payload, len) != expected_crc)
  {
    return VALIDATE_ERR_CRC;
  }
  for (idx = 0U; idx < CONFIG_PAYLOAD_SIZE; idx += sizeof(float))
  {
    memcpy(&v, &payload[idx], sizeof(float));
    if (!float_in_range(v))
    {
      return VALIDATE_ERR_RANGE;
    }
  }
  return VALIDATE_OK;
}

static bool enqueue_tx(uint8_t msg_type, const uint8_t *payload, uint16_t len, bool high_prio)
{
  net_tx_item_t item = {0};
  if (len > PROTO_MAX_PAYLOAD)
  {
    return false;
  }
  item.msg_type = msg_type;
  item.len = len;
  if ((payload != NULL) && (len > 0U))
  {
    memcpy(item.payload, payload, len);
  }
  return osMessageQueuePut(high_prio ? qNetTxHiHandle : qNetTxLoHandle, &item, 0U, 0U) == osOK;
}

static bool net_send_frame(uint8_t msg_type, const uint8_t *payload, uint16_t len)
{
  proto_header_t hdr = {0};
  uint8_t frame[sizeof(proto_header_t) + PROTO_MAX_PAYLOAD];

  if (len > PROTO_MAX_PAYLOAD)
  {
    g_status.crc_errors_tx++;
    return false;
  }

  hdr.magic = PROTO_MAGIC;
  hdr.proto_ver = PROTO_VERSION;
  hdr.msg_type = msg_type;
  hdr.seq = g_seq_tx++;
  hdr.len = len;
  hdr.crc32 = (payload != NULL) ? crc32_compute(payload, len) : 0U;

  memcpy(frame, &hdr, sizeof(hdr));
  if ((payload != NULL) && (len > 0U))
  {
    memcpy(&frame[sizeof(hdr)], payload, len);
  }
  return NetAdapter_Send(frame, (uint16_t)(sizeof(hdr) + len));
}

static void publish_event(uint8_t severity, uint16_t code, uint16_t source, float value)
{
  event_payload_t ev = {0};
  ev.event_id = g_next_event_id++;
  ev.severity = severity;
  ev.code = code;
  ev.source = source;
  ev.value = value;
  ev.timestamp_ms = HAL_GetTick();

  (void)osMessageQueuePut(qEventsHandle, &ev, 0U, 0U);
  g_status.events_generated_count++;
}

static void event_store(const event_payload_t *ev)
{
  event_record_t *slot;
  uint32_t idx = (g_event_head + g_event_count) % EVENT_BUFFER_SIZE;
  if (g_event_count == EVENT_BUFFER_SIZE)
  {
    g_event_head = (g_event_head + 1U) % EVENT_BUFFER_SIZE;
    idx = (g_event_head + g_event_count - 1U) % EVENT_BUFFER_SIZE;
  }
  else
  {
    g_event_count++;
  }

  slot = &g_events[idx];
  slot->event_id = ev->event_id;
  slot->severity = ev->severity;
  slot->code = ev->code;
  slot->source = ev->source;
  slot->value = ev->value;
  slot->timestamp_ms = ev->timestamp_ms;
  slot->acked = 0U;
  slot->retry_ms = EVENT_ACK_TIMEOUT_MS;
  slot->next_retry_ms = HAL_GetTick() + EVENT_ACK_TIMEOUT_MS;
  slot->resend_count = 0U;
}

static event_record_t *event_find(uint32_t event_id)
{
  uint32_t i;
  uint32_t idx;
  for (i = 0U; i < g_event_count; i++)
  {
    idx = (g_event_head + i) % EVENT_BUFFER_SIZE;
    if (g_events[idx].event_id == event_id)
    {
      return &g_events[idx];
    }
  }
  return NULL;
}

static void event_ack(uint32_t event_id)
{
  event_record_t *ev = event_find(event_id);
  if ((ev != NULL) && (ev->acked == 0U))
  {
    ev->acked = 1U;
    g_status.events_acked_count++;
  }
}

static void send_event_record(const event_record_t *ev, bool resend)
{
  event_payload_t payload = {0};
  payload.event_id = ev->event_id;
  payload.severity = ev->severity;
  payload.code = ev->code;
  payload.source = ev->source;
  payload.value = ev->value;
  payload.timestamp_ms = ev->timestamp_ms;
  (void)enqueue_tx(MSG_EVENT, (const uint8_t *)&payload, sizeof(payload), true);
  if (resend)
  {
    g_status.events_resent_count++;
  }
  else
  {
    g_status.events_sent_count++;
  }
}

static void process_rx_message(uint8_t msg_type, const uint8_t *payload, uint16_t len)
{
  hello_ack_payload_t hello_ack = {0};
  status_payload_t status = {0};
  validate_ack_payload_t vack = {0};
  config_update_req_t req = {0};
  apply_ack_payload_t aack = {0};
  active_config_t tmp_cfg = {0};
  validate_code_t vrc;
  setpoints_put_hdr_t put_hdr = {0};

  g_last_rx_ms = HAL_GetTick();
  switch (msg_type)
  {
    case MSG_HELLO:
      if (len >= sizeof(hello_payload_t))
      {
        hello_payload_t h = {0};
        memcpy(&h, payload, sizeof(h));
        g_last_event_id_seen = h.last_event_id_seen;
      }
      memset(&hello_ack, 0, sizeof(hello_ack));
      memcpy(hello_ack.build_id, "F407-MASTER-1.0", 15U);
      hello_ack.uptime_ms = HAL_GetTick();
      hello_ack.active_config_version = g_active_config.version;
      hello_ack.last_event_id = g_next_event_id - 1U;
      (void)enqueue_tx(MSG_HELLO_ACK, (const uint8_t *)&hello_ack, sizeof(hello_ack), true);
      break;

    case MSG_HEARTBEAT:
      break;

    case MSG_STATUS_REQ:
      status = g_status;
      (void)enqueue_tx(MSG_STATUS_RESP, (const uint8_t *)&status, sizeof(status), true);
      break;

    case MSG_GET_CONFIG_REQ:
      (void)enqueue_tx(MSG_GET_CONFIG_RESP, g_active_config.payload, CONFIG_PAYLOAD_SIZE, true);
      break;

    case MSG_GET_BLOCK_LAYOUT_REQ:
      enqueue_block_layout_response();
      break;

    case MSG_EVENT_ACK:
      if (len == sizeof(uint32_t))
      {
        uint32_t id = 0U;
        memcpy(&id, payload, sizeof(id));
        event_ack(id);
      }
      break;

    case MSG_SETPOINTS_PUT:
      if (len < sizeof(setpoints_put_hdr_t))
      {
        vack.result = VALIDATE_ERR_LEN;
        (void)enqueue_tx(MSG_SETPOINTS_VALIDATE_ACK, (const uint8_t *)&vack, sizeof(vack), true);
        break;
      }

      memcpy(&put_hdr, payload, sizeof(put_hdr));
      vrc = validate_config_payload(&payload[sizeof(put_hdr)],
                                    (uint16_t)(len - sizeof(put_hdr)),
                                    put_hdr.payload_crc);

      vack.result = (uint8_t)vrc;
      (void)enqueue_tx(MSG_SETPOINTS_VALIDATE_ACK, (const uint8_t *)&vack, sizeof(vack), true);
      if (vrc != VALIDATE_OK)
      {
        publish_event(EVENT_SEV_WARN, EVENT_CODE_CFG_REJECTED, 0U, (float)vrc);
        break;
      }

      req.version = put_hdr.version;
      req.payload_crc = put_hdr.payload_crc;
      memcpy(req.payload, &payload[sizeof(put_hdr)], CONFIG_PAYLOAD_SIZE);
      if (osMessageQueuePut(qConfigStoreHandle, &req, 0U, 0U) != osOK)
      {
        vack.result = VALIDATE_ERR_BUSY;
        (void)enqueue_tx(MSG_SETPOINTS_VALIDATE_ACK, (const uint8_t *)&vack, sizeof(vack), true);
      }
      break;

    case MSG_SETPOINTS_APPLY_REQ:
      tmp_cfg = g_active_config;
      aack.result = APPLY_APPLIED;
      aack.active_version = tmp_cfg.version;
      (void)enqueue_tx(MSG_SETPOINTS_APPLY_ACK, (const uint8_t *)&aack, sizeof(aack), true);
      break;

    default:
      g_status.last_error_code = EVENT_CODE_BAD_FRAME;
      publish_event(EVENT_SEV_WARN, EVENT_CODE_BAD_FRAME, 0U, (float)msg_type);
      break;
  }
}

static void process_rx_stream(const uint8_t *data, uint16_t len)
{
  static uint8_t rxbuf[sizeof(proto_header_t) + PROTO_MAX_PAYLOAD];
  static uint16_t rxlen = 0U;
  proto_header_t hdr = {0};
  uint32_t calc_crc;

  if ((len + rxlen) > sizeof(rxbuf))
  {
    rxlen = 0U;
    g_status.last_error_code = EVENT_CODE_BAD_FRAME;
    return;
  }

  memcpy(&rxbuf[rxlen], data, len);
  rxlen += len;

  while (rxlen >= sizeof(proto_header_t))
  {
    memcpy(&hdr, rxbuf, sizeof(hdr));
    if ((hdr.magic != PROTO_MAGIC) || (hdr.proto_ver != PROTO_VERSION) || (hdr.len > PROTO_MAX_PAYLOAD))
    {
      rxlen = 0U;
      g_status.crc_errors_rx++;
      break;
    }
    if (rxlen < (uint16_t)(sizeof(proto_header_t) + hdr.len))
    {
      break;
    }

    calc_crc = crc32_compute(&rxbuf[sizeof(proto_header_t)], hdr.len);
    if (calc_crc == hdr.crc32)
    {
      process_rx_message(hdr.msg_type, &rxbuf[sizeof(proto_header_t)], hdr.len);
    }
    else
    {
      g_status.crc_errors_rx++;
    }

    memmove(rxbuf, &rxbuf[sizeof(proto_header_t) + hdr.len], rxlen - (uint16_t)(sizeof(proto_header_t) + hdr.len));
    rxlen = (uint16_t)(rxlen - (sizeof(proto_header_t) + hdr.len));
  }
}

static void send_events_backlog(uint32_t last_seen)
{
  uint32_t i;
  uint32_t idx;
  for (i = 0U; i < g_event_count; i++)
  {
    idx = (g_event_head + i) % EVENT_BUFFER_SIZE;
    if (g_events[idx].event_id > last_seen)
    {
      send_event_record(&g_events[idx], false);
    }
  }
}

static void enqueue_block_layout_response(void)
{
  block_layout_payload_t pl = {0};
  uint32_t i;
  uint32_t item_count = sizeof(kModbusMap) / sizeof(kModbusMap[0]);

  if (item_count > MODBUS_MAX_SLAVES)
  {
    item_count = MODBUS_MAX_SLAVES;
  }

  pl.channels_per_block = BLOCK_CHANNEL_COUNT;
  pl.item_count = (uint8_t)item_count;
  pl.reserved = 0U;
  for (i = 0U; i < item_count; i++)
  {
    pl.items[i].block_no = kModbusMap[i].block_no;
    pl.items[i].slave_id = kModbusMap[i].slave_id;
    pl.items[i].start_reg = kModbusMap[i].start_reg;
    pl.items[i].sensor_count = kModbusMap[i].sensor_count;
    pl.items[i].sensor_base = kModbusMap[i].sensor_base;
  }

  (void)enqueue_tx(MSG_BLOCK_LAYOUT_RESP, (const uint8_t *)&pl, sizeof(pl), true);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  sensors_init_defaults();
  config_load_or_default();
  g_last_rx_ms = HAL_GetTick();

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  qEventsHandle = osMessageQueueNew(64U, sizeof(event_payload_t), NULL);
  qNetTxHiHandle = osMessageQueueNew(4U, sizeof(net_tx_item_t), NULL);
  qNetTxLoHandle = osMessageQueueNew(1U, sizeof(net_tx_item_t), NULL);
  qConfigApplyHandle = osMessageQueueNew(4U, sizeof(active_config_t), NULL);
  qConfigStoreHandle = osMessageQueueNew(4U, sizeof(config_update_req_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  controlTaskHandle = osThreadNew(StartControlTask, NULL, &controlTask_attributes);
  modbusMasterTaskHandle = osThreadNew(StartModbusMasterTask, NULL, &modbusMasterTask_attributes);
  netServerTaskHandle = osThreadNew(StartNetServerTask, NULL, &netServerTask_attributes);
  telemetryTaskHandle = osThreadNew(StartTelemetryTask, NULL, &telemetryTask_attributes);
  eventTaskHandle = osThreadNew(StartEventTask, NULL, &eventTask_attributes);
  configStorageTaskHandle = osThreadNew(StartConfigStorageTask, NULL, &configStorageTask_attributes);
  healthWatchdogTaskHandle = osThreadNew(StartHealthWatchdogTask, NULL, &healthWatchdogTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  HAL_PWR_EnableBkUpAccess();
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) != RTC_BKP_INIT_MARKER)
  {
    sTime.Hours = 0;
    sTime.Minutes = 0;
    sTime.Seconds = 0;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
    {
      Error_Handler();
    }

    sDate.WeekDay = RTC_WEEKDAY_THURSDAY;
    sDate.Month = RTC_MONTH_JANUARY;
    sDate.Date = 1;
    sDate.Year = 26;
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
    {
      Error_Handler();
    }

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, RTC_BKP_INIT_MARKER);
  }

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin : PD7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void StartControlTask(void *argument)
{
  active_config_t cfg = {0};
  (void)argument;
  for (;;)
  {
    if (osMessageQueueGet(qConfigApplyHandle, &cfg, NULL, 100U) == osOK)
    {
      g_active_config = cfg;
      publish_event(EVENT_SEV_INFO, EVENT_CODE_CFG_APPLIED, 0U, (float)cfg.version);
    }
    task_heartbeat_kick(TASK_BIT_CONTROL);
    osDelay(20U);
  }
}

void StartModbusMasterTask(void *argument)
{
  uint16_t regs[MODBUS_MAX_REGS_PER_REQ];
  uint16_t i;
  uint16_t s;
  uint16_t global_idx;
  uint32_t now;
  bool ok;
  const modbus_sensor_map_t *m;
  (void)argument;

  for (;;)
  {
    now = HAL_GetTick();
    for (s = 0U; s < (sizeof(kModbusMap) / sizeof(kModbusMap[0])); s++)
    {
      m = &kModbusMap[s];
      if ((m->sensor_count == 0U) || (m->sensor_count > MODBUS_MAX_SENSORS_PER_SLAVE))
      {
        continue;
      }
      ok = modbus_read_holding_registers(m->slave_id, m->start_reg, m->sensor_count, regs);
      if (ok)
      {
        for (i = 0U; i < m->sensor_count; i++)
        {
          global_idx = (uint16_t)(m->sensor_base + i);
          if (global_idx < SENSOR_COUNT)
          {
            g_sensors[global_idx].value = ((float)(int16_t)regs[i]) / 10.0f;
            g_sensors[global_idx].quality = SENSOR_QUALITY_OK;
            g_sensors[global_idx].timestamp_ms = now;
          }
        }
        /* Channels not present yet on this block are marked OFFLINE for UI clarity. */
        if (m->sensor_count < BLOCK_CHANNEL_COUNT)
        {
          for (i = m->sensor_count; i < BLOCK_CHANNEL_COUNT; i++)
          {
            global_idx = (uint16_t)(m->sensor_base + i);
            if (global_idx < SENSOR_COUNT)
            {
              g_sensors[global_idx].quality = SENSOR_QUALITY_OFFLINE;
              g_sensors[global_idx].timestamp_ms = now;
            }
          }
        }
      }
      else
      {
        if (s < (sizeof(g_status.modbus_timeouts) / sizeof(g_status.modbus_timeouts[0])))
        {
          g_status.modbus_timeouts[s]++;
        }
        for (i = 0U; i < m->sensor_count; i++)
        {
          global_idx = (uint16_t)(m->sensor_base + i);
          if (global_idx < SENSOR_COUNT)
          {
            g_sensors[global_idx].quality = SENSOR_QUALITY_STALE;
          }
        }
      }
    }
    task_heartbeat_kick(TASK_BIT_MODBUS);
    osDelay(100U);
  }
}

void StartNetServerTask(void *argument)
{
  uint8_t rx_tmp[256];
  int32_t rx_n;
  uint32_t last_hb = HAL_GetTick();
  net_tx_item_t tx_item = {0};
  osStatus_t qst;
  bool connected_prev = false;
  (void)argument;

  for (;;)
  {
    NetAdapter_ServerPoll();
    g_client_connected = NetAdapter_IsConnected();

    if (g_client_connected && !connected_prev)
    {
      connected_prev = true;
      g_status.link_up_count++;
      g_status.tcp_connect_count++;
      publish_event(EVENT_SEV_INFO, EVENT_CODE_LINK_UP, 0U, 0.0f);
      send_events_backlog(g_last_event_id_seen);
    }
    else if ((!g_client_connected) && connected_prev)
    {
      connected_prev = false;
      g_status.link_down_count++;
      g_status.tcp_disconnect_count++;
      publish_event(EVENT_SEV_WARN, EVENT_CODE_LINK_DOWN, 0U, 0.0f);
    }

    if (g_client_connected)
    {
      if ((HAL_GetTick() - last_hb) >= HEARTBEAT_PERIOD_MS)
      {
        (void)enqueue_tx(MSG_HEARTBEAT, NULL, 0U, true);
        last_hb = HAL_GetTick();
      }

      rx_n = NetAdapter_Recv(rx_tmp, sizeof(rx_tmp), 10U);
      if (rx_n > 0)
      {
        process_rx_stream(rx_tmp, (uint16_t)rx_n);
      }

      if ((HAL_GetTick() - g_last_rx_ms) > (HEARTBEAT_PERIOD_MS * 5U))
      {
        g_client_connected = false;
        NetAdapter_CloseClient();
      }

      qst = osMessageQueueGet(qNetTxHiHandle, &tx_item, NULL, 0U);
      if (qst != osOK)
      {
        qst = osMessageQueueGet(qNetTxLoHandle, &tx_item, NULL, 0U);
      }

      if (qst == osOK)
      {
        (void)net_send_frame(tx_item.msg_type, tx_item.payload, tx_item.len);
        if (tx_item.msg_type == MSG_SNAPSHOT)
        {
          g_status.snapshot_sent_count++;
        }
      }
    }
    else
    {
      osDelay(50U);
    }

    task_heartbeat_kick(TASK_BIT_NET);
    osDelay(5U);
  }
}

void StartTelemetryTask(void *argument)
{
  snapshot_payload_t snap = {0};
  uint16_t i;
  (void)argument;

  for (;;)
  {
    snap.snapshot_id = g_next_snapshot_id++;
    snap.timestamp_ms = HAL_GetTick();
    for (i = 0U; i < SENSOR_COUNT; i++)
    {
      snap.values[i] = g_sensors[i].value;
      snap.quality[i] = g_sensors[i].quality;
    }
    (void)enqueue_tx(MSG_SNAPSHOT, (const uint8_t *)&snap, sizeof(snap), false);
    task_heartbeat_kick(TASK_BIT_TELEMETRY);
    osDelay(SNAPSHOT_PERIOD_MS);
  }
}

void StartEventTask(void *argument)
{
  event_payload_t rx_event = {0};
  uint32_t i;
  uint32_t idx;
  uint32_t now;
  event_record_t *slot;
  uint32_t retry_idx;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qEventsHandle, &rx_event, NULL, 50U) == osOK)
    {
      event_store(&rx_event);
      slot = event_find(rx_event.event_id);
      if (slot != NULL)
      {
        send_event_record(slot, false);
      }
    }

    now = HAL_GetTick();
    for (i = 0U; i < g_event_count; i++)
    {
      idx = (g_event_head + i) % EVENT_BUFFER_SIZE;
      slot = &g_events[idx];
      if ((slot->acked == 0U) && (now >= slot->next_retry_ms))
      {
        send_event_record(slot, true);
        slot->resend_count++;
        retry_idx = slot->resend_count;
        if (retry_idx >= (sizeof(kEventRetryStepsMs) / sizeof(kEventRetryStepsMs[0])))
        {
          retry_idx = (sizeof(kEventRetryStepsMs) / sizeof(kEventRetryStepsMs[0])) - 1U;
        }
        slot->retry_ms = kEventRetryStepsMs[retry_idx];
        if (slot->retry_ms > EVENT_RETRY_MAX_MS)
        {
          slot->retry_ms = EVENT_RETRY_MAX_MS;
        }
        slot->next_retry_ms = now + slot->retry_ms;
      }
    }

    task_heartbeat_kick(TASK_BIT_EVENT);
    osDelay(20U);
  }
}

void StartConfigStorageTask(void *argument)
{
  config_update_req_t req = {0};
  active_config_t pending = {0};
  apply_ack_payload_t ack = {0};
  bool use_slot_a = true;
  bool ok;
  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(qConfigStoreHandle, &req, NULL, 100U) == osOK)
    {
      pending.version = req.version;
      pending.crc = req.payload_crc;
      memcpy(pending.payload, req.payload, CONFIG_PAYLOAD_SIZE);

      ok = use_slot_a ?
           config_write_to_slot(CONFIG_SLOT_A_ADDR, CONFIG_SLOT_A_SECTOR, &pending) :
           config_write_to_slot(CONFIG_SLOT_B_ADDR, CONFIG_SLOT_B_SECTOR, &pending);
      use_slot_a = !use_slot_a;

      if (ok)
      {
        g_status.flash_write_ok_count++;
        (void)osMessageQueuePut(qConfigApplyHandle, &pending, 0U, osWaitForever);
        ack.result = APPLY_APPLIED;
        ack.active_version = pending.version;
      }
      else
      {
        g_status.flash_write_fail_count++;
        ack.result = APPLY_FAILED;
        ack.active_version = g_active_config.version;
      }
      (void)enqueue_tx(MSG_SETPOINTS_APPLY_ACK, (const uint8_t *)&ack, sizeof(ack), true);
    }

    task_heartbeat_kick(TASK_BIT_CONFIG);
    osDelay(20U);
  }
}

void StartHealthWatchdogTask(void *argument)
{
  uint32_t snapshot_flags;
  (void)argument;
  for (;;)
  {
    taskENTER_CRITICAL();
    snapshot_flags = g_watchdog_flags;
    g_watchdog_flags = 0U;
    taskEXIT_CRITICAL();

    if ((snapshot_flags & g_watchdog_all_mask) != g_watchdog_all_mask)
    {
      g_status.last_error_code = EVENT_CODE_WDG_MISS;
      publish_event(EVENT_SEV_CRIT, EVENT_CODE_WDG_MISS, 0U, (float)(snapshot_flags & 0xFFFFU));
    }
    else
    {
      /* Integration point: HAL_IWDG_Refresh(&hiwdg); */
    }
    task_heartbeat_kick(TASK_BIT_WDG);
    osDelay(1000U);
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */
  (void)argument;
  NetAdapter_Init(PROTO_PORT);

  /* Infinite loop */
  for(;;)
  {
    osDelay(1000U);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
