# Window Setpoints Client Contract

Date: `2026-05-12`

This screen edits zone-slave window controller setpoints. The client talks only
to the master over Modbus TCP and submits data-driven commands; the master then
writes the selected register block to the slave over Modbus RTU.

## Navigation

Recommended screen structure:

- Zone selector and status strip: selected zone, active topology generation,
  last submit result, dirty/saved state.
- Tab `Manual`: `103`, `104`, `171`, `172`.
- Tab `Automation`: temperature and humidity algorithm settings:
  `195`, `173`, `180`, `181`, `219`, `196`, `197`, `198`, `221`.
- Tab `Safety`: cold close and stale weather:
  `199`, `200`, `174`, `212`, `223`, `224`.
- Tab `Greenhouse`: global targets `275`, `276`.
- Tab `Curtain`: `243..274`.
- Tab `Wind`: storm, sector, windward/leeward limits:
  `176`, `177`, `178`, `179`, `201..209`.
- Tab `Rain`: `210`, `211`.
- Tab `Actuator`: `182`, `183`, `184`.
- Tab `Service`: `175`, `185`, `186`, `220`, `222`; hide by default.

Inside each tab, sort by control workflow first and show the slave register as
secondary technical metadata. The operator should not have to scan by register
number during normal work.

## Command Profiles

For topology `one_zone_one_weather_schedule`, zone 1 uses:

| UI/transport block | Profile | Slave regs | Payload words |
|---|---:|---:|---:|
| Manual window targets | `5002` | `103..104` | `2` |
| Window core/safety/actuator/service | `5004` | `171..186` | `16` |
| Auto, cold, wind, rain mode | `5005` | `195..210` | `16` |
| Rain/stale weather policy | `5006` | `211..212` | `2` |
| Step targets and stale age | `5007` | `219..224` | `6` |
| Curtain core | `5015` | `243..250` | `8` |
| Curtain radiation/temp | `5016` | `251..260` | `10` |
| Curtain humidity | `5017` | `261..266` | `6` |
| Curtain fault reset | `5018` | `274` | `1` |
| Greenhouse global targets | `5019` | `275..276` | `2` |

For topology `two_zones_one_weather_schedule`, zone 2 uses profiles
`5008`, `5009`, `5010`, `5011`, `5012`, `5020`, `5021`, `5022`, `5023`,
`5024` for the same register blocks.

## Submit Rules

The master command ingress window is unchanged:

- `TARGET_SLAVE_ID`
- `TARGET_MODULE_ID`
- `CMD_PROFILE_ID`
- `PAYLOAD_LEN`
- `PAYLOAD[]`
- `TRIGGER`, written last

The client should keep a full local form state for each transport block. When a
field inside a block changes, submit the full block for deterministic behavior.
Partial prefix writes are supported by the generic dispatcher, but they are only
safe for prefix ranges such as `103..104`; they are not safe for isolated fields
inside `171..186`, `195..210`, or `219..224`.

Result is accepted only when:

- `LAST_APPLIED_TRIGGER == TRIGGER`
- `RESULT == 2`
- `IO_ERR == 0`

Profiles `5019` and `5024` require master-side readback verification. After
the RTU write succeeds, the master reads slave registers `275..276` and reports
`ACK_FAIL` instead of `APPLIED` if the values do not match the submitted
payload.

## Payload Mapping

Profile `5002`, start `103`:

- `PAYLOAD[0]` -> `103 WINDOWS_POS_A_TARGET`
- `PAYLOAD[1]` -> `104 WINDOWS_POS_B_TARGET`

The underlying topology profile allows a longer prefix up to `103..109`, but
the window screen should submit only two words so it does not touch water
setpoints.

Profile `5004`, start `171`:

- `PAYLOAD[0]` -> `171 WINDOWS_CTRL_MODE`
- `PAYLOAD[1]` -> `172 WINDOWS_FORCE_SAFE_CMD`
- `PAYLOAD[2]` -> `173 WINDOWS_TEMP_SETPOINT`
- `PAYLOAD[3]` -> `174 WINDOWS_SAFE_MIN_PERCENT`
- `PAYLOAD[4]` -> `175 WINDOWS_WIND_LIMIT`
- `PAYLOAD[5]` -> `176 WINDOWS_WIND_STORM`
- `PAYLOAD[6]` -> `177 WINDOWS_WIND_RECOVER`
- `PAYLOAD[7]` -> `178 WINDOW_A_AZIMUTH_DEG`
- `PAYLOAD[8]` -> `179 WINDOWS_WIND_SECTOR_HALF_WIDTH_DEG`
- `PAYLOAD[9]` -> `180 WINDOWS_TEMP_STEP_C`
- `PAYLOAD[10]` -> `181 WINDOWS_TEMP_STEP_HYST_C`
- `PAYLOAD[11]` -> `182 RLL400_TARGET_HYST_PERCENT`
- `PAYLOAD[12]` -> `183 RLL400_MOTION_DELTA_PERCENT`
- `PAYLOAD[13]` -> `184 RLL400_NO_MOTION_TIMEOUT_MS`
- `PAYLOAD[14]` -> `185 WINDOW_A_FAULT_RESET_TOKEN`
- `PAYLOAD[15]` -> `186 WINDOW_B_FAULT_RESET_TOKEN`

Profile `5005`, start `195`:

- `PAYLOAD[0]` -> `195 WINDOWS_AUTO_ALGO_MODE`
- `PAYLOAD[1]` -> `196 WINDOWS_HUM_SETPOINT`
- `PAYLOAD[2]` -> `197 WINDOWS_HUM_STEP`
- `PAYLOAD[3]` -> `198 WINDOWS_HUM_STEP_HYST`
- `PAYLOAD[4]` -> `199 WINDOWS_COLD_CLOSE_DELTA`
- `PAYLOAD[5]` -> `200 WINDOWS_COLD_CLOSE_HYST`
- `PAYLOAD[6]` -> `201 WINDOWS_WINDWARD_MIN_PERCENT`
- `PAYLOAD[7]` -> `202 WINDOWS_WINDWARD_MAX_PERCENT`
- `PAYLOAD[8]` -> `203 WINDOWS_WINDWARD_SPEED_THRESHOLD`
- `PAYLOAD[9]` -> `204 WINDOWS_WINDWARD_REDUCTION_PERCENT_PER_MS`
- `PAYLOAD[10]` -> `205 WINDOWS_LEEWARD_MIN_PERCENT`
- `PAYLOAD[11]` -> `206 WINDOWS_LEEWARD_MAX_PERCENT`
- `PAYLOAD[12]` -> `207 WINDOWS_LEEWARD_SPEED_THRESHOLD`
- `PAYLOAD[13]` -> `208 WINDOWS_LEEWARD_REDUCTION_PERCENT_PER_MS`
- `PAYLOAD[14]` -> `209 WINDOWS_WINDWARD_LAG_PERCENT`
- `PAYLOAD[15]` -> `210 WINDOWS_RAIN_MODE`

Profile `5006`, start `211`:

- `PAYLOAD[0]` -> `211 WINDOWS_RAIN_WINDWARD_PERCENT`
- `PAYLOAD[1]` -> `212 WINDOWS_WEATHER_STALE_POLICY`

Profile `5007`, start `219`:

- `PAYLOAD[0]` -> `219 WINDOWS_TEMP_STEP_TARGET_PERCENT`
- `PAYLOAD[1]` -> `220 WINDOWS_TEMP_STEP_MAX_INDEX`
- `PAYLOAD[2]` -> `221 WINDOWS_HUM_STEP_TARGET_PERCENT`
- `PAYLOAD[3]` -> `222 WINDOWS_HUM_STEP_MAX_INDEX`
- `PAYLOAD[4]` -> `223 WINDOWS_WEATHER_STALE_TIMEOUT_MS`
- `PAYLOAD[5]` -> `224 WINDOWS_WEATHER_SOURCE_AGE_S`

The current slave map exposes global greenhouse targets at `275..276`. It does
not expose `WINDOWS_REACTION_DELAY_MS`; clients must not send legacy profiles
`5013` or `5014` with this map.

The active topologies publish `187 WINDOWS_STATUS_BITS` through points
`10187`/`10287` as `zone_1.windows_status_bits` and
`zone_2.windows_status_bits`; clients should read bit `13` from that published
point instead of polling the slave directly.

## Curtain Mapping

The expanded curtain controller uses slave registers `243..274`. The slave
firmware must expose `MODBUS_HREG_TOTAL_COUNT >= 277` because global greenhouse
targets use `275..276`.

Profile `5015`, start `243`:

- `PAYLOAD[0]` -> `243 CURTAIN_CTRL_MODE`
- `PAYLOAD[1]` -> `244 CURTAIN_MANUAL_TARGET`
- `PAYLOAD[2]` -> `245 CURTAIN_SCHEDULE_START_HHMM`
- `PAYLOAD[3]` -> `246 CURTAIN_SCHEDULE_END_HHMM`
- `PAYLOAD[4]` -> `247 CURTAIN_OUTSIDE_TARGET`
- `PAYLOAD[5]` -> `248 CURTAIN_MIN_POSITION`
- `PAYLOAD[6]` -> `249 CURTAIN_MAX_POSITION`
- `PAYLOAD[7]` -> `250 CURTAIN_POSITION_HYST`

Profile `5016`, start `251`:

- `PAYLOAD[0]` -> `251 CURTAIN_RADIATION_THRESHOLD`
- `PAYLOAD[1]` -> `252 CURTAIN_RADIATION_STEP_WM2`
- `PAYLOAD[2]` -> `253 CURTAIN_RADIATION_STEP_PERCENT`
- `PAYLOAD[3]` -> `254 CURTAIN_RADIATION_HYST`
- `PAYLOAD[4]` -> `255 CURTAIN_COLD_DELTA`
- `PAYLOAD[5]` -> `256 CURTAIN_COLD_HYST`
- `PAYLOAD[6]` -> `257 CURTAIN_COLD_TARGET`
- `PAYLOAD[7]` -> `258 CURTAIN_HEAT_DELTA`
- `PAYLOAD[8]` -> `259 CURTAIN_HEAT_HYST`
- `PAYLOAD[9]` -> `260 CURTAIN_HEAT_TARGET`

Profile `5017`, start `261`:

- `PAYLOAD[0]` -> `261 CURTAIN_HUM_LOW_THRESHOLD`
- `PAYLOAD[1]` -> `262 CURTAIN_HUM_LOW_HYST`
- `PAYLOAD[2]` -> `263 CURTAIN_HUM_LOW_TARGET`
- `PAYLOAD[3]` -> `264 CURTAIN_HUM_HIGH_THRESHOLD`
- `PAYLOAD[4]` -> `265 CURTAIN_HUM_HIGH_HYST`
- `PAYLOAD[5]` -> `266 CURTAIN_HUM_HIGH_TARGET`

Profile `5018`, start `274`:

- `PAYLOAD[0]` -> `274 CURTAIN_FAULT_RESET_TOKEN`

Zone 2 uses profiles `5020`, `5021`, `5022`, and `5023` for the same blocks.
Registers `267..273` are read-only status registers and must not be written via
command profiles. Active topologies publish them as points `10167..10173` and
`10267..10273`.

## Greenhouse Target Mapping

Profile `5019`, start `275`:

- `PAYLOAD[0]` -> `275 AIR_TEMP_TARGET`, `x10 C`
- `PAYLOAD[1]` -> `276 AIR_HUM_TARGET`, `x10 %`

Zone 2 uses profile `5024` for the same block.
