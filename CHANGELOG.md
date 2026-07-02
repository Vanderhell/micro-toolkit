# Changelog

All notable changes to `micro-toolkit` will be documented in this file.

This repository had no local Git tags at audit time on 2026-07-02, so no
released version history is asserted here.

## Unreleased

### Added

- dependency compatibility matrix
- API migration notes
- integration cookbook
- troubleshooting guide
- verification guide
- contributing guide
- root and example CMake build entry points
- GitHub Actions CI and tag-only release workflow definitions

### Changed

- README now describes `micro-toolkit` as an integration/example repository
- removed stale line/test totals and broad unverified support claims
- IoT sensor node example updated to current inspected header contracts

### Release gates

- dependency refs must be pinned
- integration example must build against those refs
- configured CI gates must pass
- public docs must match verified evidence
