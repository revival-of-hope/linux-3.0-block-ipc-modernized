#ifndef _LINUX_L30_CHECKED_MATH_H
#define _LINUX_L30_CHECKED_MATH_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

/*
 * Checked size arithmetic for Linux 3.0-era code.
 *
 * Modern kernels provide check_add_overflow(), check_mul_overflow() and
 * struct_size()/array_size().  Linux 3.0 predates those helpers.  Keep the
 * compatibility layer small and header-only so production code and the
 * native verification harness execute exactly the same arithmetic.
 *
 * Every helper returns 1 on overflow and 0 on success.  On success, *result
 * contains the computed byte count.  Callers must not consume *result after
 * an overflow return.
 */
static inline int l30_size_add_overflow(size_t a, size_t b, size_t *result)
{
	if (a > (size_t)-1 - b)
		return 1;

	*result = a + b;
	return 0;
}

static inline int l30_size_mul_overflow(size_t a, size_t b, size_t *result)
{
	if (a && b > (size_t)-1 / a)
		return 1;

	*result = a * b;
	return 0;
}

/* Compute count * element_size. */
static inline int l30_size_array_bytes(size_t count, size_t element_size,
				       size_t *result)
{
	return l30_size_mul_overflow(count, element_size, result);
}

/* Compute header_size + count * element_size without an unchecked step. */
static inline int l30_size_flex_bytes(size_t header_size, size_t count,
				      size_t element_size, size_t *result)
{
	size_t payload_size;

	if (l30_size_array_bytes(count, element_size, &payload_size))
		return 1;

	return l30_size_add_overflow(header_size, payload_size, result);
}

/* Compute count * (payload_size + overhead_size) without signed overflow. */
static inline int l30_size_records_bytes(size_t count, size_t payload_size,
					 size_t overhead_size, size_t *result)
{
	size_t record_size;

	if (l30_size_add_overflow(payload_size, overhead_size, &record_size))
		return 1;

	return l30_size_array_bytes(count, record_size, result);
}

#endif /* _LINUX_L30_CHECKED_MATH_H */
