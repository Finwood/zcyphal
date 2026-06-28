#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zcyphal/zcyphal.h>

static K_SEM_DEFINE(rx_sem, 0, 1);
static uint8_t rx_payload[64];
static size_t rx_len;
static K_KERNEL_STACK_DEFINE(publish_stack, 2048);
static struct k_thread publish_thread;

static void reset_rx_state(void)
{
	rx_len = 0;

	while (k_sem_take(&rx_sem, K_NO_WAIT) == 0) {
	}
}

static void test_rx_cb(void *user, cy_arrival_t arrival)
{
	ARG_UNUSED(user);

	if (arrival.message.content == NULL) {
		return;
	}

	rx_len = cy_message_read(arrival.message.content, 0, sizeof(rx_payload), rx_payload);
	if (rx_len > 0) {
		k_sem_give(&rx_sem);
	}
}

ZTEST(zcyphal_integration, test_01_init)
{
	zcyphal_t ctx;

	zassert_ok(zcyphal_init_ctx(&ctx, NULL));
	zassert_not_null(zcyphal_cy_ctx(&ctx));
	zcyphal_shutdown_ctx(&ctx);
	zassert_is_null(zcyphal_cy_ctx(&ctx));
}

ZTEST(zcyphal_integration, test_02_filters)
{
	cy_future_t *sub;

	zassert_ok(zcyphal_init(NULL));
	k_sleep(K_MSEC(200));
	sub = zcyphal_subscribe("test/filter/a", 64, test_rx_cb, NULL);
	zassert_not_null(sub);
	cy_future_destroy(sub);
	zcyphal_shutdown();
}

ZTEST(zcyphal_integration, test_03_loopback)
{
	cy_publisher_t *pub;
	cy_future_t *sub;
	uint8_t payload = 42;
	bool got = false;

	reset_rx_state();
	zassert_ok(zcyphal_init(NULL));
	k_sleep(K_MSEC(500));
	pub = zcyphal_advertise("test/loopback");
	zassert_not_null(pub);
	sub = zcyphal_subscribe("test/loopback", 64, test_rx_cb, NULL);
	zassert_not_null(sub);
	k_sleep(K_SECONDS(1));

	for (int i = 0; i < 10; i++) {
		zassert_ok(zcyphal_publish(pub, &payload, 1, K_MSEC(500)));
		if (k_sem_take(&rx_sem, K_MSEC(500)) == 0) {
			got = true;
			break;
		}
	}

	cy_future_destroy(sub);
	cy_unadvertise(pub);

	zassert_true(got);
	zassert_equal(rx_len, 1);
	zassert_equal(rx_payload[0], payload);
}

ZTEST(zcyphal_integration, test_04_canfd)
{
#ifdef CONFIG_ZCYPHAL_CAN_FD
	cy_publisher_t *pub;
	cy_future_t *sub;
	uint8_t payload[32];

	reset_rx_state();
	for (size_t i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)i;
	}

	zassert_ok(zcyphal_init(NULL));
	pub = zcyphal_advertise("test/canfd");
	zassert_not_null(pub);
	sub = zcyphal_subscribe("test/canfd", 64, test_rx_cb, NULL);
	zassert_not_null(sub);

	zassert_ok(zcyphal_publish(pub, payload, sizeof(payload), K_MSEC(500)));
	zassert_ok(k_sem_take(&rx_sem, K_SECONDS(2)));
	zassert_equal(rx_len, sizeof(payload));
	zassert_mem_equal(rx_payload, payload, sizeof(payload));

	cy_future_destroy(sub);
	cy_unadvertise(pub);
	k_sleep(K_MSEC(50));
	zcyphal_shutdown();
#else
	ztest_test_skip();
#endif
}

static void publish_thread_fn(void *a, void *b, void *c)
{
	cy_publisher_t *pub = a;
	uint8_t val = 42;

	ARG_UNUSED(b);
	ARG_UNUSED(c);

	zcyphal_publish(pub, &val, 1, K_MSEC(500));
}

ZTEST(zcyphal_integration, test_05_thread_safety)
{
	cy_publisher_t *pub;
	cy_future_t *sub;
	k_tid_t tid;

	reset_rx_state();
	zassert_ok(zcyphal_init(NULL));
	pub = zcyphal_advertise("test/thread");
	zassert_not_null(pub);
	sub = zcyphal_subscribe("test/thread", 64, test_rx_cb, NULL);
	zassert_not_null(sub);

	tid = k_thread_create(&publish_thread, publish_stack, K_KERNEL_STACK_SIZEOF(publish_stack),
			      publish_thread_fn, pub, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	zassert_not_null(tid);

	zassert_ok(k_sem_take(&rx_sem, K_SECONDS(2)));
	zassert_equal(rx_len, 1);
	zassert_equal(rx_payload[0], 42);

	k_thread_join(&publish_thread, K_SECONDS(2));
	cy_future_destroy(sub);
	cy_unadvertise(pub);
	k_sleep(K_MSEC(50));
	zcyphal_shutdown();
}

static void zcyphal_test_cleanup(void *unused)
{
	ARG_UNUSED(unused);

	zcyphal_shutdown();
}

ZTEST_SUITE(zcyphal_integration, NULL, NULL, NULL, zcyphal_test_cleanup, NULL);
