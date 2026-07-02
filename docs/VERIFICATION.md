# Verification

Audit date: 2026-07-02

Repository commit inspected at audit start:

- `micro-toolkit`: `c8ce799abb45a7ee2931d65c33cf0d57bfa6aa10`

Current upstream refs inspected for header contracts:

- `microfsm`: `6eb051c28a90dffc091c21d21c16e8d98fc71718` (`v1.0.0`)
- `microres`: `99388579be7b0b25cb8032c225ed581ebd0e2471` (`v1.0.0`)
- `microconf`: `7e5e57f072dc4de2d8ecf12cd0229edf7af8ed86` (`master`)
- `microlog`: `81745e558ee435748f690c3ea68eba2064b1e9ab` (`v1.0.0`)
- `microsh`: `eb429b2aae652ff4c17834d73e9bd490548b0f7c` (`v1.0.0`)
- `microcbor`: `17d646c09e63ebb8a5d73c98a27af8f99acba44e` (`v1.0.0`)
- `micoring`: `a6f91ee2991a77f5d7096366ef46f7e8775d3bc7` (`v1.0.0`)
- `microtimer`: `bac53d95b08a68c6a4f4dd84b4dd9346e141bf98` (`master`)
- `microbus`: `9ed989a4a9e3f2cfe18f70d36e806700b5d2e9f2` (`v1.0.0`)

## Per-Library Integration Status

| Library | Status |
|---------|--------|
| `microfsm` | Header inspected, toolkit integration not built in this audit |
| `microres` | Header inspected, toolkit integration not built in this audit |
| `microconf` | Header inspected, toolkit integration not built in this audit |
| `microlog` | Header inspected, toolkit integration not built in this audit |
| `microsh` | Header inspected, toolkit integration not built in this audit |
| `microcbor` | Header inspected, toolkit integration not built in this audit |
| `micoring` | Header inspected, toolkit integration not built in this audit |
| `microtimer` | Header inspected, toolkit integration not built in this audit |
| `microbus` | Header inspected, toolkit integration not built in this audit |

## Exact Manual Build Commands

Clone or place dependency source trees under `lib/<name>` or pass explicit
cache variables:

```bash
cmake -S . -B build -DMICRO_TOOLKIT_BUILD_EXAMPLES=ON
cmake --build build --target micro_toolkit_iot_sensor_node
```

Optional feature toggles exercised by CI configuration:

```bash
cmake -S . -B build \
  -DMICRO_TOOLKIT_BUILD_EXAMPLES=ON \
  -DMICRO_TOOLKIT_EXAMPLE_MLOG_COLOR=OFF \
  -DMICRO_TOOLKIT_EXAMPLE_MLOG_TIMESTAMP=OFF \
  -DMICRO_TOOLKIT_EXAMPLE_MSH_HISTORY=OFF \
  -DMICRO_TOOLKIT_EXAMPLE_MSH_COMPLETE=OFF \
  -DMICRO_TOOLKIT_EXAMPLE_MCBOR_FLOAT32=OFF
cmake --build build --target micro_toolkit_iot_sensor_node
```

## Not Verified

- Linux GCC build execution in this audit run
- Linux Clang build execution in this audit run
- Windows / MSVC consumer build
- macOS consumer build
- ESP32 build
- STM32 build
- Zephyr build
- Arduino build
- hardware timing, ISR behavior, and MQTT transport integration

## Incomplete / Release Blockers

- dependency refs are still `UNPINNED`
- no build was executed during this audit run
- example integration is updated to current headers but not proven against a
  built dependency set in this repository
- public release readiness must remain `not proven`
