#include <stdio.h>
#include <stdlib.h>

#include <linux/l30_checked_math.h>

static void require_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static void test_add(void)
{
	size_t result = 0;
	const size_t max = (size_t)-1;

	require_true(!l30_size_add_overflow(10, 20, &result),
		     "10 + 20 must not overflow");
	require_true(result == 30, "10 + 20 must equal 30");
	require_true(l30_size_add_overflow(max, 1, &result),
		     "SIZE_MAX + 1 must overflow");
	require_true(!l30_size_add_overflow(max - 8, 8, &result),
		     "SIZE_MAX - 8 + 8 must not overflow");
	require_true(result == max, "boundary addition must equal SIZE_MAX");
}

static void test_multiply(void)
{
	size_t result = 0;
	const size_t max = (size_t)-1;

	require_true(!l30_size_mul_overflow(0, max, &result),
		     "zero multiplication must not overflow");
	require_true(result == 0, "zero multiplication must equal zero");
	require_true(!l30_size_mul_overflow(32, 64, &result),
		     "32 * 64 must not overflow");
	require_true(result == 2048, "32 * 64 must equal 2048");
	require_true(l30_size_mul_overflow(max, 2, &result),
		     "SIZE_MAX * 2 must overflow");
	require_true(!l30_size_mul_overflow(max / 4, 4, &result),
		     "largest safe multiple of four must not overflow");
}

static void test_array_bytes(void)
{
	size_t result = 0;
	const size_t max = (size_t)-1;

	require_true(!l30_size_array_bytes(128, sizeof(void *), &result),
		     "normal pointer table must fit");
	require_true(result == 128u * sizeof(void *),
		     "pointer table byte count must be exact");
	require_true(l30_size_array_bytes(max, sizeof(void *), &result),
		     "oversized pointer table must overflow");
}

static void test_flexible_object_bytes(void)
{
	size_t result = 0;
	const size_t max = (size_t)-1;

	require_true(!l30_size_flex_bytes(128, 32000, 32, &result),
		     "normal flexible allocation must fit");
	require_true(result == 1024128,
		     "flexible allocation byte count must be exact");
	require_true(l30_size_flex_bytes(128, max, 32, &result),
		     "flexible payload multiplication must reject overflow");
	require_true(l30_size_flex_bytes(max - 4, 1, 8, &result),
		     "flexible header addition must reject overflow");
}

static void test_record_bytes(void)
{
	size_t result = 0;
	const size_t max = (size_t)-1;

	require_true(!l30_size_records_bytes(128, 8192, sizeof(void *), &result),
		     "normal record accounting must fit");
	require_true(result >= 128u * 8192u,
		     "record accounting must include payload bytes");
	require_true(l30_size_records_bytes(1, max, sizeof(void *), &result),
		     "record payload + overhead must reject overflow");
}

int main(void)
{
	test_add();
	test_multiply();
	test_array_bytes();
	test_flexible_object_bytes();
	test_record_bytes();
	puts("PASS: checked size arithmetic policy");
	return EXIT_SUCCESS;
}
