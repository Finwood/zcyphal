#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/sys_heap.h>

struct zcyphal_heap {
	uint8_t *buffer;
	size_t size;
	struct sys_heap heap;
};

int zcyphal_heap_init(struct zcyphal_heap *h, uint8_t *buffer, size_t size);
void *zcyphal_heap_realloc(struct zcyphal_heap *h, void *ptr, size_t size);
