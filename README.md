# micro-toolkit

**A modular C99 toolkit for building reliable embedded & IoT firmware.**

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)
![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)
![Domain: Embedded](https://img.shields.io/badge/Domain-Embedded%20IoT-orange.svg)
![Memory: No Heap](https://img.shields.io/badge/Memory-No%20Heap-critical.svg)
![Dependencies: Zero](https://img.shields.io/badge/Dependencies-Zero-success.svg)

Zero dependencies · Zero allocations · ROM-friendly · Portable · Tested

---

## The problem

Every IoT project re-invents the same infrastructure: state machines built
from nested `switch` statements, retry logic scattered across files, config
stored as raw bytes with no validation, `printf` debugging that never gets
removed, and ring buffers written from scratch for every UART.

These are solved problems — but in embedded C, there's no ecosystem of
small, composable, zero-dependency libraries like there is in Rust or Go.
You either pull in a massive framework (Zephyr, ESP-IDF) or write
everything yourself.

**micro-toolkit** is a collection of focused C99 libraries that solve
one problem each, compose cleanly, and share a common philosophy:

> No heap. No dependencies. No code generation. Just `#include` and go.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        YOUR APPLICATION                     │
├─────────────────────────────────────────────────────────────┤
│  microsh ─── debug shell (runtime access to everything)     │
├─────────────────────────────────────────────────────────────┤
│  microlog ── structured logging (observability glue)        │
├───────────┬───────────┬───────────┬──────────┬──────────────┤
│ microfsm  │ microres  │ microconf │microcbor │   micoring   │
│           │           │           │          │              │
│ state     │ retry +   │ flash     │ CBOR     │ ring buffer  │
│ machine   │ circuit   │ config +  │ encode / │ ISR-safe     │
│ engine    │ breaker + │ CRC +     │ decode   │ lock-free    │
│           │ rate      │ defaults  │          │ SPSC         │
│           │ limiter   │           │          │              │
├───────────┴───────────┴───────────┴──────────┴──────────────┤
│  iotspool ── persistent MQTT queue                          │
│  MCU-Malloc-Tracker ── heap diagnostics                     │
└─────────────────────────────────────────────────────────────┘
```

## Libraries

| Library | Description | Lines | Tests | Links |
|---------|-------------|-------|-------|-------|
| [**microfsm**](https://github.com/Vanderhell/microfsm) | Table-driven finite state machine engine | 517 | 36 | [README](https://github.com/Vanderhell/microfsm#readme) |
| [**microres**](https://github.com/Vanderhell/microres) | Retry, circuit breaker, rate limiter | 721 | 41 | [README](https://github.com/Vanderhell/microres#readme) |
| [**microconf**](https://github.com/Vanderhell/microconf) | Schema-driven config with CRC + flash | 781 | 40 | [README](https://github.com/Vanderhell/microconf#readme) |
| [**microlog**](https://github.com/Vanderhell/microlog) | Multi-backend structured logging | 519 | 33 | [README](https://github.com/Vanderhell/microlog#readme) |
| [**microsh**](https://github.com/Vanderhell/microsh) | Debug shell with history + tab completion | 680 | 43 | [README](https://github.com/Vanderhell/microsh#readme) |
| [**microcbor**](https://github.com/Vanderhell/microcbor) | Minimal CBOR encoder/decoder (RFC 8949) | 750 | 43 | [README](https://github.com/Vanderhell/microcbor#readme) |
| [**micoring**](https://github.com/Vanderhell/micoring) | Generic ISR-safe ring buffer (SPSC) | 423 | 33 | [README](https://github.com/Vanderhell/micoring#readme) |
| [**iotspool**](https://github.com/Vanderhell/iotspool) | Persistent store-and-forward MQTT queue | — | — | [README](https://github.com/Vanderhell/iotspool#readme) |
| [**MCU-Malloc-Tracker**](https://github.com/Vanderhell/MCU-Malloc-Tracker) | Deterministic heap diagnostics | — | — | [README](https://github.com/Vanderhell/MCU-Malloc-Tracker#readme) |

**Total: 4,391 lines of engine code · 269 tests · 9 libraries**

## Design philosophy

Every library in the toolkit follows the same rules:

1. **C99** — no extensions, no C11+. Compiles with gcc, clang, armcc, iccarm.
2. **Zero dependencies** — each library depends only on the C standard library (`stdint.h`, `string.h`, etc.).
3. **Zero dynamic allocation** — no `malloc`, ever. All state is caller-provided.
4. **ROM-friendly** — definitions and policies are `const` and can live in flash.
5. **Portable** — no platform-specific code. Platform integration via callbacks (clock, I/O, output).
6. **Two files** — one `.h`, one `.c`. Drop into any build system.
7. **Tested** — every library has a comprehensive test suite that passes with `-Wall -Wextra -Wpedantic -Werror`.
8. **Documented** — README, API reference, design rationale, and porting guide for each library.

## How they work together

The libraries are independent — you can use any one without the others.
But they're designed to compose:

```
                 ┌──────────┐
    Sensors ────▶│ micoring  │──── event queue ────▶ microfsm
                 └──────────┘                        │
                                                     │ state transitions
                                                     ▼
                                                  microlog ──▶ UART / RTT
                                                     │
                                          ┌──────────┴──────────┐
                                          ▼                      ▼
                                      microres               microconf
                                    retry + breaker          load config
                                          │                      │
                                          ▼                      │
                                      microcbor                  │
                                    encode telemetry             │
                                          │                      │
                                          ▼                      │
                                      iotspool ◀─── endpoints ──┘
                                    queue for MQTT
                                          │
                                          ▼
                                     MQTT broker
```

**And microsh gives you runtime access to all of it:**

```
> fsm state
Current: ONLINE

> conf get mqtt_host
mqtt_host = broker.local

> log level debug
Log level set to DEBUG

> breaker status
Breaker: CLOSED (0 failures)

> ring count uart_rx
uart_rx: 12/64 elements
```

## Quick start

### Pick what you need

| I need to... | Use |
|-------------|-----|
| Manage device states (boot → connect → work → error) | microfsm |
| Retry failed network calls with backoff | microres |
| Store WiFi/MQTT config in flash with CRC | microconf |
| Log events with levels and module tags | microlog |
| Debug a running device over UART | microsh |
| Send compact sensor data over MQTT | microcbor |
| Buffer UART bytes or events from ISR | micoring |
| Queue MQTT messages through power loss | iotspool |
| Track heap usage on bare metal | MCU-Malloc-Tracker |

### Add to your project

Each library is two files. Copy them, or use git submodules:

```bash
git submodule add https://github.com/Vanderhell/microfsm.git  lib/microfsm
git submodule add https://github.com/Vanderhell/microres.git   lib/microres
git submodule add https://github.com/Vanderhell/microconf.git  lib/microconf
git submodule add https://github.com/Vanderhell/microlog.git   lib/microlog
git submodule add https://github.com/Vanderhell/microsh.git    lib/microsh
git submodule add https://github.com/Vanderhell/microcbor.git  lib/microcbor
git submodule add https://github.com/Vanderhell/micoring.git   lib/micoring
```

### CMake

```cmake
# Add each library you need
add_subdirectory(lib/microfsm)
add_subdirectory(lib/microlog)
# ...
target_link_libraries(my_firmware PRIVATE microfsm microlog ...)
```

## Example: IoT sensor node

A complete example showing all libraries working together is in
[`examples/iot-sensor-node/`](examples/iot-sensor-node/).

This simulates a temperature sensor that:
- Boots → connects → reads sensors → publishes via MQTT
- Handles disconnections with circuit breaker + retry
- Stores config in flash with CRC validation
- Logs everything with structured logging
- Encodes telemetry in CBOR
- Buffers events from ISR via ring buffer
- Provides a debug shell over UART

## Supported platforms

All libraries are tested on:

| Platform | Compiler | Notes |
|----------|----------|-------|
| Linux (x86_64) | gcc, clang | CI and development |
| ESP32 | xtensa-gcc, riscv-gcc | ESP-IDF v4.4+ |
| STM32 | arm-none-eabi-gcc, armclang | Cortex-M0/M3/M4/M7 |
| Zephyr | gcc | nRF52, STM32, native_posix |
| Arduino | avr-gcc, arm-gcc | AVR and ARM boards |
| Windows | MSVC (C11 mode) | Testing and simulation |
| macOS | clang | Development |

## Repository checklist

- [x] Clear value proposition and architecture overview
- [x] Quick-start and integration snippets
- [x] Example project in `examples/iot-sensor-node/`
- [x] MIT license
- [x] Maintainer attribution (Vanderhell)
- [x] GitHub-ready badges
- [ ] CI workflow badge (add after first GitHub Actions workflow is created)

## Topics for GitHub

`c`, `c99`, `embedded`, `iot`, `firmware`, `mcu`, `bare-metal`, `mqtt`, `modbus`, `state-machine`, `ring-buffer`, `cbor`, `logging`, `no-heap`, `zero-dependencies`
## License

All libraries are MIT licensed. Use them however you want.

## Author

Built by [Vanderhell](https://github.com/Vanderhell) — embedded systems
developer focused on industrial IoT, Modbus, and MQTT.


