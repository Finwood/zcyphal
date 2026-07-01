/**
 * @file heap.c
 * @brief @ref zcyphal_heap_init() and @ref zcyphal_heap_realloc() implementations.
 * @internal
 */

#include <zcyphal/heap.h>

#include <errno.h>
#include <zephyr/sys/sys_heap.h>

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
