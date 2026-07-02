# Cookbook

## 1. Choose Libraries Deliberately

Pick only the `micro*` libraries your application actually needs. This
repository is an aggregator, not a requirement to consume all nine libraries.

## 2. Pin Compatible Versions

Do not consume floating dependency heads in production. Record exact commits or
tags in your project and mirror them into `docs/INTEGRATION_MATRIX.md`.

## 3. Use a Local `lib/<name>` Layout

Recommended source-tree layout:

```text
your-project/
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

## 4. Use Installed Packages Carefully

If you build against installed packages, keep package names and generated
config headers synchronized with the installed libraries. This repository does
not currently publish authoritative package names for every dependency.

## 5. Build the IoT Sensor Example

```bash
cmake -S . -B build -DMICRO_TOOLKIT_BUILD_EXAMPLES=ON
cmake --build build --target micro_toolkit_iot_sensor_node
```

## 6. microconf Config Flow

- define a caller-owned config struct
- describe it with `MCONF_ENTRY_SCALAR`, `MCONF_ENTRY_STRING`, and related macros
- create `mconf_t`
- initialize with `mconf_init`
- load with context-aware `mconf_io_t`
- fall back to `mconf_load_defaults` if storage is empty or invalid

## 7. micoring Handoff

The checked-in `micoring` config is single-context. Treat the example ring as a
main-loop handoff unless you rebuild the dependency with a concurrency mode that
matches your ISR model.

## 8. microtimer to microbus Event Flow

Use timer callbacks to queue a compact signal or payload, then dispatch the bus
from a non-callback owner context.

## 9. microbus to microfsm Event Flow

Translate bus notifications into explicit FSM events. Keep transition logic in
the state machine rather than in subscriber side effects.

## 10. microres Around Publish

Wrap the publish operation in `mres_retry_exec`, then place that wrapper behind
`mres_breaker_call` so breaker state reflects the higher-level publish result.

## 11. microcbor Telemetry Encode and Validate

- initialize `mcbor_enc_t`
- check every `mcbor_enc_*` call
- query final size
- verify no overflow
- call `mcbor_validate_one` before transport

## 12. microlog Backend Setup

- initialize the logger from `mlog_global()`
- register a backend
- keep backend context storage alive for the logger lifetime
- do not hold onto the callback buffer after return

## 13. microsh Command Setup

- initialize the shell
- do not register `help` yourself
- register commands only after deciding prompt and output callback ownership
- treat argv storage as borrowed temporary memory

## 14. Add One Library At A Time

When migrating an older project, integrate one library, verify it, then add the
next. This repository’s combined example is useful for patterns, but staged
adoption is safer.

## 15. Update After Upstream API Migration

- inspect the upstream header first
- update the integration matrix
- update the migration notes
- rebuild the example against the pinned set
- correct README claims before release

## 16. Release Readiness Checklist

- dependency refs pinned
- example build green for the pinned set
- CI green on configured gates
- verification doc updated
- README claims match evidence
