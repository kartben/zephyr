/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(otp_sample, LOG_LEVEL_INF);

#define OTP_NODE DT_ALIAS(otp_0)

#if !DT_NODE_EXISTS(OTP_NODE)
#error "OTP device not defined. Add 'otp-0 = &<otp_node>;' to your DTS aliases."
#endif

/*
 * A simple provisioning record layout stored at the start of OTP memory.
 *
 * In production the record would typically be written once during
 * manufacturing; the application only needs to call otp_read() at runtime.
 */
struct otp_provisioning_record {
	uint8_t  magic[4];     /* "OTP\0" sentinel */
	uint8_t  serial[8];    /* ASCII device serial number */
	uint8_t  hw_rev;       /* hardware revision */
	uint8_t  reserved[3];  /* pad to 16-byte alignment */
};

#define OTP_RECORD_MAGIC "OTP"

static const struct device *const otp_dev = DEVICE_DT_GET(OTP_NODE);

static void print_record(const struct otp_provisioning_record *rec)
{
	LOG_INF("  Magic:    %.3s", rec->magic);
	LOG_INF("  Serial:   %.8s", rec->serial);
	LOG_INF("  HW rev:   %u", rec->hw_rev);
}

int main(void)
{
	struct otp_provisioning_record record;
	int ret;

	if (!device_is_ready(otp_dev)) {
		LOG_ERR("OTP device \"%s\" is not ready", otp_dev->name);
		return -ENODEV;
	}

	LOG_INF("OTP sample on device \"%s\"", otp_dev->name);

	/* Read whatever is currently in OTP */
	ret = otp_read(otp_dev, 0, &record, sizeof(record));
	if (ret < 0) {
		LOG_ERR("otp_read failed: %d", ret);
		return ret;
	}

	if (memcmp(record.magic, OTP_RECORD_MAGIC, 3) == 0) {
		LOG_INF("Provisioning record found:");
		print_record(&record);
	} else {
		LOG_INF("No provisioning record found. Writing one now...");
		LOG_WRN("WARNING: On real OTP hardware this operation is irreversible!");

		/* Compose a demo provisioning record */
		memcpy(record.magic, OTP_RECORD_MAGIC, 3);
		record.magic[3] = '\0';
		memcpy(record.serial, "SN001234", 8);
		record.hw_rev = 1;
		memset(record.reserved, 0, sizeof(record.reserved));

		ret = otp_program(otp_dev, 0, &record, sizeof(record));
		if (ret < 0) {
			LOG_ERR("otp_program failed: %d", ret);
			return ret;
		}

		/* Verify by reading back */
		struct otp_provisioning_record readback;

		ret = otp_read(otp_dev, 0, &readback, sizeof(readback));
		if (ret < 0) {
			LOG_ERR("Readback failed: %d", ret);
			return ret;
		}

		if (memcmp(&record, &readback, sizeof(record)) != 0) {
			LOG_ERR("Readback mismatch — OTP verify failed");
			return -EIO;
		}

		LOG_INF("Provisioning record written and verified:");
		print_record(&readback);
	}

	LOG_INF("OTP sample complete");
	return 0;
}
