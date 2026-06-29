/**
 * @file cy_can_zephyr.h
 * @brief cy_can_vtable_t implementation over Zephyr CAN.
 *
 * @internal Module-private glue between upstream @c cy_can and the Zephyr CAN driver.
 */

#pragma once

#include <cy_can.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zcyphal/heap.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State for one Zephyr CAN-bound @c cy_can platform instance. */
struct cy_can_zephyr {
	const struct device *can_dev;
	struct zcyphal_heap *heap;
	struct k_msgq rxq;
	uint8_t *rxq_buffer;
	size_t rxq_buffer_size;
	int filter_ids[CONFIG_ZCYPHAL_FILTER_COUNT];
	size_t filter_id_count;
	bool fd_capable;
};

/**
 * @brief Create a @c cy_platform_t backed by @p can_dev.
 * @return Platform handle, or @c NULL on failure.
 */
cy_platform_t *cy_can_zephyr_new(const struct device *can_dev, struct zcyphal_heap *heap,
				 size_t tx_queue_capacity, size_t filter_count, uint64_t prng_seed);

/** @brief Destroy platform glue and stop the CAN controller. */
void cy_can_zephyr_destroy(cy_platform_t *platform);

#ifdef __cplusplus
}
#endif
