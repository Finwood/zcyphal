#include "cy_can_zephyr.h"

#include <errno.h>
#include <string.h>

#include <canard.h>
#include <cy_platform.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zcyphal/heap.h>

LOG_MODULE_DECLARE(zcyphal);

static cy_us_t zcy_now(void *user)
{
	ARG_UNUSED(user);

	return (cy_us_t)k_ticks_to_us_floor64(k_uptime_ticks());
}

static void *zcy_realloc(void *user, void *ptr, size_t size)
{
	struct cy_can_zephyr *self = user;

	return zcyphal_heap_realloc(self->heap, ptr, size);
}

static bool zcy_tx_classic(void *user, canard_us_t deadline, uint_least8_t iface_index,
			   uint32_t can_id, const void *data, uint_least8_t len)
{
	struct cy_can_zephyr *self = user;
	struct can_frame frame;
	int err;

	ARG_UNUSED(deadline);

	if (iface_index != 0) {
		return true;
	}

	frame.id = can_id;
	frame.flags = CAN_FRAME_IDE;
	frame.dlc = can_bytes_to_dlc(len);
	memcpy(frame.data, data, MIN(len, 8));

	err = can_send(self->can_dev, &frame, K_NO_WAIT, NULL, NULL);
	return err == 0;
}

static bool zcy_tx_fd(void *user, canard_us_t deadline, uint_least8_t iface_index, uint32_t can_id,
		      const void *data, uint_least8_t len)
{
	struct cy_can_zephyr *self = user;
	struct can_frame frame;
	int err;

	ARG_UNUSED(deadline);

	if (iface_index != 0) {
		return true;
	}

	frame.id = can_id;
	frame.flags = CAN_FRAME_IDE | CAN_FRAME_FDF;
	frame.dlc = can_bytes_to_dlc(len);
	memcpy(frame.data, data, MIN(len, CAN_MAX_DLEN));

	err = can_send(self->can_dev, &frame, K_NO_WAIT, NULL, NULL);
	return err == 0;
}

static void zcy_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
	struct cy_can_zephyr *self = user_data;

	ARG_UNUSED(dev);

	(void)k_msgq_put(&self->rxq, frame, K_NO_WAIT);
}

static bool zcy_rx(void *user, cy_can_rx_t *out_frame, cy_us_t deadline, uint_least8_t tx_pending_iface_bitmap)
{
	struct cy_can_zephyr *self = user;
	struct can_frame frame;
	cy_us_t now = zcy_now(user);
	k_timeout_t timeout;
	int64_t remain_us;

	ARG_UNUSED(tx_pending_iface_bitmap);

	if (deadline > now) {
		remain_us = deadline - now;
	} else {
		remain_us = 0;
	}

	timeout = K_USEC(remain_us);

	if (k_msgq_get(&self->rxq, &frame, timeout) != 0) {
		return false;
	}

	out_frame->iface_index = 0;
	out_frame->can_id = frame.id & CAN_EXT_ID_MASK;
	out_frame->fd = (frame.flags & CAN_FRAME_FDF) != 0;
	out_frame->len = can_dlc_to_bytes(frame.dlc);
	out_frame->timestamp = zcy_now(user);
	memcpy(out_frame->data, frame.data, out_frame->len);
	return true;
}

static void zcy_remove_filters(struct cy_can_zephyr *self)
{
	for (size_t i = 0; i < self->filter_id_count; i++) {
		can_remove_rx_filter(self->can_dev, self->filter_ids[i]);
	}

	self->filter_id_count = 0;
}

static int zcy_add_filter(struct cy_can_zephyr *self, const struct can_filter *filter)
{
	int filter_id;

	if (self->filter_id_count >= CONFIG_ZCYPHAL_FILTER_COUNT) {
		return -ENOSPC;
	}

	filter_id = can_add_rx_filter(self->can_dev, zcy_rx_callback, self, filter);
	if (filter_id < 0) {
		return filter_id;
	}

	self->filter_ids[self->filter_id_count++] = filter_id;
	return 0;
}

static bool zcy_filter(void *user, size_t filter_count, const canard_filter_t *filters)
{
	struct cy_can_zephyr *self = user;
	const struct can_filter accept_all = {
		.id = 0,
		.mask = CAN_EXT_ID_MASK,
		.flags = CAN_FILTER_IDE,
	};
	int err;

	zcy_remove_filters(self);

	if (filter_count == 0 || filters == NULL) {
		err = zcy_add_filter(self, &accept_all);
		return err == 0;
	}

	for (size_t i = 0; i < filter_count; i++) {
		struct can_filter zfilter = {
			.id = filters[i].extended_can_id & CAN_EXT_ID_MASK,
			.mask = filters[i].extended_mask & CAN_EXT_ID_MASK,
			.flags = CAN_FILTER_IDE,
		};

		err = zcy_add_filter(self, &zfilter);
		if (err == -ENOSPC) {
			LOG_WRN("CAN RX filter slots exhausted; falling back to accept-all");
			zcy_remove_filters(self);
			return zcy_add_filter(self, &accept_all) == 0;
		}
		if (err != 0) {
			zcy_remove_filters(self);
			return false;
		}
	}

	return true;
}

static const cy_can_vtable_t zcy_vtable_fd = {
	.tx_classic = zcy_tx_classic,
	.tx_fd = zcy_tx_fd,
	.rx = zcy_rx,
	.filter = zcy_filter,
	.now = zcy_now,
	.realloc = zcy_realloc,
};

static const cy_can_vtable_t zcy_vtable_classic = {
	.tx_classic = zcy_tx_classic,
	.tx_fd = NULL,
	.rx = zcy_rx,
	.filter = zcy_filter,
	.now = zcy_now,
	.realloc = zcy_realloc,
};

cy_platform_t *cy_can_zephyr_new(const struct device *can_dev, struct zcyphal_heap *heap,
				 size_t tx_queue_capacity, size_t filter_count, uint64_t prng_seed)
{
	struct cy_can_zephyr *self;
	can_mode_t cap = 0;
	can_mode_t mode = CAN_MODE_NORMAL;
	const struct can_filter accept_all = {
		.id = 0,
		.mask = CAN_EXT_ID_MASK,
		.flags = CAN_FILTER_IDE,
	};
	cy_platform_t *platform;
	int err;

	if (can_dev == NULL || heap == NULL) {
		return NULL;
	}

	self = zcyphal_heap_realloc(heap, NULL, sizeof(*self));
	if (self == NULL) {
		return NULL;
	}

	memset(self, 0, sizeof(*self));
	self->can_dev = can_dev;
	self->heap = heap;
	self->rxq_buffer_size = CONFIG_ZCYPHAL_RX_QUEUE_SIZE * sizeof(struct can_frame);
	self->rxq_buffer = zcyphal_heap_realloc(heap, NULL, self->rxq_buffer_size);
	if (self->rxq_buffer == NULL) {
		zcyphal_heap_realloc(heap, self, 0);
		return NULL;
	}

	k_msgq_init(&self->rxq, self->rxq_buffer, sizeof(struct can_frame), CONFIG_ZCYPHAL_RX_QUEUE_SIZE);

	if (can_get_capabilities(can_dev, &cap) == 0) {
		self->fd_capable = (cap & CAN_MODE_FD) != 0;
	}

	if (IS_ENABLED(CONFIG_ZCYPHAL_CAN_FD) && self->fd_capable) {
		mode |= CAN_MODE_FD;
	}

	err = can_set_mode(can_dev, mode);
	if (err != 0) {
		goto fail;
	}

	err = can_start(can_dev);
	if (err != 0) {
		goto fail;
	}

	if (zcy_add_filter(self, &accept_all) != 0) {
		goto fail;
	}

	platform = cy_can_new(1, tx_queue_capacity, filter_count, prng_seed,
			      (IS_ENABLED(CONFIG_ZCYPHAL_CAN_FD) && self->fd_capable) ? &zcy_vtable_fd
											: &zcy_vtable_classic,
			      self);
	if (platform == NULL) {
		goto fail;
	}

	return platform;

fail:
	zcy_remove_filters(self);
	zcyphal_heap_realloc(heap, self->rxq_buffer, 0);
	zcyphal_heap_realloc(heap, self, 0);
	return NULL;
}

void cy_can_zephyr_destroy(cy_platform_t *platform)
{
	struct cy_can_zephyr *self;

	if (platform == NULL) {
		return;
	}

	self = cy_can_user(platform);
	zcy_remove_filters(self);
	cy_can_destroy(platform);
	zcyphal_heap_realloc(self->heap, self->rxq_buffer, 0);
	zcyphal_heap_realloc(self->heap, self, 0);
}
