/**
 * @file heap.h
 * @brief Per-instance sys_heap wrapper used by zcyphal and CAN glue.
 *
 * @internal Part of the module implementation; not part of the application-facing API.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/sys_heap.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief sys_heap state for one zcyphal context. */
struct zcyphal_heap {
	uint8_t *buffer;       /**< Backing storage (not owned). */
	size_t size;           /**< Size of @p buffer in bytes. */
	struct sys_heap heap;  /**< Zephyr heap instance. */
};

/**
 * @brief Initialize a heap over caller-provided memory.
 * @retval 0 Success.
 * @retval -EINVAL Invalid arguments.
 */
int zcyphal_heap_init(struct zcyphal_heap *h, uint8_t *buffer, size_t size);

/**
 * @brief realloc() semantics for the heap; @p size == 0 frees @p ptr.
 * @return Allocated pointer, or @c NULL on failure / after free.
 */
void *zcyphal_heap_realloc(struct zcyphal_heap *h, void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
