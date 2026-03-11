# HIL Scenarios

Date: `2026-03-08`

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

6. `topology_invalid_safe_mode`
- Provide invalid/empty topology so runtime plan becomes unavailable.
- Verify device enters deterministic safe mode (quality `STALE/OFFLINE`, heartbeat intact).
- Verify no legacy fallback execution path is observed.

7. `command_ingress_busy_pressure`
- Submit two command triggers back-to-back through generic command block.
- Verify second trigger is rejected with `REJECT_BUSY`.
- Verify first accepted trigger reaches final result path (`LAST_APPLIED_TRIGGER` sync).

8. `schedule_via_command_profile`
- Submit schedule payload via generic command ingress (`cmd_kind=schedule` contract).
- Verify FC16 + FC6 sequence executes via topology command profile chain.
- Verify apply status/version checks and final result code are stable.
