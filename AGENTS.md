# AGENTS.md

## Cursor Cloud specific instructions

This repo is a **Zephyr T2 workspace** (west manifest repo + the `zcyphal` module). The
"application" is Zephyr firmware that is built and run on the **`native_sim`** simulator via
`west`. There is no web server or database. Standard setup steps live in `README.md`; only the
non-obvious, durable notes are captured here.

### What the update script already handles
On each session start the update script runs `uv sync` (Python tooling incl. `west`) and
`uv run west update --narrow --fetch-opt=--depth=1` (fetches Zephyr v4.4.0 into `deps/`). You do
**not** need to re-run these unless `pyproject.toml` or `west.yml` changed.

### Already provisioned in the VM image (do not reinstall)
- `uv` is installed and symlinked into `/usr/local/bin`, so it is on `PATH` for any shell.
- Zephyr build system deps via apt: `ninja-build`, `device-tree-compiler`, `gperf`, `ccache`,
  `gcc-multilib`/`g++-multilib` (the 32-bit libs are required because `native_sim` defaults to a
  32-bit build).
- **Zephyr SDK 1.0.1** at `/opt/zephyr-sdk-1.0.1` (`/opt` is chowned to `ubuntu`).

### Required environment variables (gotcha)
`direnv` is **not** installed, so `.envrc` does **not** auto-load. The following are exported in
`~/.bashrc`, but if your shell does not source it, export them manually before building:
```bash
export ZEPHYR_BASE=/workspace/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
```

### Build / run / test / lint
All `west` commands must be prefixed with `uv run` (west lives in the uv venv).
```bash
# Build + run any app on the simulator (the produced binary is zephyr/zephyr.exe):
uv run west build -b native_sim -d /tmp/b <app_path>
/tmp/b/zephyr/zephyr.exe        # Ctrl-C / it self-stops; native_sim is interactive

# Tests use twister (the harness zcyphal's tests/ target):
uv run west twister -T <tests_dir> -p native_sim

# Lint (Python tooling): ruff and yamllint are available in the venv:
uv run ruff check .
uv run yamllint <files>
```

### Status caveat (important)
As of this setup the `zcyphal` module **source does not exist yet** (only `west.yml`,
`zephyr/module.yml`, and design/plan docs under `docs/`). `west.yml` does **not** yet pin the
`cy`/`libcanard` upstreams. Until the implementation plan
(`docs/superpowers/plans/2026-06-28-zcyphal-cyphal-zephyr-module.md`) is executed, there is no
`zcyphal` sample/test to build — verify the toolchain instead by building upstream Zephyr samples
(e.g. `deps/zephyr/samples/hello_world` or `deps/zephyr/samples/drivers/can/counter`) on
`native_sim`. CAN works on `native_sim` via `CONFIG_CAN_LOOPBACK`.
