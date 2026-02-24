# wowx vs wow tool run — Design Analysis

This document analyses the relationship between `uvx`/`uv tool run` and `uv run` in uv, to inform the design of wow's equivalent commands.

**Status**: Focus is on `wowx`/`wow tool run` only. `wow run` (project-aware) is out of scope for now.

---

## uvx versus uv tool run analysis

### 1. Core Purpose

| Command | Purpose |
|---------|---------|
| **`uvx`** (alias for `uv tool run`) | Run CLI tools from Python packages in **temporary, isolated environments** |
| **`uv run`** | Run commands within a **project's virtual environment** (`.venv`) |

### 2. Environment Behavior

| Aspect | `uvx` | `uv run` |
|--------|-------|----------|
| **Virtual environment** | Temporary, cached in uv's cache directory | Persistent `.venv` in project root |
| **Lifetime** | Disposable (removed with `uv cache clean`) | Persistent across invocations |
| **Isolation** | Isolated from project, system, and other tools | Uses project environment with all project deps |
| **Project discovery** | Ignores project files (`pyproject.toml`, `uv.lock`) | Discovers and syncs project environment |

### 3. Project Awareness

**`uv run`** is **project-aware**:
- Discovers `pyproject.toml` / `uv.lock` in current/parent directories
- Syncs environment to match lockfile before running
- Can install the project itself in editable mode
- Respects extras (`--extra`, `--all-extras`) and dependency groups (`--group`, `--dev`)
- Supports workspace operations (`--package`, `--all-packages`)

**`uvx`** is **tool-focused**:
- No project discovery
- No lockfile handling
- Runs tool in isolation from any project context
- Designed for "one-off" tool invocations

### 4. Key CLI Options

**`uv run` unique options:**
```bash
--extra <EXTRA>           # Include optional dependencies
--all-extras              # Include all extras
--group <GROUP>           # Include dependency groups
--dev / --no-dev          # Control dev dependencies
--module, -m              # Run as python -m <module>
--script, -s              # Run as PEP 723 script
--locked                  # Assert lockfile won't change
--frozen                  # Don't update lockfile
--package <PKG>           # Run in specific workspace member
--all-packages            # Run with all workspace members
--no-project              # Skip project discovery
--exact                   # Exact sync (remove extraneous packages)
```

**`uvx` unique options:**
```bash
--from <PKG>              # Use different package for command
--constraints, -c         # Constrain versions
--overrides               # Override versions
--build-constraints, -b   # Constrain build deps
--lfs                     # Enable Git LFS support
--torch-backend           # PyTorch backend selection
```

**Common options** (both support):
```bash
--with <PKG>              # Add extra dependencies
--with-editable <PATH>    # Add editable dependencies
--with-requirements <FILE> # Add from requirements file
--python, -p              # Python interpreter to use
--isolated                # Force isolated environment
```

### 5. Usage Examples

**`uvx` — Run standalone tools:**
```bash
# Run ruff without installing
uvx ruff check .

# Run specific version
uvx ruff@0.6.0 --version

# Run command from different package
uvx --from httpie http GET httpie.io

# With additional dependencies
uvx --with mkdocs-material mkdocs build
```

**`uv run` — Run in project context:**
```bash
# Run project CLI
uv run my-cli-command

# Run Python with project installed
uv run python -c "import my_package"

# Run pytest with dev dependencies
uv run --dev pytest

# Run with extra dependencies
uv run --extra docs mkdocs build

# Run script with inline metadata (PEP 723)
uv run --script example.py
```

### 6. When to Use Which

| Use `uvx` when... | Use `uv run` when... |
|-------------------|----------------------|
| Running standalone tools (ruff, black, httpie) | Working on a project |
| One-off tool invocations | Tool needs access to project code |
| You don't want to install anything permanently | Running project tests (pytest) |
| Tool doesn't need project context | Need type checking with project types (mypy) |
| | Running project scripts or entry points |

### 7. Source Code Relationship

From `crates/uv-cli/src/lib.rs`:

- **`ToolRunArgs`** (used by `uvx`/`uv tool run`): Focused on tool execution with `--from`, constraints, overrides
- **`RunArgs`** (used by `uv run`): Focused on project execution with extras, groups, workspaces, locking

The `UvxArgs` struct simply wraps `ToolRunArgs` with a version flag:
```rust
pub struct UvxArgs {
    #[command(flatten)]
    pub tool_run: ToolRunArgs,
    #[arg(short = 'V', long, action = clap::ArgAction::Version)]
    pub version: Option<bool>,
}
```

### 8. Important Note from Documentation

> If you are running a tool in a project and the tool requires that your project is installed (e.g., when using `pytest` or `mypy`), you'll want to use `uv run` instead of `uvx`. Otherwise, the tool will be run in a virtual environment that is isolated from your project.

---

## Reference: uv tool --help

```
❯ uv tool --help
Run and install commands provided by Python packages

Usage: uv tool [OPTIONS] <COMMAND>

Commands:
  run           Run a command provided by a Python package
  install       Install commands provided by a Python package
  upgrade       Upgrade installed tools
  list          List installed tools
  uninstall     Uninstall a tool
  update-shell  Ensure that the tool executable directory is on the `PATH`
  dir           Show the path to the uv tools directory

Cache options:
  -n, --no-cache               Avoid reading from or writing to the cache, instead using a temporary directory for the duration of the operation [env: UV_NO_CACHE=]
      --cache-dir <CACHE_DIR>  Path to the cache directory [env: UV_CACHE_DIR=]

Python options:
      --managed-python       Require use of uv-managed Python versions [env: UV_MANAGED_PYTHON=]
      --no-managed-python    Disable use of uv-managed Python versions [env: UV_NO_MANAGED_PYTHON=]
      --no-python-downloads  Disable automatic downloads of Python. [env: "UV_PYTHON_DOWNLOADS=never"]

Global options:
  -q, --quiet...                                   Use quiet output
  -v, --verbose...                                 Use verbose output
      --color <COLOR_CHOICE>                       Control the use of color in output [possible values: auto, always, never]
      --native-tls                                 Whether to load TLS certificates from the platform's native store [env: UV_NATIVE_TLS=]
      --offline                                    Disable network access [env: UV_OFFLINE=]
      --allow-insecure-host <ALLOW_INSECURE_HOST>  Allow insecure connections to a host [env: UV_ALLOW_INSECURE_HOST=]
      --no-progress                                Hide all progress outputs [env: UV_NO_PROGRESS=]
      --directory <DIRECTORY>                      Change to the given directory prior to running the command [env: UV_WORKING_DIR=]
      --project <PROJECT>                          Discover a project in the given directory [env: UV_PROJECT=]
      --config-file <CONFIG_FILE>                  The path to a `uv.toml` file to use for configuration [env: UV_CONFIG_FILE=]
      --no-config                                  Avoid discovering configuration files (`pyproject.toml`, `uv.toml`) [env: UV_NO_CONFIG=]
  -h, --help                                       Display the concise help for this command

Use `uv help tool` for more details.
```

---

## Design Decisions for wow

### 1. Command Structure

**Decision**: Match uv exactly.

- `wowx <gem>` — alias for `wow tool run`
- `wow tool run <gem>` — full command
- `wow tool install <gem>` — install tool persistently
- `wow tool list` — list installed tools
- `wow tool uninstall <gem>` — remove installed tool
- `wow tool upgrade <gem>` — upgrade installed tool

`wow run` (project-aware) is **out of scope for now**.

### 2. Configuration System

**Decision**: Follow uv's pattern — env vars + XDG-compatible config file.

Every CLI option (where it makes sense) gets a corresponding `WOW_` environment variable:

```bash
# Examples following uv's pattern:
WOW_CACHE_DIR=/tmp/wow-cache
WOW_NO_CACHE=1
WOW_OFFLINE=1
WOW_RUBY=3.2             # Ruby version for tool environments
WOW_COLOR=never
```

**Implementation location**: `src/util.c` / `include/wow/common.h`

**Config file**: XDG-compliant `settings.json` (or similar):
- `$XDG_CONFIG_HOME/wow/settings.json` (usually `~/.config/wow/settings.json`)
- Falls back to platform-appropriate locations on macOS/Windows

**Priority order** (highest to lowest):
1. CLI flags
2. Environment variables
3. Config file
4. Built-in defaults

### 3. wowx Specific Features

**`--from` support**: When gem name differs from command name:
```bash
wowx --from httparty http
```

**`@` version syntax**: Specify version inline (exact version or `latest` only for now):
```bash
wowx rspec@3.12.0
wowx rubocop@latest
# Compound constraints deferred to later phase:
# wowx rspec@'>= 3.0, < 4.0'
```

**Combined**:
```bash
wowx --from 'httparty>=1.0' http
```

### 4. Open Questions

| Topic | Status | Notes |
|-------|--------|-------|
| Binstubs | **Not needed** | Use shims via `wow tool update-shell` (like uv) instead of binstubs |
| Cache location | **Current** | `$XDG_CACHE_HOME/wowx/` — already implemented in `src/wowx_main.c` |
| Tool directory | **Future** | `$XDG_DATA_HOME/wow/tools/` for `wow tool install` (Phase 9+) |

---

## Implementation Notes

### Key Files to Modify/Create

| File | Purpose | Phase |
|------|---------|-------|
| `src/wowx_main.c` | wowx entry point, argument parsing | 8 (current) |
| `src/tool/cmd.c` | `wow tool` subcommand dispatch | 9+ (future) |
| `src/tool/run.c` | `wow tool run` implementation | 9+ (future) |
| `src/tool/install.c` | `wow tool install` implementation | 9+ (future) |
| `src/util.c` | Configuration loading (env vars + XDG settings) | 9+ (future) |
| `include/wow/common.h` | Config structures and defaults | 9+ (future) |

### Configuration API (Phase 9+)

**Note**: This is future design for when we implement `wow tool install` and the full config system. Current focus (Phase 8) is core resolver and test harness.

```c
// include/wow/common.h
typedef struct wow_config {
    char *cache_dir;
    bool no_cache;
    bool offline;
    char *ruby_version;
    // ... etc
} wow_config;

// src/util.c
int wow_config_load(wow_config *cfg, int argc, char **argv);
const char *wow_config_get(wow_config *cfg, const char *key);
```

### Current Cache Behavior (Phase 8)

Already implemented in `src/wowx_main.c`:
- Cache location: `$XDG_CACHE_HOME/wowx/` (falls back to `~/.cache/wowx/`)
- Tool installs (future): `$XDG_DATA_HOME/wow/tools/` (Phase 9+)
- Shims (future): `$XDG_BIN_HOME` or `~/.local/bin/` (Phase 9+)

---

**Last updated**: 2026-02-21
