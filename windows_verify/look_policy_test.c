#include <stdio.h>
#include <stdlib.h>

#include <look-iosched-policy.h>

static void require_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static void require_choice(enum l30_look_direction direction,
			   int has_expired,
			   enum l30_look_direction expired_direction,
			   int has_forward, int has_backward,
			   enum l30_look_choice expected_choice,
			   enum l30_look_direction expected_direction,
			   const char *message)
{
	enum l30_look_direction next_direction = direction;
	enum l30_look_choice choice;

	choice = l30_look_choose(direction, has_expired, expired_direction,
				 has_forward, has_backward, &next_direction);
	require_true(choice == expected_choice, message);
	require_true(next_direction == expected_direction,
		     "selected direction does not match policy");
}

static void test_empty_queue(void)
{
	require_choice(L30_LOOK_FORWARD, 0, L30_LOOK_FORWARD, 0, 0,
		       L30_LOOK_CHOICE_NONE, L30_LOOK_FORWARD,
		       "an empty forward queue must not select a request");
	require_choice(L30_LOOK_BACKWARD, 0, L30_LOOK_BACKWARD, 0, 0,
		       L30_LOOK_CHOICE_NONE, L30_LOOK_BACKWARD,
		       "an empty backward queue must not select a request");
}

static void test_directional_selection(void)
{
	require_choice(L30_LOOK_FORWARD, 0, L30_LOOK_FORWARD, 1, 1,
		       L30_LOOK_CHOICE_FORWARD, L30_LOOK_FORWARD,
		       "forward sweep must prefer the nearest forward request");
	require_choice(L30_LOOK_BACKWARD, 0, L30_LOOK_BACKWARD, 1, 1,
		       L30_LOOK_CHOICE_BACKWARD, L30_LOOK_BACKWARD,
		       "backward sweep must prefer the nearest backward request");
	require_choice(L30_LOOK_FORWARD, 0, L30_LOOK_FORWARD, 1, 0,
		       L30_LOOK_CHOICE_FORWARD, L30_LOOK_FORWARD,
		       "a single forward request must be selected");
	require_choice(L30_LOOK_BACKWARD, 0, L30_LOOK_BACKWARD, 0, 1,
		       L30_LOOK_CHOICE_BACKWARD, L30_LOOK_BACKWARD,
		       "a single backward request must be selected");
}

static void test_boundary_reversal(void)
{
	require_choice(L30_LOOK_FORWARD, 0, L30_LOOK_FORWARD, 0, 1,
		       L30_LOOK_CHOICE_BACKWARD, L30_LOOK_BACKWARD,
		       "forward edge must reverse toward pending requests");
	require_choice(L30_LOOK_BACKWARD, 0, L30_LOOK_BACKWARD, 1, 0,
		       L30_LOOK_CHOICE_FORWARD, L30_LOOK_FORWARD,
		       "backward edge must reverse toward pending requests");
}

static void test_expiration_preemption(void)
{
	require_choice(L30_LOOK_FORWARD, 1, L30_LOOK_BACKWARD, 1, 1,
		       L30_LOOK_CHOICE_EXPIRED, L30_LOOK_BACKWARD,
		       "expired backward request must preempt a forward sweep");
	require_choice(L30_LOOK_BACKWARD, 1, L30_LOOK_FORWARD, 1, 1,
		       L30_LOOK_CHOICE_EXPIRED, L30_LOOK_FORWARD,
		       "expired forward request must preempt a backward sweep");
	require_choice(L30_LOOK_FORWARD, 1, L30_LOOK_FORWARD, 0, 0,
		       L30_LOOK_CHOICE_EXPIRED, L30_LOOK_FORWARD,
		       "expired candidate must be selected independently of sweep candidates");
}

int main(void)
{
	test_empty_queue();
	test_directional_selection();
	test_boundary_reversal();
	test_expiration_preemption();
	puts("PASS: deadline-aware LOOK policy");
	return EXIT_SUCCESS;
}
