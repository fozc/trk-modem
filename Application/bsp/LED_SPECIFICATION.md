# LED Specification — Troika Smart Breaker Modem

## Hardware Map

| Physical LED   | GPIO        | Pin  | Channel            | Function           | Polarity     |
|----------------|-------------|------|--------------------|--------------------|-------------|
| LED1           | GPIOE       | PE7  | (heartbeat)        | CPU alive toggle   | Active-LOW   |
| LED2           | GPIOB       | PB2  | `LED_CHANNEL_IEC104` | IEC104 listener  | Active-LOW   |
| LED3           | GPIOB       | PB1  | `LED_CHANNEL_WEB`  | Web listener       | Active-LOW   |
| RGB1 Red       | GPIOE       | PE11 | `LED_CHANNEL_MODEM_R` | Modem status    | Active-LOW   |
| RGB1 Green     | GPIOE       | PE12 | `LED_CHANNEL_MODEM_G` | Modem status    | Active-LOW   |
| RGB1 Blue      | GPIOE       | PE13 | `LED_CHANNEL_MODEM_B` | Modem status    | Active-LOW   |
| RGB2 Red       | GPIOE       | PE8  | `LED_CHANNEL_GSM_R` | GSM network      | Active-LOW   |
| RGB2 Green     | GPIOE       | PE9  | `LED_CHANNEL_GSM_G` | GSM network      | Active-LOW   |
| RGB2 Blue      | GPIOE       | PE10 | `LED_CHANNEL_GSM_B` | GSM network      | Active-LOW   |

All LEDs are active-LOW: GPIO=0 turns the LED ON.

---

## 1. Modem Status LED (RGB1)

Indicates the overall modem initialization and connectivity state.

| State                 | Color          | Pattern                          | Waveform                               |
|-----------------------|----------------|----------------------------------|-----------------------------------------|
| Power-on, HW boot     | Yellow (R+G)   | 1000 ms ON / 1000 ms OFF          | `\|-----\|_____\|-----\|_____`          |
| Init, AT responding   | Yellow (R+G)   | 250 ms ON / 250 ms OFF           | `\|_\|^_\|^_\|^`                       |
| Searching network     | Blue (B)       | 1000 ms ON / 1000 ms OFF         | `\|-----\|_____\|-----\|_____`          |
| Connected, ready      | Green (G)      | 250 ms ON / 2750 ms OFF          | `\|_\|___________\|_\|___________`      |
| No SIM / SIM error    | Red (R)        | Double pulse: 150/150, 150, 1500 | `\|_\|^\|_\|_______________`            |
| Generic error         | Red (R)        | Triple pulse: 150/150 x3, 1500   | `\|_\|^\|_\|^\|_\|_______________`      |

### Trigger Points (gsm_init.c)

| Mode                  | Trigger Location                                           |
|----------------------|-------------------------------------------------------------|
| `LED_MODEM_POWER_ON` | `gsm_power_on` process — power-on sequence start            |
| `LED_MODEM_INIT`     | `gsm_init_step_check_module()` — first AT OK                |
| `LED_MODEM_SEARCHING` | `gsm_init_step_check_simcard()` — SIM READY                |
| `LED_MODEM_READY`    | `gsm_init_step_connect_gprs()` — GPRS OK                   |
| `LED_MODEM_READY`    | `gsm_init_step_done()` — entering NORMAL_MODE              |
| `LED_MODEM_NO_SIM`   | `gsm_init_step_check_simcard()` — NO SIM                   |
| `LED_MODEM_ERROR`    | `gsm_init_step_reset_module()` — module reset               |

---

## 2. GSM Network LED (RGB2)

Indicates the registered radio access technology and signal strength.
Driven by the `gsm_signal_led` module via `gsm_signal_led_apply()` BSP override.

| State             | Color     | Pattern                     | Waveform                          |
|-------------------|-----------|-----------------------------|-----------------------------------|
| No network        | Off       | —                           | `___________________________`     |
| 2G registered     | Red (R)   | 250 ms ON / 2750 ms OFF     | `\|_\|___________\|_\|___________`|
| 2G weak signal    | Red (R)   | 500 ms ON / 500 ms OFF      | `\|---\|___\|---\|___`            |
| 3G registered     | Blue (B)  | 250 ms ON / 2750 ms OFF     | `\|_\|___________\|_\|___________`|
| 3G weak signal    | Blue (B)  | 500 ms ON / 500 ms OFF      | `\|---\|___\|---\|___`            |
| 4G registered     | Green (G) | Solid ON                    | `\|^^^^^^^^^^^^^^^^^^^^^^^`       |
| 4G weak signal    | Green (G) | 500 ms ON / 500 ms OFF      | `\|---\|___\|---\|___`            |

### Signal Level Classification (from AT+CESQ)

| Technology | Source Field | Weak     | Fair     | Good     | Excellent |
|------------|-------------|----------|----------|----------|-----------|
| 2G (GSM)   | rxlev 0-63  | 0-14     | 15-24    | 25-35    | 36+       |
| 3G (WCDMA) | rscp 0-96   | 0-24     | 25-48    | 49-71    | 72+       |
| 4G (LTE)   | rsrp 0-97   | 0-24     | 25-48    | 49-72    | 73+       |

- **Weak** signal uses 500/500 blink pattern.
- **Fair** and above use the normal pattern (250/2750 pulse or solid).
- Technology priority: 4G > 3G > 2G.

### Trigger Point

`gsm_signal_led_update()` is called from `gsm_periodical_step_ext_signal_quality()`
after every AT+CESQ response. The weak `gsm_signal_led_apply()` is overridden in
`bsp.c` to call `led_driver_set_gsm_mode()`.

---

## 3. IEC104 Listener LED (LED2)

Indicates the IEC104 listener socket state.

| State             | Pattern                     | Waveform                          |
|-------------------|-----------------------------|-----------------------------------|
| Off / not init    | Off                         | `___________________________`     |
| Listening         | 1000 ms ON / 1000 ms OFF    | `\|-----\|_____\|-----\|_____`    |
| Client connected  | Solid ON                    | `\|^^^^^^^^^^^^^^^^^^^^^^^`       |

### Trigger Points

| Mode                    | Location                                           |
|-------------------------|-----------------------------------------------------|
| `LED_LISTENER_LISTENING`| `gsm_init_step_open_iec104_listener()` — socket OK  |
| `LED_LISTENER_LISTENING`| `handle_open_socket()` — re-opened after close      |
| `LED_LISTENER_CONNECTED`| `handle_accept_connection()` — client connected     |
| `LED_LISTENER_CONNECTED`| `handle_check_socket()` — state '2' or '3'          |
| `LED_LISTENER_LISTENING`| `handle_check_socket()` — state '4'                 |
| `LED_LISTENER_OFF`      | `handle_close_socket()` — socket closed              |

---

## 4. Web Listener LED (LED3)

Indicates the web server listener socket state. Same pattern table as IEC104.

| State             | Pattern                     | Waveform                          |
|-------------------|-----------------------------|-----------------------------------|
| Off / not init    | Off                         | `___________________________`     |
| Listening         | 1000 ms ON / 1000 ms OFF    | `\|-----\|_____\|-----\|_____`    |
| Client connected  | Solid ON                    | `\|^^^^^^^^^^^^^^^^^^^^^^^`       |

### Trigger Points

Same as IEC104 — the unified `gsm_listener_socket_process()` handles both
through the `ls_led_update()` helper that maps `GSM_LISTENER_WEB` / `GSM_LISTENER_IEC104`
to the appropriate LED driver function.

---

## 5. Heartbeat LED (LED1)

Simple 500 ms toggle driven from `heart_beat_process` in `app_main.c`.
Not managed by the LED driver — uses direct `gpio_toggle_pin()`.
LED1 is active-LOW: GPIO=0 turns LED ON.

| State     | Pattern            |
|-----------|--------------------|
| CPU alive | 500 ms ON / 500 ms OFF |

---

## Architecture

### Pattern Engine

The LED driver (`led_driver.c/h`) uses a table-driven pattern engine:

```
led_pattern_t = {
    phases[3] = { {on_ms, off_ms}, ... },
    phase_count,
    repeat_gap_ms
}
```

- Single phase + no gap = simple blink (250/250, 1000/1000, etc.)
- Single phase + on only = solid ON
- Multiple phases + gap = multi-pulse patterns (double/triple pulse)
- 0 phases = LED off

### Tick Rate

`led_driver_tick()` is called from the `heart_beat_process` main loop at ~10 Hz
(100 ms etimer period). The driver uses `clock_time()` internally for
millisecond-accurate timing regardless of call frequency.

### Module Relationships

```
app_main.c ──── led_driver_init() + led_driver_tick()
gsm_init.c ──── led_driver_set_modem_mode()
gsm_listener_process.c ── led_driver_set_iec104_mode() / led_driver_set_web_mode()
gsm_periodical.c ── gsm_signal_led_update()
  └── gsm_signal_led.c ── gsm_signal_led_apply() [weak]
       └── bsp.c ── gsm_signal_led_apply() [override] ── led_driver_set_gsm_mode()
```

### Files

| File                         | Role                                     |
|------------------------------|------------------------------------------|
| `bsp/led_driver.h`          | Public API, types, pattern declarations  |
| `bsp/led_driver.c`          | Pattern engine, pin map, mode functions  |
| `bsp/bsp.c`                 | `gsm_signal_led_apply()` BSP override    |
| `gsm/gsm_signal_led.c/h`    | CESQ classification (tech + level)       |
| `gsm/gsm_init.c`            | Modem status LED integration             |
| `gsm/gsm_listener_process.c`| IEC104 + Web listener LED integration    |
| `gsm/gsm_periodical.c`      | Periodic CESQ query → signal LED update  |
| `app_main.c`                | LED driver init + tick                   |
