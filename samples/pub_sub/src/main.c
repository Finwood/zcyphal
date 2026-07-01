#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zcyphal/zcyphal.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

static void on_msg(void *user, cy_arrival_t arrival)
{
	ARG_UNUSED(user);
	uint8_t buf[64];
	size_t n = cy_message_read(arrival.message.content, 0, sizeof(buf), buf);

	LOG_INF("rx %zu bytes", n);
}

int main(void)
{
	if (zcyphal_init(NULL) != 0) {
		LOG_ERR("init failed");
		return 1;
	}

	cy_publisher_t *pub = zcyphal_advertise("demo/counter");

	zcyphal_subscribe("demo/counter", 64, on_msg, NULL);
	k_sleep(K_SECONDS(1));

	uint8_t val = 0;

	while (1) {
		zcyphal_publish(pub, &val, 1, K_MSEC(500));
		k_sleep(K_MSEC(500));
		val++;
	}
}
