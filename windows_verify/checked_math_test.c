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

static void test_mqueue_accounting(void)
{
	size_t bytes_per_message;
	size_t queue_bytes;
	const size_t max = (size_t)-1;

	require_true(!l30_size_add_overflow(8192, sizeof(void *),
					    &bytes_per_message),
		     "normal message overhead must fit");
	require_true(!l30_size_mul_overflow(128, bytes_per_message,
					    &queue_bytes),
		     "normal queue allocation must fit");
	require_true(queue_bytes >= 128u * 8192u,
		     "queue accounting must include payload bytes");
	require_true(l30_size_add_overflow(max, sizeof(void *),
					   &bytes_per_message),
		     "message overhead addition must reject overflow");
}

static void test_semaphore_accounting(void)
{
	size_t sem_bytes;
	size_t allocation_size;
	const size_t max = (size_t)-1;

	require_true(!l30_size_mul_overflow(32000, 32, &sem_bytes),
		     "normal semaphore array must fit");
	require_true(!l30_size_add_overflow(128, sem_bytes,
					    &allocation_size),
		     "normal semaphore header addition must fit");
	require_true(allocation_size == 1024128,
		     "semaphore accounting must be exact");
	require_true(l30_size_mul_overflow(max, 32, &sem_bytes),
		     "oversized semaphore array must be rejected");
}

int main(void)
{
	test_add();
	test_multiply();
	test_mqueue_accounting();
	test_semaphore_accounting();
	puts("PASS: checked arithmetic and allocation accounting");
	return EXIT_SUCCESS;
}
