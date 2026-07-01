/**
 * @file zcyphal.h
 * @brief Zephyr-facing Cyphal v1.1 over CAN API.
 *
 * This module integrates the upstream @c cy stack and @c libcanard v5 with Zephyr CAN,
 * providing a managed spin thread, per-instance heap, and a thin convenience layer on top
 * of the native @c cy API.
 *
 * Two usage styles are supported:
 *
 * - **Single-instance convenience API** — @ref zcyphal_init(), @ref zcyphal_advertise(),
 *   @ref zcyphal_publish(), @ref zcyphal_subscribe(), and @ref zcyphal_cy().
 * - **Context API** — the same operations with an explicit @ref zcyphal_t for future
 *   multi-instance (gateway) use.
 *
 * All public entry points are thread-safe with respect to the module spin thread: each call
 * acquires the context mutex around the underlying @c cy operation. Subscription callbacks
 * run on the spin thread with that mutex held; keep them short and non-blocking.
 *
 * For full-featured publish/subscribe (reliable delivery, RPC, streaming), use the native
 * @c cy API via @ref zcyphal_cy() or @ref zcyphal_cy_ctx().
 */

#pragma once

#include <cy.h>
#include <zephyr/kernel.h>
#include <zcyphal/heap.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup zcyphal_api zcyphal public API
 * @{
 */

/**
 * @brief Runtime configuration for @ref zcyphal_init_ctx() / @ref zcyphal_init().
 *
 * Any field set to @c NULL (or omitted when @p cfg is @c NULL) is resolved from devicetree
 * and Kconfig defaults.
 */
struct zcyphal_config {
	/** CAN controller device. Default: @c DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)). */
	const struct device *can_dev;
	/**
	 * Base Cyphal node home name before hwinfo suffix is applied.
	 * Default: @c CONFIG_ZCYPHAL_NODE_HOME.
	 */
	const char *home;
	/** Cyphal namespace string. Default: @c CONFIG_ZCYPHAL_NAMESPACE. */
	const char *namespace_;
	/** Whitespace-separated @c from=to remap rules. Default: @c CONFIG_ZCYPHAL_REMAP. */
	const char *remap;
	/**
	 * Optional per-instance suffix appended to the derived home name (gateway forward-compat).
	 * Default: none.
	 */
	const char *discriminator;
};

/**
 * @brief Per-instance Cyphal node state.
 *
 * Holds the heap, platform glue, @c cy instance, spin thread, and synchronization primitives.
 * Applications may embed this structure (static or dynamic) and pass it to the @c *_ctx()
 * API functions. Do not modify fields directly.
 */
typedef struct zcyphal {
	struct zcyphal_heap heap;
	uint8_t heap_mem[CONFIG_ZCYPHAL_HEAP_SIZE];
	cy_platform_t *platform;
	cy_t *cy;
	struct k_mutex lock;
	struct k_thread spin_thread;
	K_KERNEL_STACK_MEMBER(spin_stack, CONFIG_ZCYPHAL_THREAD_STACK_SIZE);
	atomic_t running;
	atomic_t initialized;
	cy_diag_t diag;
	char home_buf[64];
} zcyphal_t;

/**
 * @brief Subscription callback invoked when a message arrives.
 *
 * Called from the module spin thread with the context mutex held. The @p arrival borrow
 * is valid only for the duration of the callback; copy payload data before returning if
 * needed later.
 *
 * @param user  Application context pointer supplied to @ref zcyphal_subscribe().
 * @param arrival  Borrowed view of the received message and timestamp.
 */
typedef void (*zcyphal_sub_cb_t)(void *user, cy_arrival_t arrival);

/**
 * @brief Initialize a Cyphal node context.
 *
 * Builds identity, starts the CAN platform glue, creates the @c cy instance, registers
 * diagnostics, and starts the managed spin thread.
 *
 * @param ctx  Context to initialize. Must not already be initialized (@c -EALREADY).
 * @param cfg  Optional configuration; @c NULL selects devicetree/Kconfig defaults.
 *
 * @retval 0         Success.
 * @retval -EINVAL   Invalid arguments or CAN device not ready.
 * @retval -EALREADY Context already initialized.
 * @retval -ENOSPC   Identity/home buffer too small.
 * @retval -ENOMEM   Platform or @c cy instance creation failed.
 */
int zcyphal_init_ctx(zcyphal_t *ctx, const struct zcyphal_config *cfg);

/**
 * @brief Shut down a Cyphal node context.
 *
 * Stops the spin thread, drains pending @c cy work, destroys the @c cy instance and platform
 * glue, and releases CAN filters. Safe to call on an uninitialized context (no-op).
 *
 * The application must destroy all publishers and subscription futures (@c cy_future_destroy)
 * before shutdown; otherwise @c cy_destroy() may assert.
 *
 * @param ctx  Context to shut down.
 */
void zcyphal_shutdown_ctx(zcyphal_t *ctx);

/**
 * @brief Access the native @c cy instance for a context.
 *
 * Use this escape hatch for advanced APIs (reliable publish, RPC, streaming, gossip) not
 * wrapped by the convenience helpers.
 *
 * @param ctx  Initialized context, or @c NULL.
 * @return @c cy_t pointer, or @c NULL if @p ctx is @c NULL or not initialized.
 */
cy_t *zcyphal_cy_ctx(zcyphal_t *ctx);

/**
 * @brief Create a best-effort publisher on a topic.
 *
 * Wraps @c cy_advertise() under the context mutex.
 *
 * @param ctx    Initialized context.
 * @param topic  Cyphal topic name (e.g. @c "demo/counter").
 * @return Publisher handle, or @c NULL on invalid arguments or allocation failure.
 */
cy_publisher_t *zcyphal_advertise_ctx(zcyphal_t *ctx, const char *topic);

/**
 * @brief Publish a best-effort message.
 *
 * Converts the Zephyr @p timeout to a @c cy microsecond deadline and calls @c cy_publish().
 *
 * @param ctx     Initialized context.
 * @param pub     Publisher from @ref zcyphal_advertise_ctx().
 * @param data    Payload bytes (may be @c NULL when @p len is 0).
 * @param len     Payload length in bytes.
 * @param timeout Zephyr timeout for the publish operation (@c K_NO_WAIT, @c K_MSEC(), etc.).
 *
 * @retval 0     Published (accepted by @c cy).
 * @retval -EINVAL Invalid arguments.
 * @retval -EIO    @c cy_publish() returned an error.
 */
int zcyphal_publish_ctx(zcyphal_t *ctx, cy_publisher_t *pub, const void *data, size_t len,
			k_timeout_t timeout);

/**
 * @brief Subscribe to a topic with a callback.
 *
 * Registers a @c cy subscription and wires @p cb to be invoked on each completed arrival.
 * Returns a @c cy_future_t that the application must eventually destroy with
 * @c cy_future_destroy() before shutdown.
 *
 * @param ctx     Initialized context.
 * @param topic   Cyphal topic name or pattern.
 * @param extent  Maximum expected payload size in bytes (reassembly buffer sizing).
 * @param cb      Arrival callback; may be @c NULL to poll via @c cy_future_* instead.
 * @param user    Opaque pointer passed to @p cb.
 * @return Subscription future, or @c NULL on invalid arguments or allocation failure.
 */
cy_future_t *zcyphal_subscribe_ctx(zcyphal_t *ctx, const char *topic, size_t extent,
				     zcyphal_sub_cb_t cb, void *user);

/** @name Single-instance convenience API
 * Thin wrappers around a module-owned default context.
 * @{
 */

/**
 * @brief Initialize the default Cyphal node.
 * @param cfg  Optional configuration; @c NULL for defaults. See @ref zcyphal_init_ctx().
 * @return 0 on success, or a negative errno from @ref zcyphal_init_ctx().
 */
int zcyphal_init(const struct zcyphal_config *cfg);

/** @brief Shut down the default Cyphal node. See @ref zcyphal_shutdown_ctx(). */
void zcyphal_shutdown(void);

/** @brief Native @c cy instance of the default node. See @ref zcyphal_cy_ctx(). */
cy_t *zcyphal_cy(void);

/**
 * @brief Advertise on the default node.
 * @copydetails zcyphal_advertise_ctx
 */
cy_publisher_t *zcyphal_advertise(const char *topic);

/**
 * @brief Publish on the default node.
 * @copydetails zcyphal_publish_ctx
 */
int zcyphal_publish(cy_publisher_t *pub, const void *data, size_t len, k_timeout_t timeout);

/**
 * @brief Subscribe on the default node.
 * @copydetails zcyphal_subscribe_ctx
 */
cy_future_t *zcyphal_subscribe(const char *topic, size_t extent, zcyphal_sub_cb_t cb,
			       void *user);

/** @} */

/** @} */ /* zcyphal_api */

#ifdef __cplusplus
}
#endif
