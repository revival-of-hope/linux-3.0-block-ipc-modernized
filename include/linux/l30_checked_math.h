#ifndef _LINUX_L30_CHECKED_MATH_H
#define _LINUX_L30_CHECKED_MATH_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

/*
 * Small overflow helpers for Linux 3.0-era code.  Newer kernels provide
 * check_add_overflow()/check_mul_overflow(), but those helpers are not
 * available in the original 3.0 tree.
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

#endif /* _LINUX_L30_CHECKED_MATH_H */
