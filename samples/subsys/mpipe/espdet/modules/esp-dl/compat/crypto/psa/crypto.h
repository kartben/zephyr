/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The subset of the PSA Crypto API the ESP-DL model loader uses, for builds
 * without CONFIG_PSA_CRYPTO. The loader only calls it to decrypt encrypted
 * models and to check model packages. Every call here fails, so only plain
 * models can be used without CONFIG_PSA_CRYPTO.
 */

#ifndef ESP_DL_COMPAT_PSA_CRYPTO_H_
#define ESP_DL_COMPAT_PSA_CRYPTO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
typedef uint32_t psa_key_id_t;
typedef uint16_t psa_key_type_t;
typedef uint32_t psa_key_usage_t;

typedef struct {
	int unused;
} psa_hash_operation_t;

typedef struct {
	int unused;
} psa_cipher_operation_t;

typedef struct {
	int unused;
} psa_key_attributes_t;

#define PSA_SUCCESS               ((psa_status_t)0)
#define PSA_ERROR_NOT_SUPPORTED   ((psa_status_t)-134)
#define PSA_ALG_SHA_256           ((psa_algorithm_t)0x02000009)
#define PSA_ALG_CTR               ((psa_algorithm_t)0x04c01000)
#define PSA_KEY_TYPE_AES          ((psa_key_type_t)0x2400)
#define PSA_KEY_USAGE_ENCRYPT     ((psa_key_usage_t)0x00000100)
#define PSA_KEY_ID_NULL           ((psa_key_id_t)0)
#define PSA_HASH_OPERATION_INIT   {0}
#define PSA_CIPHER_OPERATION_INIT {0}
#define PSA_KEY_ATTRIBUTES_INIT   {0}

static inline psa_status_t psa_crypto_init(void)
{
	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_hash_setup(psa_hash_operation_t *op, psa_algorithm_t alg)
{
	(void)op;
	(void)alg;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_hash_update(psa_hash_operation_t *op, const uint8_t *input,
					   size_t length)
{
	(void)op;
	(void)input;
	(void)length;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_hash_finish(psa_hash_operation_t *op, uint8_t *hash,
					   size_t hash_size, size_t *hash_length)
{
	(void)op;
	(void)hash;
	(void)hash_size;
	*hash_length = 0;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_hash_abort(psa_hash_operation_t *op)
{
	(void)op;

	return PSA_SUCCESS;
}

static inline void psa_set_key_usage_flags(psa_key_attributes_t *attr, psa_key_usage_t usage)
{
	(void)attr;
	(void)usage;
}

static inline void psa_set_key_algorithm(psa_key_attributes_t *attr, psa_algorithm_t alg)
{
	(void)attr;
	(void)alg;
}

static inline void psa_set_key_type(psa_key_attributes_t *attr, psa_key_type_t type)
{
	(void)attr;
	(void)type;
}

static inline void psa_set_key_bits(psa_key_attributes_t *attr, size_t bits)
{
	(void)attr;
	(void)bits;
}

static inline void psa_reset_key_attributes(psa_key_attributes_t *attr)
{
	(void)attr;
}

static inline psa_status_t psa_import_key(const psa_key_attributes_t *attr, const uint8_t *data,
					  size_t data_length, psa_key_id_t *key)
{
	(void)attr;
	(void)data;
	(void)data_length;
	*key = PSA_KEY_ID_NULL;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_destroy_key(psa_key_id_t key)
{
	(void)key;

	return PSA_SUCCESS;
}

static inline psa_status_t psa_cipher_encrypt_setup(psa_cipher_operation_t *op, psa_key_id_t key,
						    psa_algorithm_t alg)
{
	(void)op;
	(void)key;
	(void)alg;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_cipher_set_iv(psa_cipher_operation_t *op, const uint8_t *iv,
					     size_t iv_length)
{
	(void)op;
	(void)iv;
	(void)iv_length;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_cipher_update(psa_cipher_operation_t *op, const uint8_t *input,
					     size_t input_length, uint8_t *output,
					     size_t output_size, size_t *output_length)
{
	(void)op;
	(void)input;
	(void)input_length;
	(void)output;
	(void)output_size;
	*output_length = 0;

	return PSA_ERROR_NOT_SUPPORTED;
}

static inline psa_status_t psa_cipher_finish(psa_cipher_operation_t *op, uint8_t *output,
					     size_t output_size, size_t *output_length)
{
	(void)op;
	(void)output;
	(void)output_size;
	*output_length = 0;

	return PSA_ERROR_NOT_SUPPORTED;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_DL_COMPAT_PSA_CRYPTO_H_ */
