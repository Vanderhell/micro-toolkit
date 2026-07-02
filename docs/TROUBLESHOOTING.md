# Troubleshooting

## Old API Compile Errors After Library Hardening

If an example or downstream project still uses stale signatures, check
[API_MIGRATION.md](API_MIGRATION.md) first. This repository previously carried
older usage patterns.

## Missing `lib/<name>` Paths

The root CMake build expects `lib/<name>` by default. Either create that layout
or override the cache variables such as `MICRO_TOOLKIT_MICROFSM_DIR`.

## Installed Package Not Found

Installed package names are not yet documented as authoritative in this
repository. Prefer explicit source-tree paths until your environment has a
stable package contract.

## Stale `MCONF_ENTRY` Default Macro

Current `microconf` uses explicit-size entry macros such as
`MCONF_ENTRY_SCALAR` and `MCONF_ENTRY_STRING`. Old pointer-only defaults no
longer describe the full storage contract.

## `mring_init` Missing Storage Size

Current `micoring` expects:

```c
mring_init(ring, storage, storage_size, capacity, elem_size)
```

Not the old four-argument form.

## `mres_breaker_call` Signature Changed

Current `microres` requires a platform bundle and an operation-result output:

- breaker pointer
- operation callback
- operation context
- `mres_platform_t`
- `int *operation_result`

## `mfsm_current` / `mfsm_state_name` Are Status APIs

Both APIs now return `mfsm_err_t` and write outputs through pointers. Do not
compare them directly to a state id or assume fallback strings.

## `mcbor_enc_size` Alone Does Not Prove Complete CBOR

Always check individual encode statuses, confirm overflow is false, and validate
the top-level payload with `mcbor_validate_one`.

## microsh Help Slot Capacity

`MSH_MAX_COMMANDS` already includes the built-in `help` command. If you fill all
slots with user commands, registration will fail.

## microlog Variadic Macro Behavior

Use the provided macros or `mlog_log` with normal C99 formatting. Do not depend
on non-standard variadic macro tricks or on asynchronous ownership of the write
buffer.

## Feature-Flag ABI Mismatch

Some libraries embed configuration into public headers. Mixing objects built with
different config headers can break ABI expectations even if the code compiles.

## CI Builds The Example But Not Hardware

Even after CI is enabled, Linux source-tree builds do not prove hardware HALs,
timing behavior, or ESP32/STM32 integrations.

## Platform Support Not Verified

If README or project notes suggest a platform, check
[VERIFICATION.md](VERIFICATION.md) for evidence. In this repository most
non-Linux surfaces remain `Not verified`.
