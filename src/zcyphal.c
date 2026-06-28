#include <zcyphal/zcyphal.h>

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "cy_can_zephyr.h"
#include <zcyphal/identity.h>

LOG_MODULE_DECLARE(zcyphal);

extern const cy_diag_vtable_t zcyphal_diag_vtable;

static zcyphal_t default_ctx;

static void zcyphal_subscribe_cb(cy_future_t *future)
{
	cy_user_context_t ctx;
	zcyphal_sub_cb_t cb;
	void *user;

	if (!cy_future_done(future)) {
		return;
	}

	if (cy_future_error(future) != CY_OK) {
		return;
	}

	ctx = cy_future_context(future);
	cb = ctx.ptr[0];
	user = ctx.ptr[1];
	if (cb != NULL) {
		cb(user, cy_arrival_borrow(future));
	}
}

static void zcyphal_spin_fn(void *p1, void *p2, void *p3)
{
	zcyphal_t *ctx = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (atomic_get(&ctx->running)) {
		k_mutex_lock(&ctx->lock, K_FOREVER);
		if (ctx->cy != NULL) {
			(void)cy_spin_until(ctx->cy, cy_now(ctx->cy) + CONFIG_ZCYPHAL_SPIN_SLICE_US);
		}
		k_mutex_unlock(&ctx->lock);
	}
}

static const struct device *zcyphal_default_can_dev(void)
{
#if DT_HAS_CHOSEN(zephyr_canbus)
	return DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
#else
	return NULL;
#endif
}

int zcyphal_init_ctx(zcyphal_t *ctx, const struct zcyphal_config *cfg)
{
	const struct zcyphal_config defaults = {
		.can_dev = zcyphal_default_can_dev(),
		.home = CONFIG_ZCYPHAL_NODE_HOME,
		.namespace_ = CONFIG_ZCYPHAL_NAMESPACE,
		.remap = CONFIG_ZCYPHAL_REMAP,
		.discriminator = NULL,
	};
	const struct zcyphal_config *use = cfg != NULL ? cfg : &defaults;
	const struct device *can_dev = use->can_dev != NULL ? use->can_dev : defaults.can_dev;
	const char *home_base = use->home != NULL ? use->home : CONFIG_ZCYPHAL_NODE_HOME;
	const char *namespace_ = use->namespace_ != NULL ? use->namespace_ : defaults.namespace_;
	const char *remap = use->remap != NULL ? use->remap : defaults.remap;
	uint64_t prng_seed;
	int err;

	if (ctx == NULL || can_dev == NULL || !device_is_ready(can_dev)) {
		return -EINVAL;
	}

	memset(ctx, 0, sizeof(*ctx));
	atomic_set(&ctx->running, 0);

	err = zcyphal_heap_init(&ctx->heap, ctx->heap_mem, sizeof(ctx->heap_mem));
	if (err != 0) {
		return err;
	}

	err = zcyphal_identity_build(ctx->home_buf, sizeof(ctx->home_buf), &prng_seed, home_base,
				     use->discriminator);
	if (err != 0) {
		return err;
	}

	ctx->platform = cy_can_zephyr_new(can_dev, &ctx->heap, CONFIG_ZCYPHAL_TX_QUEUE_SIZE,
					  CONFIG_ZCYPHAL_FILTER_COUNT, prng_seed);
	if (ctx->platform == NULL) {
		return -ENOMEM;
	}

	ctx->cy = cy_new(ctx->platform, cy_str(ctx->home_buf), cy_str(namespace_), cy_str(remap));
	if (ctx->cy == NULL) {
		cy_can_zephyr_destroy(ctx->platform);
		ctx->platform = NULL;
		return -ENOMEM;
	}

	ctx->diag.vtable = &zcyphal_diag_vtable;
	cy_diag_add(ctx->cy, &ctx->diag);

	k_mutex_init(&ctx->lock);
	atomic_set(&ctx->running, 1);
	k_thread_create(&ctx->spin_thread, ctx->spin_stack, K_KERNEL_STACK_SIZEOF(ctx->spin_stack),
			zcyphal_spin_fn, ctx, NULL, NULL, CONFIG_ZCYPHAL_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&ctx->spin_thread, "zcyphal_spin");

	return 0;
}

void zcyphal_shutdown_ctx(zcyphal_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	atomic_set(&ctx->running, 0);
	k_thread_join(&ctx->spin_thread, K_FOREVER);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->cy != NULL) {
		cy_diag_remove(ctx->cy, &ctx->diag);
		cy_destroy(ctx->cy);
		ctx->cy = NULL;
	}
	if (ctx->platform != NULL) {
		cy_can_zephyr_destroy(ctx->platform);
		ctx->platform = NULL;
	}
	k_mutex_unlock(&ctx->lock);
}

cy_t *zcyphal_cy_ctx(zcyphal_t *ctx)
{
	return ctx != NULL ? ctx->cy : NULL;
}

cy_publisher_t *zcyphal_advertise_ctx(zcyphal_t *ctx, const char *topic)
{
	cy_publisher_t *pub;

	if (ctx == NULL || topic == NULL) {
		return NULL;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	pub = cy_advertise(ctx->cy, cy_str(topic));
	k_mutex_unlock(&ctx->lock);
	return pub;
}

int zcyphal_publish_ctx(zcyphal_t *ctx, cy_publisher_t *pub, const void *data, size_t len,
			k_timeout_t timeout)
{
	cy_bytes_t message = { .size = len, .data = data, .next = NULL };
	cy_us_t deadline;
	cy_err_t err;

	if (ctx == NULL || pub == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	deadline = cy_now(ctx->cy) + (cy_us_t)k_timeout_to_us(timeout);
	err = cy_publish(pub, deadline, message);
	k_mutex_unlock(&ctx->lock);

	return err == CY_OK ? 0 : -EIO;
}

cy_future_t *zcyphal_subscribe_ctx(zcyphal_t *ctx, const char *topic, size_t extent,
				     zcyphal_sub_cb_t cb, void *user)
{
	cy_future_t *future;
	cy_user_context_t fctx = {
		.ptr = { cb, user },
	};

	if (ctx == NULL || topic == NULL) {
		return NULL;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	future = cy_subscribe(ctx->cy, cy_str(topic), extent);
	if (future != NULL) {
		cy_future_context_set(future, fctx);
		cy_future_callback_set(future, zcyphal_subscribe_cb);
	}
	k_mutex_unlock(&ctx->lock);
	return future;
}

int zcyphal_init(const struct zcyphal_config *cfg)
{
	return zcyphal_init_ctx(&default_ctx, cfg);
}

void zcyphal_shutdown(void)
{
	zcyphal_shutdown_ctx(&default_ctx);
}

cy_t *zcyphal_cy(void)
{
	return zcyphal_cy_ctx(&default_ctx);
}

cy_publisher_t *zcyphal_advertise(const char *topic)
{
	return zcyphal_advertise_ctx(&default_ctx, topic);
}

int zcyphal_publish(cy_publisher_t *pub, const void *data, size_t len, k_timeout_t timeout)
{
	return zcyphal_publish_ctx(&default_ctx, pub, data, len, timeout);
}

cy_future_t *zcyphal_subscribe(const char *topic, size_t extent, zcyphal_sub_cb_t cb, void *user)
{
	return zcyphal_subscribe_ctx(&default_ctx, topic, extent, cb, user);
}

#if defined(CONFIG_ZCYPHAL_AUTO_INIT)
static int zcyphal_auto_init(void)
{
	return zcyphal_init(NULL);
}

SYS_INIT(zcyphal_auto_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
