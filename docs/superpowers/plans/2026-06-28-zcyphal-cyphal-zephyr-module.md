# zcyphal Cyphal v1.1 Zephyr Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement v0.1 of the `zcyphal` Zephyr module integrating Cyphal v1.1 over CAN (cy + libcanard v5) with a Zephyr CAN media glue, context-based core, managed spin thread, and native_sim loopback tests.

**Architecture:** West-import upstream `cy` and `libcanard`; build them from our module `CMakeLists.txt`. Implement `cy_can_zephyr` (Zephyr equivalent of `cy_can_socketcan`) behind `cy_can_vtable_t`. Wrap in `zcyphal_t` context struct with per-instance heap/thread/mutex; expose a thin single-instance convenience API on top.

**Tech Stack:** Zephyr v4.4.0, west, cy (OpenCyphal-Garage/cy @ `0a3ab4d`), libcanard v5 (@ `254b7160`), sys_heap, native_sim + CAN loopback, twister.

**Design spec:** `docs/superpowers/specs/2026-06-28-zcyphal-cyphal-zephyr-module-design.md`

---

## File map (created/modified by this plan)

| File | Responsibility |
|------|----------------|
| `west.yml` | Pin `cy` + `libcanard` west projects |
| `zephyr/module.yml` | Module build/kconfig/samples/tests registration |
| `CMakeLists.txt` | Compile upstream + glue + core |
| `Kconfig` | All `CONFIG_ZCYPHAL_*` options |
| `include/zcyphal/zcyphal.h` | Public medium API + context types |
| `include/zcyphal/heap.h` | sys_heap realloc wrapper API |
| `src/heap.c` | Per-instance sys_heap |
| `src/identity.c` | hwinfo → home suffix + prng_seed |
| `src/cy_can_zephyr.h` | Glue state + factory decl |
| `src/cy_can_zephyr.c` | `cy_can_vtable_t` over Zephyr CAN |
| `src/zcyphal.c` | Context core, spin thread, diag, convenience wrappers |
| `src/zcyphal_log.c` | `cy_trace` + diag → LOG |
| `samples/pub_sub/` | Runnable demo on native_sim loopback |
| `tests/integration/` | Twister tests t1–t5 |

---

### Task 1: West dependencies & module scaffolding

**Files:**
- Modify: `west.yml`
- Modify: `zephyr/module.yml`
- Create: `CMakeLists.txt`
- Create: `Kconfig`
- Create: `include/zcyphal/zcyphal.h` (stub)
- Create: `src/zcyphal.c` (stub)

- [ ] **Step 1: Update `west.yml`**

Add pinned upstream projects under `deps/`:

```yaml
manifest:
  self:
    path: .

  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos
    - name: opencyphal-garage
      url-base: https://github.com/OpenCyphal-Garage
    - name: opencyphal
      url-base: https://github.com/OpenCyphal

  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: v4.4.0
      clone-depth: 1
      west-commands: scripts/west-commands.yml
      import:
        path-prefix: deps
        name-allowlist:
          - cmsis_6

    - name: cy
      remote: opencyphal-garage
      repo-path: cy
      path: deps/cy
      revision: 0a3ab4d22f9f7109cd3e709306625d8d435700db
      submodules: true

    - name: libcanard
      remote: opencyphal
      path: deps/libcanard
      revision: 254b71601b2d9addf17db0284f4f04a929ff4902
```

- [ ] **Step 2: Update `zephyr/module.yml`**

```yaml
name: zcyphal

build:
  cmake: .
  kconfig: Kconfig

samples:
  - samples

tests:
  - tests
```

- [ ] **Step 3: Create minimal `CMakeLists.txt`**

```cmake
if(CONFIG_ZCYPHAL)
  zephyr_library()
  zephyr_library_sources(
    src/zcyphal.c
  )
  zephyr_include_directories(include)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/lib)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy_can)
  zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/deps/libcanard)
endif()
```

- [ ] **Step 4: Create minimal `Kconfig`**

```kconfig
menuconfig ZCYPHAL
	bool "Cyphal v1.1 over CAN (zcyphal)"
	select CAN
	select HWINFO
	help
	  Enable the zcyphal module integrating Cyphal v1.1 with Zephyr CAN.

if ZCYPHAL

config ZCYPHAL_HEAP_SIZE
	int "Cy heap size (bytes)"
	default 32768

config ZCYPHAL_NODE_HOME
	string "Default node home name"
	default "zcyphal"

endif
```

- [ ] **Step 5: Fetch deps and verify west sees the module**

Run:
```bash
cd /path/to/zcyphal
uv sync --all-groups
west update
west list | rg 'cy|libcanard|zcyphal'
```
Expected: all three projects listed.

- [ ] **Step 6: Commit**

```bash
git add west.yml zephyr/module.yml CMakeLists.txt Kconfig include/zcyphal/zcyphal.h src/zcyphal.c
git commit -m "feat: scaffold zcyphal module with west deps for cy and libcanard"
```

---

### Task 2: sys_heap allocator wrapper

**Files:**
- Create: `include/zcyphal/heap.h`
- Create: `src/heap.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Implement `include/zcyphal/heap.h`**

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

struct zcyphal_heap {
	uint8_t *buffer;
	size_t size;
	struct sys_heap heap;
};

int zcyphal_heap_init(struct zcyphal_heap *h, uint8_t *buffer, size_t size);
void *zcyphal_heap_realloc(struct zcyphal_heap *h, void *ptr, size_t size);
```

- [ ] **Step 2: Implement `src/heap.c`**

```c
#include <zcyphal/heap.h>
#include <sys/sys_heap.h>
#include <zephyr/kernel.h>

int zcyphal_heap_init(struct zcyphal_heap *h, uint8_t *buffer, size_t size)
{
	if (h == NULL || buffer == NULL || size == 0) {
		return -EINVAL;
	}
	h->buffer = buffer;
	h->size = size;
	sys_heap_init(&h->heap, buffer, size);
	return 0;
}

void *zcyphal_heap_realloc(struct zcyphal_heap *h, void *ptr, size_t size)
{
	if (h == NULL) {
		return NULL;
	}
	if (size == 0) {
		sys_heap_free(&h->heap, ptr);
		return NULL;
	}
	return sys_heap_realloc(&h->heap, ptr, size);
}
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Add `src/heap.c` to `zephyr_library_sources`.

- [ ] **Step 4: Commit**

```bash
git add include/zcyphal/heap.h src/heap.c CMakeLists.txt
git commit -m "feat: add per-instance sys_heap realloc wrapper"
```

---

### Task 3: Identity derivation (hwinfo)

**Files:**
- Create: `src/identity.c`
- Create: `include/zcyphal/identity.h`
- Modify: `CMakeLists.txt`, `Kconfig`

- [ ] **Step 1: Implement identity helpers**

`zcyphal_identity_build(home_buf, home_buf_len, prng_seed_out, home_base, discriminator)`:
- Read UID via `hwinfo_get_device_id()`.
- If UID available: `prng_seed = rapidhash(uid, uid_len, 0)` (include `rapidhash.h` from cy deps); append first 4 UID bytes as hex to `home_base` → `"zcyphal-a1b2c3d4"`.
- If UID unavailable: use `CONFIG_ZCYPHAL_PRNG_SEED` and bare `home_base`; `#warning` at compile time via `#pragma message` or runtime `LOG_WRN`.
- If `discriminator` non-NULL (future gateway nudge): append `"-" + discriminator` to home before hashing.

- [ ] **Step 2: Commit**

```bash
git add src/identity.c include/zcyphal/identity.h CMakeLists.txt Kconfig
git commit -m "feat: derive cy home and prng seed from hwinfo"
```

---

### Task 4: `cy_can_zephyr` media glue

**Files:**
- Create: `src/cy_can_zephyr.h`, `src/cy_can_zephyr.c`
- Modify: `CMakeLists.txt`

Reference implementation: upstream `cy_can/cy_can_socketcan.c` (same vtable contract).

- [ ] **Step 1: Define glue state in `src/cy_can_zephyr.h`**

```c
#pragma once

#include <cy_can.h>
#include <zephyr/device.h>
#include <zcyphal/heap.h>

struct cy_can_zephyr {
	const struct device *can_dev;
	struct zcyphal_heap *heap;
	struct k_msgq rxq;
	uint8_t rxq_buffer[CONFIG_ZCYPHAL_RX_QUEUE_SIZE * sizeof(struct can_frame)];
	int filter_ids[CONFIG_ZCYPHAL_FILTER_COUNT];
	size_t filter_id_count;
	bool fd_capable;
};

cy_platform_t *cy_can_zephyr_new(const struct device *can_dev,
				 struct zcyphal_heap *heap,
				 size_t tx_queue_capacity,
				 size_t filter_count,
				 uint64_t prng_seed);
void cy_can_zephyr_destroy(cy_platform_t *platform);
```

- [ ] **Step 2: Implement vtable functions in `src/cy_can_zephyr.c`**

Key mappings (mirror socketcan semantics):

**`now()`**
```c
static cy_us_t zcy_now(void *user)
{
	ARG_UNUSED(user);
	return (cy_us_t)k_ticks_to_us_floor64(k_uptime_ticks());
}
```

**`realloc()`** — delegate to `zcyphal_heap_realloc(((struct cy_can_zephyr *)user)->heap, ...)`.

**`tx_classic()` / `tx_fd()`**
```c
struct can_frame frame = {
	.id = can_id,
	.flags = CAN_FRAME_IDE | (fd ? CAN_FRAME_FDF : 0),
	.dlc = can_dlc_from_bytes(len),
};
memcpy(frame.data, data, len);
int err = can_send(dev, &frame, K_NO_WAIT, NULL, NULL);
return (err == 0); /* false on -EAGAIN so cy retries */
```

**RX filter callback** — push received `struct can_frame` into `rxq` via `k_msgq_put(..., K_NO_WAIT)`.

**`rx()`**
```c
k_timeout_t t = K_USEC(MAX(0, deadline - zcy_now(user)));
if (k_msgq_get(&self->rxq, &frame, t) == 0) {
	out->can_id = frame.id & CAN_EXT_ID_MASK;
	out->fd = (frame.flags & CAN_FRAME_FDF) != 0;
	out->len = can_dlc_to_bytes(frame.dlc);
	out->timestamp = zcy_now(user); /* or frame timestamp if CONFIG_CAN_RX_TIMESTAMP */
	memcpy(out->data, frame.data, out->len);
	return true;
}
return false;
```

**`filter()`** — remove old filters, install new `canard_filter_t` set via `can_add_rx_filter(dev, callback, user, &can_filter)`. If slots exhausted, log warning and add one accept-all filter.

**Init path in `cy_can_zephyr_new()`**
1. `can_set_mode(dev, CAN_MODE_NORMAL)` (+ FD if `CONFIG_ZCYPHAL_CAN_FD`).
2. `can_start(dev)`.
3. Install initial accept-all RX filter pushing to `rxq`.
4. Wire vtable (set `tx_fd = NULL` if not FD capable).
5. Call `cy_can_new(1, tx_queue_capacity, filter_count, prng_seed, &vtable, self)`.

- [ ] **Step 3: Add upstream sources to `CMakeLists.txt`**

```cmake
zephyr_library_sources(
  ${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy/cy.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/deps/cy/cy_can/cy_can.c
  ${ZEPHYR_CURRENT_MODULE_DIR}/deps/libcanard/canard.c
  src/cy_can_zephyr.c
  src/identity.c
)
```

- [ ] **Step 4: Commit**

```bash
git add src/cy_can_zephyr.c src/cy_can_zephyr.h CMakeLists.txt
git commit -m "feat: add cy_can_zephyr media glue over Zephyr CAN driver"
```

---

### Task 5: `zcyphal_t` context core + spin thread

**Files:**
- Modify: `include/zcyphal/zcyphal.h`
- Modify: `src/zcyphal.c`
- Create: `src/zcyphal_log.c`
- Modify: `Kconfig`, `CMakeLists.txt`

- [ ] **Step 1: Define context struct in `include/zcyphal/zcyphal.h`**

```c
#pragma once

#include <cy.h>
#include <zephyr/kernel.h>
#include <zcyphal/heap.h>

struct zcyphal_config {
	const struct device *can_dev;
	const char *home;
	const char *namespace_;
	const char *remap;
	const char *discriminator; /* optional, for future multi-instance */
};

typedef struct zcyphal {
	struct zcyphal_heap heap;
	uint8_t heap_mem[CONFIG_ZCYPHAL_HEAP_SIZE];
	cy_platform_t *platform;
	cy_t *cy;
	struct k_mutex lock;
	struct k_thread spin_thread;
	k_thread_stack_t spin_stack[CONFIG_ZCYPHAL_THREAD_STACK_SIZE];
	atomic_t running;
	cy_diag_t diag;
} zcyphal_t;

int zcyphal_init_ctx(zcyphal_t *ctx, const struct zcyphal_config *cfg);
void zcyphal_shutdown_ctx(zcyphal_t *ctx);
cy_t *zcyphal_cy_ctx(zcyphal_t *ctx);

/* Single-instance convenience (thin wrappers over default_ctx) */
int zcyphal_init(const struct zcyphal_config *cfg);
cy_t *zcyphal_cy(void);
```

Use a file-local `static zcyphal_t default_ctx` for convenience wrappers — **no other file-scope mutable globals**.

- [ ] **Step 2: Implement spin thread in `src/zcyphal.c`**

```c
static void zcyphal_spin_fn(void *p1, void *p2, void *p3)
{
	zcyphal_t *ctx = p1;
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (atomic_get(&ctx->running)) {
		k_mutex_lock(&ctx->lock, K_FOREVER);
		cy_spin_until(ctx->cy, cy_now(ctx->cy) + CONFIG_ZCYPHAL_SPIN_SLICE_US);
		k_mutex_unlock(&ctx->lock);
	}
}
```

All context API functions (`zcyphal_advertise_ctx`, etc.) lock `ctx->lock` around cy calls.

- [ ] **Step 3: Implement `zcyphal_init_ctx()`**

Sequence:
1. Resolve defaults (`can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))` if NULL).
2. `zcyphal_heap_init(&ctx->heap, ctx->heap_mem, sizeof(ctx->heap_mem))`.
3. Build identity → `cy_can_zephyr_new(...)`.
4. `cy_new(platform, home, namespace, remap)`.
5. Register diag listener (`src/zcyphal_log.c`).
6. `k_mutex_init(&ctx->lock)`.
7. `atomic_set(&ctx->running, 1)`.
8. `k_thread_create(..., zcyphal_spin_fn, ctx, ...)`.

- [ ] **Step 4: Expand `Kconfig`**

Add all options from design spec §8:
`ZCYPHAL_CAN_FD`, `ZCYPHAL_AUTO_INIT`, `ZCYPHAL_TX_QUEUE_SIZE` (default 128),
`ZCYPHAL_RX_QUEUE_SIZE` (default 32), `ZCYPHAL_FILTER_COUNT` (default 8),
`ZCYPHAL_THREAD_STACK_SIZE` (4096), `ZCYPHAL_THREAD_PRIORITY` (5),
`ZCYPHAL_SPIN_SLICE_US` (10000), `ZCYPHAL_NAMESPACE`, `ZCYPHAL_REMAP`,
`ZCYPHAL_PRNG_SEED`, `ZCYPHAL_TRACE`.

Optional `SYS_INIT` when `CONFIG_ZCYPHAL_AUTO_INIT=y`.

- [ ] **Step 5: Commit**

```bash
git add include/zcyphal/zcyphal.h src/zcyphal.c src/zcyphal_log.c Kconfig CMakeLists.txt
git commit -m "feat: add zcyphal context core with managed spin thread"
```

---

### Task 6: Medium convenience API

**Files:**
- Modify: `include/zcyphal/zcyphal.h`, `src/zcyphal.c`

- [ ] **Step 1: Add helpers**

```c
typedef void (*zcyphal_sub_cb_t)(void *user, cy_arrival_t arrival);

cy_publisher_t *zcyphal_advertise_ctx(zcyphal_t *ctx, const char *topic);
int zcyphal_publish_ctx(zcyphal_t *ctx, cy_publisher_t *pub,
			const void *data, size_t len, k_timeout_t timeout);
cy_future_t *zcyphal_subscribe_ctx(zcyphal_t *ctx, const char *topic,
				   size_t extent, zcyphal_sub_cb_t cb, void *user);
```

`zcyphal_publish_ctx`: compute `deadline = cy_now(cy) + k_timeout_to_us(timeout)` under lock, call `cy_publish`.

`zcyphal_subscribe_ctx`: call `cy_subscribe`, set future callback that invokes `cb(user, cy_arrival_borrow(future))` when done.

Thin wrappers without `_ctx` pass `&default_ctx`.

- [ ] **Step 2: Commit**

```bash
git add include/zcyphal/zcyphal.h src/zcyphal.c
git commit -m "feat: add zcyphal convenience publish/subscribe API"
```

---

### Task 7: Sample `samples/pub_sub`

**Files:**
- Create: `samples/pub_sub/CMakeLists.txt`, `prj.conf`, `src/main.c`, `README.md`

- [ ] **Step 1: Create `samples/pub_sub/prj.conf`**

```ini
CONFIG_ZCYPHAL=y
CONFIG_CAN=y
CONFIG_CAN_LOOPBACK=y
CONFIG_HWINFO=y
CONFIG_LOG=y
CONFIG_MAIN_STACK_SIZE=4096
```

- [ ] **Step 2: Create `samples/pub_sub/src/main.c`**

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zcyphal/zcyphal.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

static void on_msg(void *user, cy_arrival_t arrival)
{
	ARG_UNUSED(user);
	uint8_t buf[64];
	size_t n = cy_message_read(arrival.message.content, 0, sizeof(buf), buf);
	LOG_INF("rx %zu bytes", n);
}

int main(void)
{
	if (zcyphal_init(NULL) != 0) {
		LOG_ERR("init failed");
		return 1;
	}

	cy_publisher_t *pub = zcyphal_advertise("demo/counter");
	zcyphal_subscribe("demo/counter", 64, on_msg, NULL);

	uint8_t val = 0;
	while (1) {
		zcyphal_publish(pub, &val, 1, K_MSEC(500));
		val++;
	}
}
```

- [ ] **Step 3: Build and run on native_sim**

```bash
west build -b native_sim zcyphal/samples/pub_sub
west build -t run
```
Expected: alternating publish log lines and `rx 1 bytes`.

- [ ] **Step 4: Commit**

```bash
git add samples/pub_sub
git commit -m "feat: add pub_sub sample for native_sim CAN loopback"
```

---

### Task 8: Integration tests (twister)

**Files:**
- Create: `tests/integration/test_zcyphal/` (one test app per case or shared with testcase.yaml)

- [ ] **Step 1: Create `tests/integration/test_zcyphal/testcase.yaml`**

```yaml
tests:
  zcyphal.integration.init:
    platform_allow: native_sim
    tags: zcyphal
  zcyphal.integration.loopback:
    platform_allow: native_sim
    tags: zcyphal
  zcyphal.integration.canfd:
    platform_allow: native_sim
    extra_configs:
      - CONFIG_ZCYPHAL_CAN_FD=y
    tags: zcyphal
```

- [ ] **Step 2: Implement test cases using ztest**

**t1 init:** `zcyphal_init(NULL) == 0`, `zcyphal_cy() != NULL`, `zcyphal_shutdown` clean.

**t2 loopback pub/sub:** semaphore in callback; publish 5 bytes; `k_sem_take` within 2s; assert payload.

**t3 CAN FD:** payload 32 bytes, `CONFIG_ZCYPHAL_CAN_FD=y`.

**t4 filters:** subscribe topic A; verify filter count > 0 via glue accessor or log hook (minimal: init + subscribe succeeds without error).

**t5 thread safety:** second thread publishes while main waits on semaphore; no hang.

- [ ] **Step 3: Run twister**

```bash
west twister -T tests/integration -p native_sim
```
Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/integration
git commit -m "test: add native_sim loopback integration tests for zcyphal"
```

---

### Task 9: Documentation & README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README with**

- What zcyphal is, alpha upstream warning
- west import instructions (`west.yml` snippet for external apps)
- Kconfig options summary
- Build/run sample commands
- Link to design spec and this plan
- Deferred features list (§11 of spec)
- Gateway forward-compat note (§12)

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document zcyphal usage, config, and testing"
```

---

## Verification checklist (before calling v0.1 done)

- [ ] `west update` fetches cy (with submodules) and libcanard at pinned SHAs
- [ ] `west build -b native_sim zcyphal/samples/pub_sub` succeeds
- [ ] Sample runs and logs TX/RX on loopback
- [ ] `west twister -T tests/integration -p native_sim` all PASS
- [ ] No file-scope mutable globals except `default_ctx` in `zcyphal.c`
- [ ] Context struct holds all per-instance state (heap, platform, cy, thread, mutex, diag)
- [ ] CAN FD gated by `CONFIG_ZCYPHAL_CAN_FD`
- [ ] README documents alpha status and deferred scope

## Cloud agent environment notes

Zephyr build requires:
- Python 3.12+ with west (this repo uses `uv sync`)
- Zephyr SDK installed (see `.envrc`; cloud env may need `ZEPHYR_SDK_INSTALL_DIR`)
- After clone: `west update` then build against `deps/zephyr`

If SDK is unavailable in cloud VM, at minimum verify: west update succeeds, CMake/Kconfig parse, and sources compile through `west build` — document any SDK blocker in the PR.
