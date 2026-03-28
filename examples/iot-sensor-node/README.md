# IoT Sensor Node Example

A complete example using **all nine micro-toolkit libraries** together
in a simulated temperature/humidity sensor node.

## What it demonstrates

| Library | Role in this example |
|---------|---------------------|
| microconf | Load MQTT host, port, device ID from flash (CRC validated) |
| microfsm | Device lifecycle: BOOT → CONNECTING → ONLINE → PUBLISHING → ERROR |
| micoring | ISR-safe ring buffer between DHT22 interrupt and main loop |
| microres | Circuit breaker on MQTT publish + exponential backoff retry |
| microcbor | Encode telemetry as compact CBOR (~50 bytes vs ~80 JSON) |
| microlog | Structured logging with timestamps and ANSI color |
| microsh | Debug shell: `status`, `conf list`, `cbor`, `breaker`, `bus`, `timers` |
| microtimer | Software timers: sensor report (periodic) + watchdog + LED blink |
| microbus | Pub/sub event bus: sensor→main, publish results, breaker events |

## Build & run

Linux/macOS simulation — replace platform stubs in `main.c` with your HAL calls for real hardware.

```bash
# Clone all libraries alongside this repo
git clone https://github.com/Vanderhell/microfsm   lib/microfsm
git clone https://github.com/Vanderhell/microres   lib/microres
git clone https://github.com/Vanderhell/microconf  lib/microconf
git clone https://github.com/Vanderhell/microlog   lib/microlog
git clone https://github.com/Vanderhell/microsh    lib/microsh
git clone https://github.com/Vanderhell/microcbor  lib/microcbor
git clone https://github.com/Vanderhell/micoring   lib/micoring
git clone https://github.com/Vanderhell/microtimer lib/microtimer
git clone https://github.com/Vanderhell/microbus   lib/microbus

cd examples/iot-sensor-node
make run
```

## Expected output

```
14.436 [I] BOOT: === IoT Sensor Node -- all 9 micro-toolkit libraries ===
14.436 [W] CONF: Load failed (-6) — using defaults
14.436 [I] CONF: esp32-node-01 @ broker.local:1883  interval=5000 ms
14.436 [D] RING: Ring buffer ready — capacity=8, elem=16 B
14.436 [I] FSM:  → BOOT
14.436 [D] BUS:  Event bus ready — 2 subscribers
14.436 [D] TMR:  3 timers created + started (report / watchdog / led)
14.436 [I] SHELL: Debug shell ready — 9 commands registered
14.436 [I] BOOT: All 9 subsystems initialised ✓
14.436 [I] FSM:  → CONNECTING
14.537 [I] FSM:  → ONLINE
14.738 [D] SENSOR: seq=0  T=22.6°C  H=64.0%
14.738 [D] CBOR: Encoded 50 B (vs ~80 JSON)
14.738 [I] MQTT: [1] OK  50 B → sensors/esp32-node-01/telemetry
14.939 [D] LED:  LED ▮ ON
...

sensor> status
State     : ONLINE
Published : 8 OK / 1 FAIL
CBOR total: 450 B
Breaker   : CLOSED
Ring      : 0 pending
WDG ticks : 0
LED blinks: 3

sensor> timers
Timers:
  [0] report       RUNNING  fires=9
  [1] watchdog     RUNNING  fires=0
  [2] led_blink    RUNNING  fires=3
Total fires: 12

sensor> bus
Publishes : 21
Deliveries: 3
Dropped   : 0
Subscribers: 2
Queue     : 0 pending
```

## Porting to ESP32

The same `main.c` compiles for both Linux and ESP32. The `#ifdef ESP_PLATFORM`
blocks select the correct HAL automatically:

| Stub | Replace with |
|------|-------------|
| `timer_report_cb` | GPIO ISR or `esp_timer` callback reading DHT22 |
| `flash_read/write` | `nvs_get_blob` / `nvs_set_blob` |
| `mqtt_publish_op` | `esp_mqtt_client_publish()` |
| `plat_now_ms` | Already uses `esp_timer_get_time()` on ESP32 |
| `plat_sleep_ms` | Already uses `vTaskDelay()` on ESP32 |

```bash
# ESP32 build
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```