#include <zcyphal/identity.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#define RAPIDHASH_COMPACT
#include <rapidhash.h>

LOG_MODULE_DECLARE(zcyphal);

#define ZCYPHAL_UID_BUF_SIZE 16

int zcyphal_identity_build(char *home_buf, size_t home_buf_len, uint64_t *prng_seed_out,
			   const char *home_base, const char *discriminator)
{
	uint8_t uid[ZCYPHAL_UID_BUF_SIZE];
	ssize_t uid_len;
	int err;

	if (home_buf == NULL || home_buf_len == 0 || prng_seed_out == NULL || home_base == NULL) {
		return -EINVAL;
	}

	uid_len = hwinfo_get_device_id(uid, sizeof(uid));
	if (uid_len > 0) {
		*prng_seed_out = rapidhash_withSeed(uid, (size_t)uid_len, 0);

		err = snprintk(home_buf, home_buf_len, "%s-%02x%02x%02x%02x", home_base, uid[0], uid[1],
			       uid[2], uid[3]);
	} else {
		LOG_WRN("hwinfo unavailable; using CONFIG_ZCYPHAL_PRNG_SEED and bare home name");
		*prng_seed_out = CONFIG_ZCYPHAL_PRNG_SEED;
		err = snprintk(home_buf, home_buf_len, "%s", home_base);
	}

	if (err < 0 || (size_t)err >= home_buf_len) {
		return -ENOSPC;
	}

	if (discriminator != NULL && discriminator[0] != '\0') {
		char suffix[64];

		err = snprintk(suffix, sizeof(suffix), "%s-%s", home_buf, discriminator);
		if (err < 0 || (size_t)err >= sizeof(suffix)) {
			return -ENOSPC;
		}

		if (strlen(suffix) + 1 > home_buf_len) {
			return -ENOSPC;
		}

		strcpy(home_buf, suffix);
	}

	return 0;
}
