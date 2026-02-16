# wow — Progress Checkpoint

> Last updated: 2026-02-16

## Phase 0: Research — COMPLETE

All four sub-phases done, cross-reviewed (Claude did 0a/0b, Kimi did 0c/0d, both reviewed each other's work).

| Sub-phase | Deliverable | Status | Key findings |
|-----------|-------------|--------|--------------|
| **0a** | `docs/research/gem_format.md` | Done | .gem is ustar tar (not gzip'd outer). 3 entries: metadata.gz, data.tar.gz, checksums.yaml.gz. No pax extensions needed — longest path tested is 79 chars. |
| **0b** | `docs/research/lockfile_format.md` | Done | Full format spec (1070 lines). All sections documented incl. CHECKSUMS (Bundler 4+). wow writes modern format only (separate GEM blocks, 2-space indent). Reads both legacy and modern. |
| **0c** | `docs/research/rubygems_api.md` | Done | `/api/v1/dependencies` is deprecated. Use Compact Index (`/info/{name}`) — what Bundler 2.x uses. Also need `/versions` endpoint for gem discovery. |
| **0d** | `docs/research/ruby_binaries.md` | Done | MVP is binary-only (ruby-builder GitHub releases). Source compilation is post-MVP. ruby-builder tarballs have `x64/` prefix to strip. |

### Working demos (CosmoCC, `-Werror` clean)

| Demo | Proves |
|------|--------|
| `demos/demo_tar.c` | ustar parsing + gzip decompression via Cosmopolitan's zlib |
| `demos/demo_lockparse.c` | Gemfile.lock state-machine parser (all sections) |
| `demos/demo_compact_index.c` | Compact index text format parser |
| `demos/demo_platform.c` | Platform detection + ruby-builder URL mapping |

### Key decisions made during Phase 0

1. **Lockfile writer**: Bundler 4+ style (2-space indent), modern format (separate GEM blocks per source)
2. **Lockfile reader**: Accept both legacy and modern formats, both 2-space and 3-space indent
3. **Checksum parsing**: Generic prefix parsing (split on `=`), verify `sha256=` only, warn on unknown
4. **Ruby install (MVP)**: Binary-only from ruby-builder. Clear error if no binary for platform. No source compilation.
5. **Compact Index**: Primary API for dependency resolution. Fallback to `/api/v1/versions/{name}.json`.
6. **YAML metadata parsing**: Use Cosmopolitan's libyaml. Ignore `!ruby/object:*` tags — treat as plain maps.

## Phase 1: Skeleton — NOT STARTED

Next up. See `docs/plan/PLAN_PHASE1.md`.

## Phases 2–8: NOT STARTED

See `docs/plan/PLAN_PHASE{2..8}.md`.

## Git log

```
030baf5 Phase 0: research docs + working CosmoCC demos
3c005df Initial commit: .gitignore and project plan docs
```
