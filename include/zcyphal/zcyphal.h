#pragma once

#include <cy.h>
#include <zephyr/kernel.h>
#include <zcyphal/heap.h>

struct zcyphal_config {
	const struct device *can_dev;
	const char *home;
	const char *namespace_;
	const char *remap;
	const char *discriminator;
};

typedef struct zcyphal {
	struct zcyphal_heap heap;
	uint8_t heap_mem[CONFIG_ZCYPHAL_HEAP_SIZE];
	cy_platform_t *platform;
	cy_t *cy;
	struct k_mutex lock;
	struct k_thread spin_thread;
	K_KERNEL_STACK_MEMBER(spin_stack, CONFIG_ZCYPHAL_THREAD_STACK_SIZE);
	atomic_t running;
	cy_diag_t diag;
	char home_buf[64];
} zcyphal_t;

typedef void (*zcyphal_sub_cb_t)(void *user, cy_arrival_t arrival);

int zcyphal_init_ctx(zcyphal_t *ctx, const struct zcyphal_config *cfg);
void zcyphal_shutdown_ctx(zcyphal_t *ctx);
cy_t *zcyphal_cy_ctx(zcyphal_t *ctx);

cy_publisher_t *zcyphal_advertise_ctx(zcyphal_t *ctx, const char *topic);
int zcyphal_publish_ctx(zcyphal_t *ctx, cy_publisher_t *pub, const void *data, size_t len,
			k_timeout_t timeout);
cy_future_t *zcyphal_subscribe_ctx(zcyphal_t *ctx, const char *topic, size_t extent,
				     zcyphal_sub_cb_t cb, void *user);

int zcyphal_init(const struct zcyphal_config *cfg);
void zcyphal_shutdown(void);
cy_t *zcyphal_cy(void);
cy_publisher_t *zcyphal_advertise(const char *topic);
int zcyphal_publish(cy_publisher_t *pub, const void *data, size_t len, k_timeout_t timeout);
cy_future_t *zcyphal_subscribe(const char *topic, size_t extent, zcyphal_sub_cb_t cb, void *user);
