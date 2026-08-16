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

static int mqueue_bytes(size_t max_messages, size_t message_size,
			size_t *table_bytes, size_t *queue_bytes)
{
	size_t payload_bytes;

	if (l30_size_array_bytes(max_messages, sizeof(void *), table_bytes) ||
	    l30_size_array_bytes(max_messages, message_size, &payload_bytes) ||
	    l30_size_add_overflow(*table_bytes, payload_bytes, queue_bytes))
		return 1;
	return 0;
}

static void test_mqueue_model(void)
{
	size_t table_bytes;
	size_t queue_bytes;
	const size_t max = (size_t)-1;

	require_true(!mqueue_bytes(10, 8192, &table_bytes, &queue_bytes),
		     "normal mqueue accounting must fit");
	require_true(table_bytes == 10u * sizeof(void *),
		     "mqueue pointer table must be exact");
	require_true(queue_bytes == table_bytes + 81920u,
		     "mqueue total must equal table plus payload");
	require_true(mqueue_bytes(max, 8192, &table_bytes, &queue_bytes),
		     "unbounded mqueue count must be rejected");
}

static void test_sem_models(void)
{
	size_t bytes;
	const size_t max = (size_t)-1;

	require_true(!l30_size_flex_bytes(256, 32000, 32, &bytes),
		     "normal semaphore object must fit");
	require_true(bytes == 1024256,
		     "semaphore object model must be exact");
	require_true(!l30_size_array_bytes(32000, sizeof(unsigned short), &bytes),
		     "normal GETALL scratch buffer must fit");
	require_true(bytes == 32000u * sizeof(unsigned short),
		     "GETALL scratch buffer model must be exact");
	require_true(l30_size_flex_bytes(256, max, sizeof(short), &bytes),
		     "oversized sem_undo object must be rejected");
}

static void test_partition_and_iovec_models(void)
{
	size_t bytes;
	const size_t max = (size_t)-1;

	require_true(!l30_size_flex_bytes(64, 4096, sizeof(void *), &bytes),
		     "normal partition table must fit");
	require_true(l30_size_array_bytes(max, 16, &bytes),
		     "oversized SCSI iovec table must be rejected");
}

int main(void)
{
	test_mqueue_model();
	test_sem_models();
	test_partition_and_iovec_models();
	puts("PASS: refactored allocation policy models");
	return EXIT_SUCCESS;
}
