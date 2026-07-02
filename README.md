# micro-toolkit

`micro-toolkit` is an integration, documentation, and example repository for the
`micro*` C99 libraries maintained under the Vanderhell GitHub account.

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)
![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)
![Domain: Embedded](https://img.shields.io/badge/Domain-Embedded%20IoT-orange.svg)
![Memory: Caller Owned](https://img.shields.io/badge/Memory-Caller%20Owned-critical.svg)

No third-party runtime dependencies in the documented `micro*` libraries where
their own upstream READMEs state that contract. This repository does not pin or
vendor those libraries automatically.

## What This Repository Is

- An integration layer for the `micro*` libraries.
- A place to document cross-library migration issues after upstream API changes.
- A home for a multi-library example under `examples/iot-sensor-node/`.

## What This Repository Is Not

- It is not the source of truth for any individual library API.
- It does not prove hardware support by itself.
- It does not currently pin a compatible dependency set.

Use these docs before copying the example into a project:

- [Integration Matrix](docs/INTEGRATION_MATRIX.md)
- [API Migration](docs/API_MIGRATION.md)
- [Cookbook](docs/COOKBOOK.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Verification](docs/VERIFICATION.md)

## Core Libraries

| Library | Repository | Public header | Toolkit status |
|---------|------------|---------------|----------------|
| `microfsm` | <https://github.com/Vanderhell/microfsm> | `mfsm.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microres` | <https://github.com/Vanderhell/microres> | `mres.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microconf` | <https://github.com/Vanderhell/microconf> | `mconf.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microlog` | <https://github.com/Vanderhell/microlog> | `mlog.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microsh` | <https://github.com/Vanderhell/microsh> | `msh.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microcbor` | <https://github.com/Vanderhell/microcbor> | `mcbor.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `micoring` | <https://github.com/Vanderhell/micoring> | `mring.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microtimer` | <https://github.com/Vanderhell/microtimer> | `mtimer.h` | Documented, example updated against inspected header, not locally verified in this repo |
| `microbus` | <https://github.com/Vanderhell/microbus> | `mbus.h` | Documented, example updated against inspected header, not locally verified in this repo |

## Dependency Layout

The root CMake build expects dependency source trees under `lib/<name>` by
default:

```text
micro-toolkit/
  lib/
    microfsm/
    microres/
    microconf/
    microlog/
    microsh/
    microcbor/
    micoring/
    microtimer/
    microbus/
```

You can override every dependency location with cache variables such as
`MICRO_TOOLKIT_MICROFSM_DIR` and `MICRO_TOOLKIT_MICROBUS_DIR`.

## Build Entry Points

- Root build: [CMakeLists.txt](CMakeLists.txt)
- Example build: [examples/iot-sensor-node/CMakeLists.txt](examples/iot-sensor-node/CMakeLists.txt)
- CI definition: [.github/workflows/ci.yml](.github/workflows/ci.yml)

Manual Linux configure example:

```bash
cmake -S . -B build -DMICRO_TOOLKIT_BUILD_EXAMPLES=ON
cmake --build build --target micro_toolkit_iot_sensor_node
```

## Support Matrix

The table below is intentionally conservative. It distinguishes documented
recipes from verified evidence.

| Surface | Evidence in this repo | Status |
|---------|-----------------------|--------|
| Linux GCC source-tree integration build | `ci.yml` configures a GCC build path | Not verified in this audit run |
| Linux Clang source-tree integration build | `ci.yml` configures a Clang build path | Not verified in this audit run |
| Windows / MSVC | No local consumer build or CI job here | Not verified |
| macOS | No local consumer build or CI job here | Not verified |
| ESP32 | Example notes and platform recipe only | Not verified |
| STM32 | No build recipe in this repo | Not verified |
| Zephyr | No build recipe in this repo | Not verified |
| Arduino | No build recipe in this repo | Not verified |

## Example

The integration example is in
[`examples/iot-sensor-node/`](examples/iot-sensor-node/). It is intended to
show current API usage patterns across all nine core libraries, but it still
depends on a caller-supplied dependency checkout layout and manual build
verification.

## Release Discipline

Do not tag or release this repository until all of the following are true:

- dependency refs are pinned in the integration matrix
- the example builds against that pinned set
- CI is green for the configured integration gates
- public claims match documented evidence

See [CHANGELOG.md](CHANGELOG.md) and [Verification](docs/VERIFICATION.md).

## License

This repository is MIT licensed. Individual library repositories retain their
own licenses and release processes.
