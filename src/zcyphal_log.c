/**
 * @file zcyphal_log.c
 * @brief Zephyr logging integration for @c cy diagnostics and optional trace.
 * @internal
 */

#include <zcyphal/zcyphal.h>

#include <stdarg.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zcyphal, CONFIG_ZCYPHAL_LOG_LEVEL);

/** @brief cy_diag @c async_error; logs warning with error code and source line. */
static void zcyphal_diag_async_error(cy_diag_t *diag, cy_topic_t *topic, cy_err_t err,
				     uint16_t line_number)
{
	ARG_UNUSED(diag);

	LOG_WRN("cy async error %u at line %u (topic %p)", err, line_number, topic);
}

/** @brief cy_diag @c topic_created; debug log of new topic pointer. */
static void zcyphal_diag_topic_created(cy_diag_t *diag, cy_topic_t *topic)
{
	ARG_UNUSED(diag);

	LOG_DBG("topic created %p", topic);
}

/** @brief cy_diag @c topic_destroyed; debug log of destroyed topic pointer. */
static void zcyphal_diag_topic_destroyed(cy_diag_t *diag, cy_topic_t *topic)
{
	ARG_UNUSED(diag);

	LOG_DBG("topic destroyed %p", topic);
}

/** @brief cy_diag @c topic_reallocated; debug log of subject ID and eviction count. */
static void zcyphal_diag_topic_reallocated(cy_diag_t *diag, cy_topic_t *topic, uint32_t subject_id,
					   uint32_t evictions)
{
	ARG_UNUSED(diag);

	LOG_DBG("topic reallocated %p subject_id=%u evictions=%u", topic, subject_id, evictions);
}

/** @brief cy_diag @c gossip_processed; intentionally silent (no log spam). */
static void zcyphal_diag_gossip_processed(cy_diag_t *diag, cy_topic_t *topic, cy_str_t name,
					  uint64_t hash)
{
	ARG_UNUSED(diag);
	ARG_UNUSED(topic);
	ARG_UNUSED(name);
	ARG_UNUSED(hash);
}

/** @brief Diagnostic vtable registered on each @c zcyphal_t during init. */
const cy_diag_vtable_t zcyphal_diag_vtable = {
	.async_error = zcyphal_diag_async_error,
	.topic_created = zcyphal_diag_topic_created,
	.topic_destroyed = zcyphal_diag_topic_destroyed,
	.topic_reallocated = zcyphal_diag_topic_reallocated,
	.gossip_processed = zcyphal_diag_gossip_processed,
};

#if defined(CY_CONFIG_TRACE) && CY_CONFIG_TRACE
/** @brief cy trace hook; forwards formatted messages to @c LOG_DBG when tracing is enabled. */
void cy_trace(cy_t *const cy, const char *const file, const uint_fast16_t line,
	      const char *const func, const char *const format, ...)
{
	char msg[128];
	va_list args;

	ARG_UNUSED(cy);

	va_start(args, format);
	(void)vsnprintk(msg, sizeof(msg), format, args);
	va_end(args);

	LOG_DBG("%s:%u %s: %s", file, line, func, msg);
}
#endif
