# IoT Sensor Node Example

This example is a Linux-first integration sample that exercises the current
public headers of:

- `microconf`
- `microfsm`
- `micoring`
- `microres`
- `microcbor`
- `microlog`
- `microsh`
- `microtimer`
- `microbus`

It is not a hardware verification artifact. ESP32 notes in this folder are a
porting recipe only until you build and test them yourself.

## Build Inputs

By default the root build expects sibling dependency checkouts under
`lib/<name>` at the repository root.

Example:

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

## Build With CMake

From the repository root:

```bash
cmake -S . -B build -DMICRO_TOOLKIT_BUILD_EXAMPLES=ON
cmake --build build --target micro_toolkit_iot_sensor_node
```

Run on Linux:

```bash
./build/examples/iot-sensor-node/micro_toolkit_iot_sensor_node
```

You can override dependency paths individually:

```bash
cmake -S . -B build \
  -DMICRO_TOOLKIT_MICROFSM_DIR=/path/to/microfsm \
  -DMICRO_TOOLKIT_MICRORES_DIR=/path/to/microres
```

## What The Example Covers

- `microconf`: caller-owned `mconf_t`, validated schema, explicit default sizes,
  context-aware I/O callbacks, and two-slot storage metadata.
- `microfsm`: `mfsm_validate`, checked `mfsm_init`, and status-returning query
  APIs.
- `micoring`: sized `mring_init` and status-returning query helpers.
- `microres`: `mres_platform_t`, retry seed/state, separate library status from
  operation result, and no direct internal-field access.
- `microcbor`: checked encoder operations plus `mcbor_validate_one` before
  publish.
- `microlog`: backend registration on the global logger without copying
  instance storage into `mlog_global()`.
- `microsh`: checked registration and execution results, built-in help slot not
  double-registered.
- `microtimer`: ABI-safe `mtimer_init` wrapper and status-returning query APIs.
- `microbus`: checked init/subscribe/publish/queue/dispatch return values and
  topic-zero avoidance.

## Porting Notes

- The checked-in source uses a Linux simulation loop.
- `ESP_PLATFORM` branches are intentionally minimal and should be treated as a
  starting point, not as proof of support.
- The default checked-in `micoring` config is single-context, so ISR-to-main
  concurrency claims must remain scoped until you switch to an atomic
  configuration in the dependency itself.

## Manual Verification

See [../../docs/VERIFICATION.md](../../docs/VERIFICATION.md) for the exact
commands and the current not-verified list.
