# Contributing

`micro-toolkit` should stay small and honest.

## Rules

- treat this repository as an aggregator, not as the source of truth for
  upstream library APIs
- do not vendor mutable dependency heads silently
- update `docs/INTEGRATION_MATRIX.md` whenever a sublibrary API changes
- update `docs/API_MIGRATION.md` when example code or integration docs change
- do not claim platform support, ISR safety, lock freedom, or verification
  without evidence
- keep examples caller-owned and heap-free unless the upstream library contract
  requires something else
- do not tag or release until dependency refs are pinned and CI proves the
  configured integration gates

## Before Opening A PR

- review `docs/VERIFICATION.md`
- check whether README claims still match evidence
- keep release work under `Unreleased` in `CHANGELOG.md`
