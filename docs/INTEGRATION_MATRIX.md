# Integration Matrix

Audit date: 2026-07-02

This repository does not pin dependency refs today. Every `Compatible ref in
micro-toolkit` field therefore remains `UNPINNED` until a lock file or
submodule strategy is added.

| Library | Repository | Header | Expected package/target | Compatible ref in micro-toolkit | Inspected upstream ref | API generation | Integration status | Verified status | Unsupported claims to avoid |
|---------|------------|--------|-------------------------|---------------------------------|------------------------|----------------|--------------------|-----------------|-----------------------------|
| `microfsm` | <https://github.com/Vanderhell/microfsm> | `mfsm.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `6eb051c28a90dffc091c21d21c16e8d98fc71718` | status-query API generation | Example updated to checked `mfsm_validate`, `mfsm_init`, `mfsm_current`, `mfsm_state_name` | Not verified in this repo | blanket portability or hardware support claims |
| `microres` | <https://github.com/Vanderhell/microres> | `mres.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `99388579be7b0b25cb8032c225ed581ebd0e2471` | platform-structured API generation | Example updated to `mres_platform_t`, `mres_retry_exec`, `mres_breaker_call` with operation result output | Not verified in this repo | direct internal-field access, implicit jitter, old context-free signatures |
| `microconf` | <https://github.com/Vanderhell/microconf> | `mconf.h` | TODO: no local package metadata documented | `UNPINNED` | `master` / `7e5e57f072dc4de2d8ecf12cd0229edf7af8ed86` | caller-owned context generation | Example updated to `mconf_t`, sized entry macros, context-aware I/O, and two-slot metadata | Not verified in this repo | old pointer-only defaults, old raw load/save contract, unscoped flash guarantees |
| `microlog` | <https://github.com/Vanderhell/microlog> | `mlog.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `81745e558ee435748f690c3ea68eba2064b1e9ab` | stable backend layout | Example updated to initialize the global logger directly and register a copied backend descriptor | Not verified in this repo | reliable-delivery claims, borrowed-buffer lifetime claims beyond callback scope |
| `microsh` | <https://github.com/Vanderhell/microsh> | `msh.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `eb429b2aae652ff4c17834d73e9bd490548b0f7c` | strict registration generation | Example updated to avoid double-registering built-in help and to check `msh_exec` / `msh_register` | Not verified in this repo | direct ISR feed claim, persistent argv/storage claim |
| `microcbor` | <https://github.com/Vanderhell/microcbor> | `mcbor.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `17d646c09e63ebb8a5d73c98a27af8f99acba44e` | typed transactional API generation | Example checks every encode step and validates a single top-level payload with `mcbor_validate_one` | Not verified in this repo | `mcbor_enc_size` as sole success proof, canonical-CBOR claim |
| `micoring` | <https://github.com/Vanderhell/micoring> | `mring.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `a6f91ee2991a77f5d7096366ef46f7e8775d3bc7` | sized-init generation | Example updated to `mring_init(ring, storage, storage_size, capacity, elem_size)` and status-returning queries | Not verified in this repo | universal ISR-safe / lock-free claim under default config |
| `microtimer` | <https://github.com/Vanderhell/microtimer> | `mtimer.h` | TODO: no local package metadata documented | `UNPINNED` | `master` / `bac53d95b08a68c6a4f4dd84b4dd9346e141bf98` | ABI-safe init wrapper generation | Example updated to `mtimer_init`, checked create/start, and status-returning query APIs | Not verified in this repo | mutation-safe callbacks claim, unverified cross-platform support |
| `microbus` | <https://github.com/Vanderhell/microbus> | `mbus.h` | TODO: no local package metadata documented | `UNPINNED` | `v1.0.0` / `9ed989a4a9e3f2cfe18f70d36e806700b5d2e9f2` | ABI-safe init wrapper generation | Example updated to checked init/subscribe/publish/queue/dispatch and non-zero topics | Not verified in this repo | unbounded dispatch, topic zero publish, default ISR-safe queue claim |

## Current Aggregator Gaps

- No pinned compatibility manifest.
- No submodules.
- No local proof that the currently inspected upstream refs build together.
- No install-package naming contract documented by the upstream repos in this
  repository.

## Required Next Step Before Release

Replace `UNPINNED` with exact refs that are built by CI in this repository.
