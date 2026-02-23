# HIL Scenarios

Date: `2026-02-23`

The following scenarios are mandatory for release acceptance:

1. `loss_of_network`
- Disconnect Ethernet link during runtime.
- Verify TCP task heartbeat remains alive and link events are generated.
- Verify system keeps polling RTU and does not deadlock.

2. `modbus_timeouts`
- Inject intermittent and persistent RS485 slave timeouts.
- Verify timeout counters increment and sensor quality degrades to stale.
- Verify cycle timing remains within expected poll-period envelope.

3. `power_cycle_during_config_write`
- Trigger config submit, then power-cycle during A/B slot write window.
- Verify after reboot at least one valid slot is restored.
- Verify active config version is monotonic and not corrupted.

4. `watchdog_recovery`
- Block one supervised task to force heartbeat miss.
- Verify IWDG reset occurs within SLA and reset reason is persisted.
- Verify system returns to normal operation after reboot.

5. `config_crc_reject`
- Submit config with bad CRC and with out-of-range float payload.
- Verify reject result code is exposed through Modbus config window.
- Verify active config version remains unchanged.
