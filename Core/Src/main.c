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
#include "gh_modbus_master.h"
#include "gh_modbus_map.h"
#include "gh_modbus_tcp_server.h"
#include "gh_config_storage.h"
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define RTC_BKP_INIT_MARKER 0x32F2U

#define SENSOR_COUNT                  150U
#define MODBUS_MAX_SLAVES             12U
#define STATUS_MODBUS_TIMEOUT_SLOTS   8U
#define MODBUS_MAX_SENSORS_PER_SLAVE  12U
#define BLOCK_CHANNEL_COUNT           9U
#define MODBUS_POLL_PERIOD_MS         5000U
#define HEARTBEAT_PERIOD_MS           1000U

#define CONFIG_PAYLOAD_SIZE           128U
#define CONFIG_VALID_MARKER           0xA55A5AA5UL
#define CONFIG_SLOT_A_ADDR            0x08040000UL /* Sector 6, STM32F407VE */
#define CONFIG_SLOT_B_ADDR            0x08060000UL /* Sector 7, STM32F407VE */
#define CONFIG_SLOT_A_SECTOR          FLASH_SECTOR_6
#define CONFIG_SLOT_B_SECTOR          FLASH_SECTOR_7

#define EVENT_CODE_LINK_DOWN          1000U
#define EVENT_CODE_LINK_UP            1001U
#define EVENT_CODE_CFG_APPLIED        1100U
#define EVENT_CODE_WDG_MISS           1200U
#define EVENT_CODE_CTRL_SYNC_FAIL     1300U

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
void StartModbusTcpServerTask(void *argument);
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

typedef struct __attribute__((packed))
{
  uint32_t version;
  uint32_t len;
  uint32_t crc;
  uint32_t seq;
  uint32_t valid_marker;
} config_slot_header_t;

typedef struct
{
  uint32_t version;
  uint8_t payload[CONFIG_PAYLOAD_SIZE];
  uint32_t crc;
} active_config_t;

typedef enum
{
  TASK_BIT_CONTROL = (1UL << 0),
  TASK_BIT_MODBUS = (1UL << 1),
  TASK_BIT_CONFIG = (1UL << 2),
  TASK_BIT_WDG = (1UL << 3)
} task_heartbeat_bit_t;

sensor_state_t g_sensors[SENSOR_COUNT];
uint32_t g_next_event_id = 1U;

static uint32_t g_watchdog_flags = 0U;
static const uint32_t g_watchdog_all_mask =
  TASK_BIT_CONTROL | TASK_BIT_MODBUS | TASK_BIT_CONFIG;

status_payload_t g_status = {0};
active_config_t g_active_config = {0};
uint32_t g_config_seq = 1U;

bool g_setpoints_apply_in_progress = false;
volatile uint8_t g_control_sync_pending = 0U;

void task_heartbeat_kick(task_heartbeat_bit_t bit)
{
  taskENTER_CRITICAL();
  g_watchdog_flags |= (uint32_t)bit;
  taskEXIT_CRITICAL();
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
}

void publish_event(uint8_t severity, uint16_t code, uint16_t source, float value)
{
  (void)severity;
  (void)source;
  (void)value;
  g_next_event_id++;
  g_status.last_error_code = code;
  g_status.events_generated_count++;
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
  qConfigApplyHandle = osMessageQueueNew(4U, sizeof(active_config_t), NULL);
  qConfigStoreHandle = osMessageQueueNew(4U, sizeof(config_update_req_t), NULL);
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
      g_control_sync_pending = 1U;
      publish_event(EVENT_SEV_INFO, EVENT_CODE_CFG_APPLIED, 0U, (float)cfg.version);
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
  uint32_t snapshot_flags;
  uint32_t miss_streak = 0U;
  uint32_t grace_until_ms = HAL_GetTick() + 5000U;
  bool miss_latched = false;
  (void)argument;
  for (;;)
  {
    taskENTER_CRITICAL();
    snapshot_flags = g_watchdog_flags;
    g_watchdog_flags = 0U;
    taskEXIT_CRITICAL();

    if ((int32_t)(HAL_GetTick() - grace_until_ms) < 0)
    {
      task_heartbeat_kick(TASK_BIT_WDG);
      osDelay(1000U);
      continue;
    }

    if ((snapshot_flags & g_watchdog_all_mask) != g_watchdog_all_mask)
    {
      miss_streak++;
      if (miss_streak >= 3U)
      {
        g_status.last_error_code = EVENT_CODE_WDG_MISS;
        if (!miss_latched)
        {
          publish_event(EVENT_SEV_CRIT, EVENT_CODE_WDG_MISS, 0U, (float)(snapshot_flags & 0xFFFFU));
          miss_latched = true;
        }
      }
    }
    else
    {
      miss_streak = 0U;
      /* Integration point: HAL_IWDG_Refresh(&hiwdg); */
      miss_latched = false;
      if (g_status.last_error_code == EVENT_CODE_WDG_MISS)
      {
        g_status.last_error_code = 0U;
      }
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
