/**
 * @file identity.h
 * @brief Derive Cyphal node home and PRNG seed from hardware identity.
 *
 * @internal Part of the module implementation; not part of the application-facing API.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the @c cy home string and PRNG seed for a node.
 *
 * When hwinfo provides a UID, the seed is hashed from it and a four-byte hex suffix is
 * appended to @p home_base. Otherwise @c CONFIG_ZCYPHAL_PRNG_SEED and the bare base name
 * are used.
 *
 * @param home_buf       Output buffer for the final home name.
 * @param home_buf_len   Size of @p home_buf.
 * @param prng_seed_out  Output PRNG seed for @c cy_can_new().
 * @param home_base      Base home name before suffix (e.g. @c CONFIG_ZCYPHAL_NODE_HOME).
 * @param discriminator  Optional extra suffix for multi-instance differentiation.
 *
 * @retval 0        Success.
 * @retval -EINVAL  Invalid arguments.
 * @retval -ENOSPC  @p home_buf too small.
 */
int zcyphal_identity_build(char *home_buf, size_t home_buf_len, uint64_t *prng_seed_out,
			   const char *home_base, const char *discriminator);

#ifdef __cplusplus
}
#endif
