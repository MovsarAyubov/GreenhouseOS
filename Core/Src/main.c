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
#include "gh_crc32.h"
#include "gh_runtime_state.h"
#include "gh_modbus_master.h"
#include "gh_modbus_map.h"
#include "gh_modbus_tcp_server.h"
#include "gh_config_storage.h"
#include "gh_topology_v2.h"
#include "task.h"
#include <string.h>
#include <stddef.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define RTC_BKP_INIT_MARKER 0x32F2U
#define RTC_BKP_RESET_REASON_DR RTC_BKP_DR2
#define RTC_BKP_DIAG_MAGIC_DR RTC_BKP_DR3
#define RTC_BKP_BOOT_COUNT_DR RTC_BKP_DR4
#define RTC_BKP_POWERON_COUNT_DR RTC_BKP_DR5
#define RTC_BKP_ERROR_HANDLER_COUNT_DR RTC_BKP_DR6
#define RTC_BKP_WDG_MISS_COUNT_DR RTC_BKP_DR7
#define RTC_BKP_FAULT_RESET_COUNT_DR RTC_BKP_DR8
#define RTC_BKP_LAST_EVENT_CODE_DR RTC_BKP_DR9
#define RTC_BKP_LAST_RESET_REASON_DR RTC_BKP_DR10
#define RTC_BKP_DIAG_MAGIC 0x47484447UL
#define RESET_REASON_ERROR_HANDLER 0xE001U
#define RESET_REASON_WATCHDOG_MISS 0xE101U
#define RESET_REASON_FAULT_RANGE_MIN 0xE200U
#define RESET_REASON_FAULT_RANGE_MAX 0xE2FFU
#define IWDG_PRESCALER_VALUE 0x06U /* divider 256 */
#define IWDG_RELOAD_VALUE    1000U
#define IWDG_KR_KEY_ENABLE   0xCCCCU
#define IWDG_KR_KEY_RELOAD   0xAAAAU
#define IWDG_KR_KEY_WRITE    0x5555U
#define IWDG_SR_WAIT_MAX_LOOPS 100000U
#if defined(DEBUG)
#define GH_ENABLE_IWDG 0U
#else
#define GH_ENABLE_IWDG 1U
#endif

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
  .stack_size = 512U * 4U,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

osThreadId_t controlTaskHandle;
osThreadId_t modbusMasterTaskHandle;
osThreadId_t modbusTcpServerTaskHandle;
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
  .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t modbusTcpServerTask_attributes = {
  .name = "ModbusTcpServerTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityNormal,
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

osMessageQueueId_t qConfigApplyHandle;
osMessageQueueId_t qConfigStoreHandle;
osMessageQueueId_t qTopologyStoreHandle;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RTC_Init(void);
static void MX_IWDG_Init(void);
static bool iwdg_init(uint8_t prescaler, uint16_t reload);
static void iwdg_refresh(void);
static void persist_diag_bootstrap(void);
static void persist_diag_update_last_event(uint16_t code);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */

void StartControlTask(void *argument);
void StartModbusMasterTask(void *argument);
void StartModbusTcpServerTask(void *argument);
void StartConfigStorageTask(void *argument);
void StartHealthWatchdogTask(void *argument);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

typedef struct __attribute__((packed))
{
  uint32_t version;
  uint32_t len;
  uint32_t crc;
  uint32_t seq;
  uint32_t valid_marker;
} config_slot_header_t;

sensor_state_t g_sensors[SENSOR_COUNT];
uint32_t g_next_event_id = 1U;

static volatile uint32_t g_last_kick_control_ms = 0U;
static volatile uint32_t g_last_kick_modbus_ms = 0U;
static volatile uint32_t g_last_kick_config_ms = 0U;
static volatile uint32_t g_last_kick_tcp_ms = 0U;

status_payload_t g_status = {0};
active_config_t g_active_config = {0};
uint32_t g_config_seq = 1U;
volatile uint32_t g_persist_boot_count = 0U;
volatile uint32_t g_persist_poweron_count = 0U;
volatile uint32_t g_persist_error_handler_count = 0U;
volatile uint32_t g_persist_wdg_miss_count = 0U;
volatile uint32_t g_persist_fault_reset_count = 0U;
volatile uint32_t g_persist_last_event_code = 0U;
volatile uint32_t g_persist_last_reset_reason = 0U;
volatile uint8_t g_topology_v2_active = 0U;
volatile uint16_t g_topology_v2_ver_major = 0U;
volatile uint16_t g_topology_v2_ver_minor = 0U;
volatile uint32_t g_topology_v2_generation = 0U;
volatile uint16_t g_topology_v2_module_count = 0U;
volatile uint16_t g_topology_v2_req_count = 0U;
volatile uint16_t g_topology_v2_point_count = 0U;
volatile uint16_t g_topology_v2_cmd_count = 0U;
volatile uint16_t g_topology_v2_policy_count = 0U;
volatile uint32_t g_topology_v2_active_size = 0U;
volatile uint8_t g_topology_commit_in_progress = 0U;

bool g_setpoints_apply_in_progress = false;
volatile uint8_t g_control_sync_pending = 0U;

void task_heartbeat_kick(task_heartbeat_bit_t bit)
{
  uint32_t now_ms = HAL_GetTick();

  taskENTER_CRITICAL();
  switch (bit)
  {
    case TASK_BIT_CONTROL:
      g_last_kick_control_ms = now_ms;
      break;
    case TASK_BIT_MODBUS:
      g_last_kick_modbus_ms = now_ms;
      break;
    case TASK_BIT_CONFIG:
      g_last_kick_config_ms = now_ms;
      break;
    case TASK_BIT_TCP:
      g_last_kick_tcp_ms = now_ms;
      break;
    case TASK_BIT_WDG:
      break;
    default:
      break;
  }
  taskEXIT_CRITICAL();
}

static void store_reset_reason(uint32_t reason)
{
  RTC_HandleTypeDef rtc = {0};
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTC_ENABLE();
  rtc.Instance = RTC;
  HAL_RTCEx_BKUPWrite(&rtc, RTC_BKP_RESET_REASON_DR, reason);
}

static uint32_t persist_diag_read(uint32_t reg)
{
  return HAL_RTCEx_BKUPRead(&hrtc, reg);
}

static void persist_diag_write(uint32_t reg, uint32_t value)
{
  HAL_RTCEx_BKUPWrite(&hrtc, reg, value);
}

static uint32_t persist_diag_increment(uint32_t reg)
{
  uint32_t value = persist_diag_read(reg);
  value++;
  persist_diag_write(reg, value);
  return value;
}

static void persist_diag_bootstrap(void)
{
  uint32_t reset_reason;
  uint32_t magic;

  HAL_PWR_EnableBkUpAccess();

  magic = persist_diag_read(RTC_BKP_DIAG_MAGIC_DR);
  if (magic != RTC_BKP_DIAG_MAGIC)
  {
    persist_diag_write(RTC_BKP_DIAG_MAGIC_DR, RTC_BKP_DIAG_MAGIC);
    persist_diag_write(RTC_BKP_BOOT_COUNT_DR, 0U);
    persist_diag_write(RTC_BKP_POWERON_COUNT_DR, 0U);
    persist_diag_write(RTC_BKP_ERROR_HANDLER_COUNT_DR, 0U);
    persist_diag_write(RTC_BKP_WDG_MISS_COUNT_DR, 0U);
    persist_diag_write(RTC_BKP_FAULT_RESET_COUNT_DR, 0U);
    persist_diag_write(RTC_BKP_LAST_EVENT_CODE_DR, 0U);
    persist_diag_write(RTC_BKP_LAST_RESET_REASON_DR, 0U);
  }

  g_persist_boot_count = persist_diag_increment(RTC_BKP_BOOT_COUNT_DR);

  reset_reason = persist_diag_read(RTC_BKP_RESET_REASON_DR);
  g_persist_last_reset_reason = reset_reason;
  persist_diag_write(RTC_BKP_LAST_RESET_REASON_DR, reset_reason);

  if (reset_reason == RESET_REASON_WATCHDOG_MISS)
  {
    g_persist_wdg_miss_count = persist_diag_increment(RTC_BKP_WDG_MISS_COUNT_DR);
  }
  else if (reset_reason == RESET_REASON_ERROR_HANDLER)
  {
    g_persist_error_handler_count = persist_diag_increment(RTC_BKP_ERROR_HANDLER_COUNT_DR);
  }
  else if ((reset_reason >= RESET_REASON_FAULT_RANGE_MIN) &&
           (reset_reason <= RESET_REASON_FAULT_RANGE_MAX))
  {
    g_persist_fault_reset_count = persist_diag_increment(RTC_BKP_FAULT_RESET_COUNT_DR);
  }
  else
  {
    g_persist_poweron_count = persist_diag_increment(RTC_BKP_POWERON_COUNT_DR);
  }

  g_persist_poweron_count = persist_diag_read(RTC_BKP_POWERON_COUNT_DR);
  g_persist_error_handler_count = persist_diag_read(RTC_BKP_ERROR_HANDLER_COUNT_DR);
  g_persist_wdg_miss_count = persist_diag_read(RTC_BKP_WDG_MISS_COUNT_DR);
  g_persist_fault_reset_count = persist_diag_read(RTC_BKP_FAULT_RESET_COUNT_DR);
  g_persist_last_event_code = persist_diag_read(RTC_BKP_LAST_EVENT_CODE_DR);

  /* Consume reason to avoid double accounting on the next clean boot. */
  persist_diag_write(RTC_BKP_RESET_REASON_DR, 0U);
}

static void persist_diag_update_last_event(uint16_t code)
{
  g_persist_last_event_code = (uint32_t)code;
  persist_diag_write(RTC_BKP_LAST_EVENT_CODE_DR, (uint32_t)code);
}

static bool task_heartbeat_missed(uint32_t now_ms, uint32_t last_kick_ms, uint32_t timeout_ms)
{
  if (last_kick_ms == 0U)
  {
    return true;
  }
  return ((uint32_t)(now_ms - last_kick_ms) > timeout_ms);
}

static bool iwdg_init(uint8_t prescaler, uint16_t reload)
{
#if (GH_ENABLE_IWDG == 0U)
  (void)prescaler;
  (void)reload;
  return true;
#else
  uint32_t loops = 0U;

  IWDG->KR = IWDG_KR_KEY_WRITE;
  IWDG->PR = (uint32_t)(prescaler & (uint8_t)IWDG_PR_PR);
  IWDG->RLR = (uint32_t)(reload & (uint16_t)IWDG_RLR_RL);

  while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0U)
  {
    loops++;
    if (loops > IWDG_SR_WAIT_MAX_LOOPS)
    {
      return false;
    }
  }

  IWDG->KR = IWDG_KR_KEY_RELOAD;
  IWDG->KR = IWDG_KR_KEY_ENABLE;
  return true;
#endif
}

static void iwdg_refresh(void)
{
  IWDG->KR = IWDG_KR_KEY_RELOAD;
}

static uint16_t task_stack_hwm_words(osThreadId_t thread_id)
{
  UBaseType_t words;

  if (thread_id == NULL)
  {
    return 0U;
  }

  words = uxTaskGetStackHighWaterMark((TaskHandle_t)thread_id);
  if (words > 0xFFFFU)
  {
    words = 0xFFFFU;
  }

  return (uint16_t)words;
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
  if (gh_crc32_compute(payload, hdr->len) != hdr->crc)
  {
    return false;
  }

  out_cfg->version = hdr->version;
  out_cfg->crc = hdr->crc;
  memcpy(out_cfg->payload, payload, CONFIG_PAYLOAD_SIZE);
  return true;
}

bool config_write_to_slot(uint32_t slot_addr, uint32_t sector, const active_config_t *cfg)
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
    g_active_config.crc = gh_crc32_compute(g_active_config.payload, CONFIG_PAYLOAD_SIZE);
  }

  GH_TopologyV2_SyncRuntimeFromConfig(&g_active_config);
}

void publish_event(uint8_t severity, uint16_t code, uint16_t source, float value)
{
  (void)severity;
  (void)source;
  (void)value;
  g_next_event_id++;
  g_status.last_error_code = code;
  g_status.events_generated_count++;
  persist_diag_update_last_event(code);
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
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  persist_diag_bootstrap();
  sensors_init_defaults();
  config_load_or_default();
  GH_TopologyStorage_LoadActiveFromFlash();
  GH_ModbusMap_Init();

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
  qConfigApplyHandle = osMessageQueueNew(4U, sizeof(config_apply_req_t), NULL);
  qConfigStoreHandle = osMessageQueueNew(4U, sizeof(config_update_req_t), NULL);
  qTopologyStoreHandle = osMessageQueueNew(4U, sizeof(topology_chunk_req_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  controlTaskHandle = osThreadNew(StartControlTask, NULL, &controlTask_attributes);
  modbusMasterTaskHandle = osThreadNew(StartModbusMasterTask, NULL, &modbusMasterTask_attributes);
  modbusTcpServerTaskHandle = osThreadNew(StartModbusTcpServerTask, NULL, &modbusTcpServerTask_attributes);
  configStorageTaskHandle = osThreadNew(StartConfigStorageTask, NULL, &configStorageTask_attributes);
  healthWatchdogTaskHandle = osThreadNew(StartHealthWatchdogTask, NULL, &healthWatchdogTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  Error_Handler();
  return 0;
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  uint32_t rtc_clk_source = RCC_RTCCLKSOURCE_NO_CLK;

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  rtc_clk_source = __HAL_RCC_GET_RTC_SOURCE();
  if ((rtc_clk_source != RCC_RTCCLKSOURCE_NO_CLK) &&
      (rtc_clk_source != RCC_RTCCLKSOURCE_LSE))
  {
    /* One-time migration: clear backup domain if RTC was previously sourced
       from a different clock (e.g. LSI), then switch RTC to LSE. */
    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
  }
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
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
    sTime.Hours = 13;
    sTime.Minutes = 50;
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
  huart2.Init.BaudRate = 19200;
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
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{
  if (!iwdg_init((uint8_t)IWDG_PRESCALER_VALUE, (uint16_t)IWDG_RELOAD_VALUE))
  {
    Error_Handler();
  }
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
  config_apply_req_t apply_req = {0};
  (void)argument;
  for (;;)
  {
    if (osMessageQueueGet(qConfigApplyHandle, &apply_req, NULL, 100U) == osOK)
    {
      g_active_config = apply_req.config;
      GH_TopologyV2_SyncRuntimeFromConfig(&g_active_config);
      g_control_sync_pending = 1U;
      g_setpoints_apply_in_progress = false;
      GH_ModbusMap_ReportConfigResult(apply_req.request_token,
                                      CFG_RESULT_APPLIED,
                                      apply_req.config.version);
      publish_event(EVENT_SEV_INFO, EVENT_CODE_CFG_APPLIED, 0U, (float)apply_req.config.version);
    }
    task_heartbeat_kick(TASK_BIT_CONTROL);
    osDelay(20U);
  }
}

void StartModbusMasterTask(void *argument)
{
  GH_ModbusMasterTask_Run(argument);
}

void StartModbusTcpServerTask(void *argument)
{
  GH_ModbusTcpServerTask_Run(argument);
}

void StartConfigStorageTask(void *argument)
{
  GH_ConfigStorageTask_Run(argument);
}

void StartHealthWatchdogTask(void *argument)
{
  uint32_t now_ms;
  uint32_t last_control_ms;
  uint32_t last_modbus_ms;
  uint32_t last_config_ms;
  uint32_t last_tcp_ms;
  uint32_t missing_mask;
  uint32_t miss_streak = 0U;
  uint32_t grace_until_ms = HAL_GetTick() + 5000U;
  bool allow_iwdg_refresh;
  bool miss_latched = false;
  (void)argument;
  for (;;)
  {
    g_status.stack_hwm_control_words = task_stack_hwm_words(controlTaskHandle);
    g_status.stack_hwm_modbus_words = task_stack_hwm_words(modbusMasterTaskHandle);
    g_status.stack_hwm_config_words = task_stack_hwm_words(configStorageTaskHandle);
    g_status.stack_hwm_tcp_words = task_stack_hwm_words(modbusTcpServerTaskHandle);
    g_status.stack_hwm_wdg_words = task_stack_hwm_words(healthWatchdogTaskHandle);
    g_status.heap_free_bytes = xPortGetFreeHeapSize();
    g_status.heap_min_ever_bytes = xPortGetMinimumEverFreeHeapSize();

    allow_iwdg_refresh = true;
    now_ms = HAL_GetTick();
    taskENTER_CRITICAL();
    last_control_ms = g_last_kick_control_ms;
    last_modbus_ms = g_last_kick_modbus_ms;
    last_config_ms = g_last_kick_config_ms;
    last_tcp_ms = g_last_kick_tcp_ms;
    taskEXIT_CRITICAL();

    if ((int32_t)(now_ms - grace_until_ms) < 0)
    {
      task_heartbeat_kick(TASK_BIT_WDG);
      if (GH_ENABLE_IWDG != 0U)
      {
        iwdg_refresh();
      }
      osDelay(1000U);
      continue;
    }

    missing_mask = 0U;
    if (task_heartbeat_missed(now_ms, last_control_ms, WDG_TIMEOUT_CONTROL_MS))
    {
      missing_mask |= TASK_BIT_CONTROL;
    }
    if (task_heartbeat_missed(now_ms, last_modbus_ms, WDG_TIMEOUT_MODBUS_MS))
    {
      missing_mask |= TASK_BIT_MODBUS;
    }
    if (task_heartbeat_missed(now_ms, last_config_ms, WDG_TIMEOUT_CONFIG_MS))
    {
      missing_mask |= TASK_BIT_CONFIG;
    }
    if (task_heartbeat_missed(now_ms, last_tcp_ms, WDG_TIMEOUT_TCP_MS))
    {
      missing_mask |= TASK_BIT_TCP;
    }

    if (missing_mask != 0U)
    {
      miss_streak++;
      if (miss_streak >= 3U)
      {
        g_status.last_error_code = EVENT_CODE_WDG_MISS;
        allow_iwdg_refresh = false;
        if (!miss_latched)
        {
          store_reset_reason(RESET_REASON_WATCHDOG_MISS);
          publish_event(EVENT_SEV_CRIT, EVENT_CODE_WDG_MISS, 0U, (float)(missing_mask & 0xFFFFU));
          miss_latched = true;
        }
      }
    }
    else
    {
      miss_streak = 0U;
      miss_latched = false;
      if (g_status.last_error_code == EVENT_CODE_WDG_MISS)
      {
        g_status.last_error_code = 0U;
      }
    }

    if ((GH_ENABLE_IWDG != 0U) && allow_iwdg_refresh)
    {
      iwdg_refresh();
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
  __disable_irq();
  store_reset_reason(RESET_REASON_ERROR_HANDLER);
  NVIC_SystemReset();
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
