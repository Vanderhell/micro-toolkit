# API Migration

Audit date: 2026-07-02

This document records the migration drift found in `micro-toolkit` and the
current header-level contract that the example and docs now target.

## microconf

Observed drift:

- old `MCONF_ENTRY(...)` usage without explicit default sizes
- schema/data access without a caller-owned `mconf_t`
- old callback signatures without callback context
- no explicit two-slot storage metadata

Current contract used here:

- `mconf_t` must be initialized with `mconf_init(ctx, ctx_size, schema, data, data_size)`
- use sized entry macros such as `MCONF_ENTRY_SCALAR` and `MCONF_ENTRY_STRING`
- `mconf_io_t` carries `callback_ctx`, `storage_size`, and `slot_size`
- load/save operate on `mconf_t *`

Repair instruction:

- keep config data in a caller-owned struct
- validate schema and init context before load/save
- check every `mconf_*` return value

## micoring

Observed drift:

- old `mring_init(ring, storage, capacity, elem_size)` signature
- implied universal ISR-safe / lock-free claims
- direct query assumptions instead of status-returning query APIs

Current contract used here:

- `mring_init(ring, storage, storage_size, capacity, elem_size)`
- query helpers return `mring_err_t` and write outputs through pointers
- checked-in config currently sets `MRING_CONCURRENCY_MODE` to
  `MRING_CONCURRENCY_SINGLE_CONTEXT`

Repair instruction:

- scope ISR-to-main-loop language to the chosen `micoring` concurrency mode
- use `mring_capacity`, `mring_count`, `mring_free`, and friends with checked
  status values

## microres

Observed drift:

- old breaker/retry calls without a platform bundle
- old signatures that did not separate library status from operation result
- reliance on mutable internals

Current contract used here:

- `mres_platform_t` carries clock, wait, and callback context
- `mres_retry_exec` and `mres_breaker_call` both separate `mres_err_t` from the
  operation result integer
- retry jitter state is explicit via `mres_retry_seed`

Repair instruction:

- treat `mres_err_t` as library status and the operation result as the wrapped
  business result
- do not read internal `mres_retry_t` or `mres_breaker_t` fields directly

## microfsm

Observed drift:

- old sentinel-returning current-state queries
- unvalidated init assumptions
- stale fallback assumptions around state names

Current contract used here:

- call `mfsm_validate` before `mfsm_init`
- `mfsm_current` writes the current state through an output pointer
- `mfsm_state_name` returns status and writes a name pointer for the current
  state
- same-instance reentrancy is busy-protected by the library

Repair instruction:

- check return values from `mfsm_validate`, `mfsm_init`, `mfsm_dispatch`,
  `mfsm_current`, and `mfsm_state_name`

## microlog

Observed drift:

- example assumed copying a logger instance into `*mlog_global()`
- stale claims around delivery guarantees
- weak callback lifetime notes

Current contract used here:

- initialize the global logger directly through `mlog_global()`
- `mlog_backend_t` is copied by value into the logger
- write callback receives borrowed stack-backed storage

Repair instruction:

- keep backend contexts caller-owned
- avoid reliable-delivery claims
- do not retain callback buffers after the callback returns

## microsh

Observed drift:

- built-in help command treated as a free extra slot
- no explicit overflow handling notes
- no checked command execution results

Current contract used here:

- `MSH_MAX_COMMANDS` includes the built-in help command
- `msh_exec` rejects overlong input with `MSH_ERR_INPUT_TOO_LONG`
- `msh_feed` is explicitly single non-ISR context

Repair instruction:

- do not double-register `help`
- do not claim that argv or command strings outlive the callback

## microcbor

Observed drift:

- success inferred only from `mcbor_enc_size` and overflow checks
- no top-level payload validation
- no float32-disabled path consideration

Current contract used here:

- every encode call returns `mcbor_err_t`
- `mcbor_validate_one` validates one complete top-level item
- `MCBOR_ENABLE_FLOAT32` can disable float encoding

Repair instruction:

- check every `mcbor_enc_*` call
- validate the finished payload before use
- provide a non-float fallback representation when needed

## microtimer

Observed drift:

- stale direct query APIs
- unchecked create/start/tick assumptions
- callback behavior not scoped

Current contract used here:

- `mtimer_init` is an inline wrapper over `mtimer_init_sized`
- query helpers return status and write outputs through pointers
- callbacks receive `(uint8_t timer_id, void *ctx)`

Repair instruction:

- check all create/start/tick/query calls
- avoid claiming callback-time mutation safety unless verified in upstream docs

## microbus

Observed drift:

- old init wording without ABI-safe wrapper
- size assumptions not called out
- topic-zero and queue guarantees not scoped

Current contract used here:

- `mbus_init` wraps `mbus_impl_init(bus, sizeof(*bus), clock)`
- publish and queue payload lengths are `size_t`
- topic `0x00` is reserved for `MBUS_TOPIC_ANY`, not a normal publish target
- dispatch and queue APIs return status/counts through `int`

Repair instruction:

- never publish topic zero
- check return values from subscribe/publish/queue/dispatch
- scope queue and ISR claims to the upstream `mbus_config.h` values
