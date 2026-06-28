# zcyphal

Zephyr [module][zephyr-module] to integrate [Cyphal v1.1][cyphal-1.1] with Zephyr.

The goal is to provide support for Cyphal v1.1 through [libcanard][] and [cy][] in a way that is easy and intuitive to use in Zephyr projects.
As all three projects (Cyphal v1.1, libcanard v5.0-alpha and cy) are still in alpha, don't expect this project to be more stable yet.

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

   This installs the SDK version from `deps/zephyr/SDK_VERSION` (see the [Zephyr SDK version compatibility matrix](https://github.com/zephyrproject-rtos/sdk-ng/wiki/Zephyr-Version-Compatibility#zephyr-sdk-version-compatibility-matrix)). The `-b /opt` base directory matches [`.envrc`](.envrc), which sets `ZEPHYR_SDK_INSTALL_DIR=/opt/` when a `zephyr-sdk-*` directory is present there.

   To install only the toolchains you need (smaller download), add `-t`, for example:

   ```bash
   uv run west sdk install -b /opt -t x86_64-zephyr-elf    # native_sim
   uv run west sdk install -b /opt -t arm-zephyr-eabi      # typical ARM MCUs
   ```

4. Allow direnv to load the project environment (once per machine):

   ```bash
   direnv allow
   ```

   On each `cd` into this repository, direnv runs `.envrc`, which activates the uv virtualenv, adds `scripts/` to `PATH`, and exports Zephyr/SDK variables (`ZEPHYR_TOOLCHAIN_VARIANT`, `ZEPHYR_SDK_INSTALL_DIR`, etc.). Without direnv, source the venv and export those variables manually before building.

[zephyr-module]: https://docs.zephyrproject.org/latest/develop/modules.html
[cyphal-1.1]: https://forum.opencyphal.org/t/rfc-early-preview-of-cyphal-v1-1/2438
[libcanard]: https://github.com/OpenCyphal/libcanard
[cy]: https://github.com/OpenCyphal-Garage/cy
