#pragma once

#include <stddef.h>
#include <stdint.h>

int zcyphal_identity_build(char *home_buf, size_t home_buf_len, uint64_t *prng_seed_out,
			   const char *home_base, const char *discriminator);
