#include "gh_runtime_state.h"

UART_HandleTypeDef huart2 = {0};

osMessageQueueId_t qConfigApplyHandle = (osMessageQueueId_t)0x2;
osMessageQueueId_t qConfigStoreHandle = (osMessageQueueId_t)0x3;

sensor_state_t g_sensors[SENSOR_COUNT] = {0};
status_payload_t g_status = {0};
active_config_t g_active_config = {0};
uint32_t g_next_event_id = 1U;
uint32_t g_config_seq = 1U;
volatile uint8_t g_control_sync_pending = 0U;
bool g_setpoints_apply_in_progress = false;

void task_heartbeat_kick(task_heartbeat_bit_t bit)
{
  (void)bit;
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

bool config_write_to_slot(uint32_t slot_addr, uint32_t sector, const active_config_t *cfg)
{
  (void)slot_addr;
  (void)sector;
  (void)cfg;
  return true;
}

bool modbus_read_holding_registers(uint8_t slave_id,
                                   uint16_t start_reg,
                                   uint16_t reg_count,
                                   uint16_t *out_regs)
{
  (void)slave_id;
  (void)start_reg;
  (void)reg_count;
  (void)out_regs;
  return false;
}

bool modbus_write_single_holding_register(uint8_t slave_id, uint16_t reg, uint16_t value)
{
  (void)slave_id;
  (void)reg;
  (void)value;
  return false;
}

bool modbus_write_multiple_holding_registers(uint8_t slave_id,
                                             uint16_t start_reg,
                                             uint16_t reg_count,
                                             const uint16_t *regs)
{
  (void)slave_id;
  (void)start_reg;
  (void)reg_count;
  (void)regs;
  return false;
}

bool apply_control_to_slave(uint8_t slave_id, const active_config_t *cfg)
{
  (void)slave_id;
  (void)cfg;
  return false;
}
