# zcyphal

Zephyr [module][zephyr-module] integrating [Cyphal v1.1][cyphal-1.1] over CAN via
[`cy`][cy] and [`libcanard`][libcanard] v5.

All three upstream stacks are **alpha**; this module pins specific commits and tracks
them deliberately.

## Features (v0.1)

- West-importable module with pinned `cy` + `libcanard`
- `cy_can_zephyr` media glue (`cy_can_vtable_t` over Zephyr CAN)
- Context-based core (`zcyphal_t`) with per-instance heap, spin thread, and mutex
- Thin convenience API (`zcyphal_advertise` / `publish` / `subscribe`) plus `zcyphal_cy()` escape hatch
- `native_sim` + CAN loopback sample and Twister integration tests

## Setup

This repository is a Zephyr T2 workspace (west manifest repo + module). Python tooling is managed with [uv](https://docs.astral.sh/uv/); the shell environment is configured in [`.envrc`](.envrc) and is meant to be loaded automatically with [direnv](https://direnv.net/).

0. Install **uv** if you do not have it yet: https://docs.astral.sh/uv/getting-started/installation/

1. Create the Python virtual environment and install dependencies (including west):

   ```bash
   uv sync
   ```

2. Fetch the west workspace (Zephyr and module dependencies):

   ```bash
   uv run west update --narrow --fetch-opt=--depth=1
   ```

3. Install the **Zephyr SDK** matching this workspace's Zephyr version. From the repository root, with `ZEPHYR_BASE` pointing at the checked-out Zephyr tree:

   ```bash
   export ZEPHYR_BASE=$PWD/deps/zephyr
   uv run west sdk install -b /opt
   ```

4. Allow direnv to load the project environment (once per machine):

   ```bash
   direnv allow
   ```

## Import into your west workspace

Add to your manifest:

```yaml
  projects:
    - name: zcyphal
      url: https://github.com/Finwood/zcyphal
      revision: main
      path: modules/zcyphal
```

Or use this repository as your manifest root (T2 workspace).

## Kconfig highlights

| Option | Purpose |
|--------|---------|
| `CONFIG_ZCYPHAL` | Enable the module |
| `CONFIG_ZCYPHAL_CAN_LOOPBACK` | Set `CAN_MODE_LOOPBACK` (required for `native_sim` loopback) |
| `CONFIG_ZCYPHAL_CAN_FD` | Enable CAN FD TX path |
| `CONFIG_ZCYPHAL_HEAP_SIZE` | `sys_heap` backing store |
| `CONFIG_ZCYPHAL_NODE_HOME` | Base node home name (hwinfo suffix appended) |
| `CONFIG_ZCYPHAL_AUTO_INIT` | Boot-time init via `SYS_INIT` |

See `Kconfig` for TX/RX queue sizes, filter count, thread stack/priority, and spin slice.

## Build & run sample (`native_sim`)

```bash
export ZEPHYR_BASE=$PWD/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

uv run west build -b native_sim -d /tmp/zcyphal-sample samples/pub_sub
/tmp/zcyphal-sample/zephyr/zephyr.exe
```

The sample advertises `demo/counter`, publishes every 500 ms, and logs received bytes on loopback.

## Tests

```bash
uv run west twister -T tests/integration -p native_sim
```

## Documentation

- Design: `docs/superpowers/specs/2026-06-28-zcyphal-cyphal-zephyr-module-design.md`
- Implementation plan: `docs/superpowers/plans/2026-06-28-zcyphal-cyphal-zephyr-module.md`

## Deferred (post-v0.1)

- Multi-interface / redundant CAN
- UAVCAN v0 / DroneCAN sidecar
- DSDL / nunavut codegen
- First-class reliable publish / RPC wrappers
- Multiple independent instances (gateway); context API is structured for this

[zephyr-module]: https://docs.zephyrproject.org/latest/develop/modules.html
[cyphal-1.1]: https://forum.opencyphal.org/t/rfc-early-preview-of-cyphal-v1-1/2438
[libcanard]: https://github.com/OpenCyphal/libcanard
[cy]: https://github.com/OpenCyphal-Garage/cy
