# IoT Sensor Node Example

A complete example using **all seven micro-toolkit libraries** together
in a simulated temperature/humidity sensor node.

## What it demonstrates

| Library | Role in this example |
|---------|---------------------|
| microconf | Load MQTT host, port, device ID from flash (with CRC) |
| microfsm | Device lifecycle: INIT → CONNECTING → ONLINE → PUBLISHING |
| micoring | Buffer sensor events from ISR to main loop |
| microres | Circuit breaker on MQTT publish (open after 3 failures) |
| microcbor | Encode telemetry as compact CBOR (30-50% smaller than JSON) |
| microlog | Structured logging with timestamps and color |
| microsh | Debug shell: `status`, `conf get`, `breaker`, `log level` |

## Build & run

This example is a Linux/macOS simulation. On real hardware, replace the
platform stubs in `main.c` with your HAL calls.

```bash
# Clone all libraries alongside this repo
cd micro-toolkit/examples/iot-sensor-node
make run
```

## Expected output

```
0.001 [I] BOOT: === IoT Sensor Node starting ===
0.001 [W] CONF: Using defaults (invalid magic)
0.001 [I] CONF: Host: broker.local:1883, Device: sensor-01, Interval: 5000 ms
0.001 [I] BOOT: All subsystems initialized
0.002 [D] FSM: INIT --(0)--> CONNECTING
0.002 [I] FSM: Connecting to broker.local:1883 ...
0.502 [D] FSM: CONNECTING --(1)--> ONLINE
0.502 [I] FSM: Online and ready (device: sensor-01)

0.502 [D] CBOR: Encoded 38 bytes (JSON would be ~60)
0.502 [I] MQTT: Published #1 (25.3°C, 55%)
...

--- Shell demo ---
> State:     ONLINE
Breaker:   CLOSED (0 failures)
Published: 5 messages
Events:    0/8 in ring
```
