# zcyphal — Cyphal v1.1 Zephyr Module Design

- **Date:** 2026-06-28
- **Status:** Approved design (pre-implementation)
- **Repo:** `zcyphal` (Zephyr T2 workspace + module; west manifest repo)

## 1. Purpose & goals

Provide a **polished, standalone, `west`-importable Zephyr module** that integrates
[Cyphal v1.1][cyphal-1.1] over CAN into Zephyr applications, exposing a simple,
Zephyr-friendly API to interact with the network.

The module packages the upstream Cyphal v1.1 C stack ([`cy`][cy] +
[`libcanard`][libcanard] v5) and supplies the missing Zephyr pieces: a CAN media
glue, build/Kconfig/devicetree integration, an allocator, a managed event-loop
thread, and a focused set of idiomatic convenience helpers.

All three upstreams (Cyphal v1.1, `libcanard` v5-alpha, `cy`) are **alpha**;
the module pins them to specific commits and tracks them deliberately.

### Goal level

A polished open-source module others can import via west: good docs and tests,
sane defaults — **not** necessarily upstreamed into `zephyrproject-rtos`, so we are
not bound to its naming/process/`.rst` requirements (though we follow the spirit).

### API thickness: "medium"

Expose `cy`'s native API as-is (`cy_advertise` / `cy_publish` / `cy_subscribe` /
futures) **plus** a small set of Zephyr-idiomatic helpers layered on top
(devicetree/Kconfig-driven init, a managed RX/TX spin thread, `k_timeout_t`-based
publish, callback- and `k_poll`-friendly subscription). `cy` is not hidden; an
escape hatch (`zcyphal_cy()`) gives full access to the native API.

## 2. Upstream stack & layering

The Cyphal v1.1 C stack is layered; the components **we write** are marked `←`:

```
   Zephyr application
        │  zcyphal.h  (medium API: init + thin convenience helpers)        ← we write
        ├──────────────► cy.h  (native API also exposed: advertise/publish/subscribe/futures)
        │
   zcyphal core (managed spin thread + recursive mutex, DT/Kconfig/identity/logging)  ← we write
        │  cy_platform_t
   cy_can.c   (Cyphal/CAN transport adapter on libcanard v5)               ← upstream (west)
        │  cy_can_vtable_t  { tx_classic, tx_fd, rx, filter, now, realloc }
   cy_can_zephyr.c   (media glue: Zephyr CAN driver + sys_heap + k_uptime) ← we write
        │
   Zephyr CAN driver  (<zephyr/drivers/can.h>, DT-bound controller)
```

`cy` is transport-agnostic; `cy_can` is the platform-agnostic Cyphal/CAN adapter
that delegates all I/O to a `cy_can_vtable_t`. SocketCAN provides one
implementation of that vtable upstream (`cy_can_socketcan.c`); **our central task
is the Zephyr implementation of that same vtable.**

## 3. Repository layout

This repo is both the west manifest repo and the module itself.

```
zcyphal/
├── west.yml                  # adds cy + libcanard as pinned west projects (cy w/ submodules)
├── zephyr/
│   └── module.yml            # build.cmake=., build.kconfig=Kconfig, samples:, tests:
├── CMakeLists.txt            # builds cy, cy_can, libcanard, cy_can_zephyr, zcyphal core
├── Kconfig                   # CONFIG_ZCYPHAL and friends
├── include/zcyphal/
│   └── zcyphal.h             # medium API
├── src/
│   ├── cy_can_zephyr.c/.h    # the cy_can_vtable_t media glue
│   ├── zcyphal.c             # init, managed thread, mutex, identity, logging hookup
│   └── heap.c                # sys_heap-backed realloc wrapper
├── samples/
│   └── pub_sub/              # runnable on native_sim + CAN loopback
├── tests/
│   └── integration/          # twister tests on native_sim loopback
└── docs/                     # README + usage + this spec
```

### Build & dependency wiring

- `west.yml` gains two projects pinned to specific commits:
  - `cy` (OpenCyphal-Garage/cy), with `submodules: true` so its header-only deps
    under `lib/` (`cavl2.h`, `wild_key_value.h`, `olga_scheduler.h`, `rapidhash.h`)
    are fetched.
  - `libcanard` (OpenCyphal/libcanard) pinned to the v5-alpha commit.
  - The Zephyr project revision stays at `v4.4.0`.
  - The exact pinned SHAs are recorded in `west.yml` at implementation time
    (resolution method: pick the current tip of the relevant alpha branch and
    record its SHA; documented in the commit message per the alpha-tracking policy).
- `zephyr/module.yml` points `build.cmake`/`build.kconfig` at our top-level
  `CMakeLists.txt`/`Kconfig` and registers `samples:` and `tests:` for twister.
- Our `CMakeLists.txt` compiles upstream `cy/cy.c`, `cy_can/cy_can.c`,
  `libcanard/canard.c`, plus our glue and core, all gated behind `CONFIG_ZCYPHAL`,
  with include dirs so `#include <cy.h>` / `<canard.h>` resolve for applications.

## 4. The `cy_can_zephyr` media glue (core technical component)

Implements `cy_can_vtable_t` against Zephyr's CAN driver. One vtable instance owns
the single DT-bound controller. v0.1 is single-interface, so `iface_index` is
always 0.

### State (`struct cy_can_zephyr`)

- `const struct device *can_dev` — DT-bound CAN controller.
- RX path: a `k_msgq` of received `struct can_frame`, fed by `can_add_rx_filter`
  callbacks; depth from `CONFIG_ZCYPHAL_RX_QUEUE_SIZE`.
- `sys_heap` realloc wrapper (see §6).
- Installed RX filter IDs, so `filter()` can replace the active set.

### Vtable mapping

| `cy_can_vtable_t` fn | Zephyr implementation |
|---|---|
| `tx_classic(can_id, data, len)` | `can_send(can_dev, &frame, K_NO_WAIT, cb, ...)`, `frame.flags = CAN_FRAME_IDE` (29-bit ext ID). Returns **true** if accepted/fatal; **false** on `-EAGAIN`/`-ENOSPC` so cy retries next spin. Async, non-blocking. |
| `tx_fd(...)` | As above with `CAN_FRAME_FDF` (+`CAN_FRAME_BRS` if configured). Set the vtable pointer to `NULL` when FD is not enabled, which keeps `cy_can` in classic mode. |
| `rx(out, deadline, tx_pending_bitmap)` | `k_msgq_get(&rxq, &frame, timeout)` with `timeout = clamp(deadline - now, ≥0)`. Translate `struct can_frame` → `cy_can_rx_t`. Returns true if a frame was dequeued. |
| `filter(count, filters[])` | `can_remove_rx_filter` previous set, then `can_add_rx_filter` for each `canard_filter_t {id,mask}` → `struct can_filter`. If HW slots are exhausted, install what fits and fall back to accept-all for the remainder (documented). |
| `now()` | Monotonic µs from `k_uptime`/`k_cyc`, same timebase as RX timestamps. |
| `realloc()` | `sys_heap_realloc` wrapper, free-on-zero-size. |

### Nuances baked into the design

1. **Async TX vs. the vtable's poll-style retry contract.** The vtable assumes a
   poll-style driver (`tx` false → retry; `rx` wakes when an iface becomes
   writable). Zephyr's `can_send` is already queued/async, so we report
   acceptance/back-pressure and **ignore `tx_pending_iface_bitmap` for wakeup** —
   `rx` only waits for frames or the deadline. Full TX queue → return false → cy
   retries next spin. Correct, marginally less tight than an edge-triggered driver.
   Documented.
2. **RX timestamps.** Enable `CONFIG_CAN_RX_TIMESTAMP` where supported and convert
   into the `now()` timebase; otherwise stamp at dequeue with `now()` (documented
   accuracy caveat).
3. **CAN ID format.** Cyphal/CAN uses 29-bit extended IDs exclusively → always
   `CAN_FRAME_IDE`; never standard IDs.
4. **Bus state / startup.** Glue performs `can_set_mode` (FD if enabled), bitrate
   from DT, and `can_start` during init; bus-off/error states are surfaced via the
   diagnostics/log path (§7), not by crashing.
5. **Construction.** `cy_can_zephyr_new(can_dev, tx_queue_capacity, filter_count,
   prng_seed)` allocates state, wires the vtable, calls upstream `cy_can_new(...)`,
   and returns a `cy_platform_t*`. Mirrors `cy_can_socketcan_new`.

## 5. Zephyr-facing API, init & threading

### Initialization

Runtime init with DT/Kconfig defaults (chosen over a DT-macro-only model for
simplicity with a single managed node):

```c
struct zcyphal_config {
    const struct device *can_dev;   /* default: DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)) */
    const char *home;               /* default: CONFIG_ZCYPHAL_NODE_HOME + hwinfo suffix */
    const char *namespace_;         /* default: "" (CONFIG_ZCYPHAL_NAMESPACE) */
    const char *remap;              /* default: "" (CONFIG_ZCYPHAL_REMAP) */
};

int   zcyphal_init(const struct zcyphal_config *cfg);  /* NULL => all defaults */
cy_t *zcyphal_cy(void);                                /* escape hatch to native cy API */
```

`zcyphal_init` resolves `can_dev` (DT chosen `zephyr,canbus` by default) → derives
identity (§6) → builds the `sys_heap` → `cy_can_zephyr_new(...)` → `cy_new(...)` →
starts the managed spin thread. Optionally auto-runs at boot via `SYS_INIT` when
`CONFIG_ZCYPHAL_AUTO_INIT=y`.

### Threading & lock contract

- A module-owned thread (Kconfig stack/priority) loops
  `cy_spin_until(cy, now + CONFIG_ZCYPHAL_SPIN_SLICE_US)` under a mutex.
- Every public API call takes the **same `k_mutex`** around the underlying cy call.
- cy invokes subscription/future callbacks **from inside `cy_spin`** — i.e. on the
  spin thread with the lock held. `k_mutex` is recursive in Zephyr, so a callback
  can safely call back into `zcyphal_publish()` etc. without deadlock.
- App callbacks run in spin-thread context: documented as "keep short; don't block."

### Medium convenience helpers (all lock internally)

- `zcyphal_advertise(name) → cy_publisher_t*`
- `zcyphal_publish(pub, data, len, k_timeout_t)` — converts the Zephyr timeout to a
  cy µs deadline internally.
- `zcyphal_subscribe(name, extent, cb, user)` — wires the cy future callback to a
  Zephyr-style user callback (no manual future polling).
- `k_poll`-friendly option: a subscription can raise a `k_poll_signal` / push into a
  user `k_msgq` on arrival, for event-driven loops that prefer `k_poll` over callbacks.
- Reliable publish / request / response / streaming remain available via the native
  cy future API through `zcyphal_cy()`; not re-wrapped in v0.1 (documented).

## 6. Identity & allocator

### Identity (hwinfo)

`hwinfo_get_device_id()` provides the hardware UID:
- Hash it to produce the `prng_seed` for `cy_can_zephyr_new`.
- Append a short hex suffix of the UID to `CONFIG_ZCYPHAL_NODE_HOME` so the cy
  `home` is unique across the network.
- Fallback when hwinfo is unavailable/zero-length: use `CONFIG_ZCYPHAL_PRNG_SEED`
  (with a build-time warning) and the bare configured home name.

### Allocator

A `sys_heap` over a Kconfig-sized static buffer (`CONFIG_ZCYPHAL_HEAP_SIZE`),
wrapped to provide `realloc` with free-on-zero-size semantics
(`sys_heap_realloc`). Backs both the cy/cy_can `realloc` and any glue allocations.
No extra third-party dependency. (Deterministic o1heap is a documented future
option but out of scope for v0.1.)

## 7. Logging & diagnostics

- `LOG_MODULE_REGISTER(zcyphal)`.
- Register a `cy_diag_t` listener forwarding `async_error` / topic lifecycle /
  gossip events to Zephyr `LOG`.
- `cy`'s `CY_CONFIG_TRACE` is gated behind `CONFIG_ZCYPHAL_TRACE` (default off);
  when on, `cy_trace(...)` routes to `LOG_DBG`.
- CAN bus-off / error states from the glue also log here.

## 8. Kconfig surface (`CONFIG_ZCYPHAL`…)

- `ZCYPHAL` (main), `ZCYPHAL_CAN_FD`, `ZCYPHAL_AUTO_INIT`
- `ZCYPHAL_HEAP_SIZE`, `ZCYPHAL_TX_QUEUE_SIZE`, `ZCYPHAL_RX_QUEUE_SIZE`,
  `ZCYPHAL_FILTER_COUNT`
- `ZCYPHAL_THREAD_STACK_SIZE`, `ZCYPHAL_THREAD_PRIORITY`, `ZCYPHAL_SPIN_SLICE_US`
- `ZCYPHAL_NODE_HOME`, `ZCYPHAL_NAMESPACE`, `ZCYPHAL_REMAP`, `ZCYPHAL_PRNG_SEED`
- `ZCYPHAL_TRACE`
- `select`s: `CAN`, `POLL`/`EVENTS` as needed, `HWINFO`, `LOG`.

## 9. Data flow

### Publish

```
app → zcyphal_publish(pub, data, len, k_timeout)   [recursive mutex]
  → cy_publish(pub, deadline, cy_bytes{data,len})
  → cy_can subject_writer_send → libcanard frames
  → cy_can_vtable.tx_classic/tx_fd → can_send(can_dev, frame, K_NO_WAIT)
  → CAN controller → bus
```

### Receive

```
bus → CAN controller → can_add_rx_filter callback → push can_frame into rxq (k_msgq)
  ── spin thread ──
cy_spin_until → cy_can_vtable.rx → k_msgq_get(rxq, deadline) → cy_can_rx_t
  → libcanard reassembles transfer → cy_on_message → topic dispatch
  → subscriber future done → user cb runs (spin thread, lock held)
  → cb reads payload via cy_message_read(), optionally cy_respond()
```

The HW filter set is recomputed by `cy_can` whenever the subscription set changes
and pushed through `filter()` → `can_add_rx_filter`, so the controller only accepts
relevant subject-IDs (accept-all fallback if HW slots are exhausted).

## 10. Samples & testing

### Sample — `samples/pub_sub/`

- Builds with `west build -b native_sim`, `CONFIG_CAN_LOOPBACK=y` as the DT chosen
  `zephyr,canbus`.
- Advertises `demo/counter`, publishes an incrementing counter every 500 ms; a
  subscriber callback logs received values (loopback delivers own frames).
  Exercises init, advertise, publish, subscribe-with-callback, and the managed
  thread — zero hardware.
- README shows pointing at real hardware (overlay real `zephyr,canbus`, bitrate, FD)
  and the optional native_sim ↔ host SocketCAN bridge for desktop `cy`/yakut interop
  (documented, not a CI test).

### Tests — `tests/integration/` (twister, native_sim + loopback)

- **t1 init/teardown:** init succeeds, `zcyphal_cy()` non-NULL, spin thread runs,
  clean shutdown.
- **t2 loopback pub/sub:** advertise + subscribe same topic; publish N; assert the
  callback receives correct payloads within a timeout (full TX→RX→reassembly path).
- **t3 CAN FD:** t2 with `CONFIG_ZCYPHAL_CAN_FD=y` and a >8-byte payload forcing an
  FD / multi-frame transfer.
- **t4 filters:** subscribing installs RX filters; a non-matching subject-ID is
  rejected (observe via counters/log).
- **t5 thread-safety smoke:** publish from a second app thread while the spin thread
  runs; assert no corruption/deadlock (exercises the recursive-mutex contract).
- Registered via `tests:` in `zephyr/module.yml`; run with
  `west twister -T tests`.

## 11. Out of scope for v0.1 (documented "later")

- Redundant / multi-interface CAN (cy_can supports up to `CANARD_IFACE_COUNT`).
- Legacy UAVCAN v0 / DroneCAN sidecar (`cy_can_v0_subscribe/publish`).
- DSDL / nunavut serialization codegen integration (v0.1 is raw-byte payloads).
- First-class re-wrapping of reliable publish / RPC / streaming (available via the
  native `zcyphal_cy()` escape hatch in v0.1).
- Deterministic o1heap allocator option.

[cyphal-1.1]: https://forum.opencyphal.org/t/rfc-early-preview-of-cyphal-v1-1/2438
[libcanard]: https://github.com/OpenCyphal/libcanard
[cy]: https://github.com/OpenCyphal-Garage/cy
