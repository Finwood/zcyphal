#include <stdint.h>
#include <stdio.h>
#include <cy.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zcyphal/zcyphal.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define PUBLISH_PERIOD_MS 100

static void on_msg(void *user, cy_arrival_t arrival)
{
	ARG_UNUSED(user);
	uint8_t buf[64];
	size_t n = cy_message_read(arrival.message.content, 0, sizeof(buf), buf);

	cy_topic_t *topic = cy_topic_find_by_hash(zcyphal_cy(), arrival.breadcrumb.topic_hash);
	cy_str_t topic_name = cy_topic_name(topic);
	char msg[128];

	(void)snprintk(msg, sizeof(msg),
		       "cy message received: %.*s, %zu bytes, remote_id %u, tag %lu",
		       (int)topic_name.len, topic_name.str, n,
		       (uint32_t)arrival.breadcrumb.remote_id,
		       (unsigned long)arrival.breadcrumb.message_tag);
	LOG_HEXDUMP_INF(buf, n, msg);
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

	uint32_t val = 0;
	int64_t next_ms = k_uptime_get();

	while (1) {
		zcyphal_publish(pub, &val, sizeof(val), K_MSEC(PUBLISH_PERIOD_MS));
		next_ms += PUBLISH_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
		val++;
	}
}
