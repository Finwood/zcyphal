#pragma once

#include <cy_can.h>
#include <zephyr/device.h>
#include <zcyphal/heap.h>

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

cy_platform_t *cy_can_zephyr_new(const struct device *can_dev, struct zcyphal_heap *heap,
				 size_t tx_queue_capacity, size_t filter_count, uint64_t prng_seed);
void cy_can_zephyr_destroy(cy_platform_t *platform);
