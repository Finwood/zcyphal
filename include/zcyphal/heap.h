#pragma once

#include <stddef.h>
#include <stdint.h>

struct sys_heap;

struct zcyphal_heap {
	uint8_t *buffer;
	size_t size;
	struct sys_heap heap;
};

int zcyphal_heap_init(struct zcyphal_heap *h, uint8_t *buffer, size_t size);
void *zcyphal_heap_realloc(struct zcyphal_heap *h, void *ptr, size_t size);
