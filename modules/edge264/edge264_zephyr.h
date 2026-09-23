/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Forced include for compiling edge264 against Zephyr. Provides heap
 * wrappers, CLOCK_PROCESS_CPUTIME_ID, and GCC fallbacks for Clang vector
 * builtins used by the generic SIMD backend.
 */

#ifndef ZEPHYR_MODULES_EDGE264_EDGE264_ZEPHYR_H
#define ZEPHYR_MODULES_EDGE264_EDGE264_ZEPHYR_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void *edge264_z_aligned_alloc(size_t align, size_t size);
void edge264_z_free(void *ptr);
int edge264_z_clock_gettime(int clk, struct timespec *tp);

#define aligned_alloc(align, size) edge264_z_aligned_alloc((align), (size))
#define free(ptr) edge264_z_free(ptr)

#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif

#define clock_gettime edge264_z_clock_gettime

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

/*
 * GCC does not implement the Clang vector builtins used by SIMD == CLANG.
 * Statement-expression fallbacks let Cortex-M (no NEON) compile. native_sim
 * also defines __SSE2__, so also enable them when SIMD==4 is forced.
 *
 * Use __auto_type so nested calls (min(max(a, lo), hi)) type-check, and
 * __VA_ARGS__ so compound literals with commas are a single argument.
 */
#if !defined(__clang__) && \
	((defined(SIMD) && (SIMD == 4)) || \
	 (!defined(__SSE2__) && !defined(__ARM_NEON) && !defined(__wasm_simd128__)))

#if !__has_builtin(__builtin_shufflevector)
#define __builtin_shufflevector(a, b, ...) \
	({ \
		__auto_type _sv_a = (a); \
		__auto_type _sv_b = (b); \
		__typeof__(_sv_a) _sv_r; \
		const int _sv_idx[] = {__VA_ARGS__}; \
		const unsigned int _sv_n = (unsigned int)(sizeof(_sv_a) / sizeof(_sv_a[0])); \
		unsigned int _sv_i; \
		for (_sv_i = 0; _sv_i < _sv_n; _sv_i++) { \
			int _sv_k = _sv_idx[_sv_i]; \
			if (_sv_k < 0) { \
				_sv_r[_sv_i] = 0; \
			} else if ((unsigned int)_sv_k < _sv_n) { \
				_sv_r[_sv_i] = _sv_a[_sv_k]; \
			} else { \
				_sv_r[_sv_i] = _sv_b[_sv_k - (int)_sv_n]; \
			} \
		} \
		_sv_r; \
	})
#endif

#if !__has_builtin(__builtin_elementwise_min)
#define __builtin_elementwise_min(a, b) \
	({ \
		__auto_type _mn_a = (a); \
		__auto_type _mn_b = (b); \
		__typeof__(_mn_a) _mn_m = _mn_a < _mn_b; \
		(_mn_a & _mn_m) | (_mn_b & ~_mn_m); \
	})
#endif

#if !__has_builtin(__builtin_elementwise_max)
#define __builtin_elementwise_max(a, b) \
	({ \
		__auto_type _mx_a = (a); \
		__auto_type _mx_b = (b); \
		__typeof__(_mx_a) _mx_m = _mx_a > _mx_b; \
		(_mx_a & _mx_m) | (_mx_b & ~_mx_m); \
	})
#endif

#if !__has_builtin(__builtin_elementwise_abs)
#define __builtin_elementwise_abs(a) \
	({ \
		__auto_type _ab_a = (a); \
		__typeof__(_ab_a) _ab_m = _ab_a < 0; \
		(_ab_a ^ _ab_m) - _ab_m; \
	})
#endif

#if !__has_builtin(__builtin_reduce_add)
#define __builtin_reduce_add(...) \
	({ \
		__auto_type _rd_v = (__VA_ARGS__); \
		__typeof__(_rd_v[0]) _rd_s = 0; \
		unsigned int _rd_i; \
		for (_rd_i = 0; _rd_i < (unsigned int)(sizeof(_rd_v) / sizeof(_rd_v[0])); \
		     _rd_i++) { \
			_rd_s += _rd_v[_rd_i]; \
		} \
		_rd_s; \
	})
#endif

#if !__has_builtin(__builtin_elementwise_add_sat)
#define __builtin_elementwise_add_sat(a, b) \
	({ \
		__auto_type _as_a = (a); \
		__auto_type _as_b = (b); \
		__typeof__(_as_a) _as_r; \
		unsigned int _as_n = (unsigned int)(sizeof(_as_a) / sizeof(_as_a[0])); \
		unsigned int _as_i; \
		for (_as_i = 0; _as_i < _as_n; _as_i++) { \
			int64_t _as_t = (int64_t)_as_a[_as_i] + (int64_t)_as_b[_as_i]; \
			int64_t _as_max; \
			int64_t _as_min; \
			if (((__typeof__(_as_a[0]))-1) > 0) { \
				_as_min = 0; \
				_as_max = (int64_t)((__typeof__(_as_a[0]))-1); \
			} else { \
				unsigned int _as_bits = (unsigned int)(sizeof(_as_a[0]) * 8U); \
				_as_max = (int64_t)((1ULL << (_as_bits - 1U)) - 1U); \
				_as_min = -_as_max - 1; \
			} \
			if (_as_t > _as_max) { \
				_as_t = _as_max; \
			} \
			if (_as_t < _as_min) { \
				_as_t = _as_min; \
			} \
			_as_r[_as_i] = (__typeof__(_as_a[0]))_as_t; \
		} \
		_as_r; \
	})
#endif

#if !__has_builtin(__builtin_elementwise_sub_sat)
#define __builtin_elementwise_sub_sat(a, b) \
	({ \
		__auto_type _ss_a = (a); \
		__auto_type _ss_b = (b); \
		__typeof__(_ss_a) _ss_r; \
		unsigned int _ss_n = (unsigned int)(sizeof(_ss_a) / sizeof(_ss_a[0])); \
		unsigned int _ss_i; \
		for (_ss_i = 0; _ss_i < _ss_n; _ss_i++) { \
			int64_t _ss_t = (int64_t)_ss_a[_ss_i] - (int64_t)_ss_b[_ss_i]; \
			int64_t _ss_max; \
			int64_t _ss_min; \
			if (((__typeof__(_ss_a[0]))-1) > 0) { \
				_ss_min = 0; \
				_ss_max = (int64_t)((__typeof__(_ss_a[0]))-1); \
			} else { \
				unsigned int _ss_bits = (unsigned int)(sizeof(_ss_a[0]) * 8U); \
				_ss_max = (int64_t)((1ULL << (_ss_bits - 1U)) - 1U); \
				_ss_min = -_ss_max - 1; \
			} \
			if (_ss_t > _ss_max) { \
				_ss_t = _ss_max; \
			} \
			if (_ss_t < _ss_min) { \
				_ss_t = _ss_min; \
			} \
			_ss_r[_ss_i] = (__typeof__(_ss_a[0]))_ss_t; \
		} \
		_ss_r; \
	})
#endif

#endif /* GCC generic SIMD fallbacks */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_EDGE264_EDGE264_ZEPHYR_H */
