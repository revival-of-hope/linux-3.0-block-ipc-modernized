#ifndef _BLOCK_LOOK_IOSCHED_POLICY_H
#define _BLOCK_LOOK_IOSCHED_POLICY_H

/*
 * Direction and candidate selection shared by the Linux elevator and the
 * portable verification harness.  Queue traversal stays in look-iosched.c;
 * this helper owns the policy decision once candidates have been found.
 */
enum l30_look_direction {
	L30_LOOK_FORWARD = 0,
	L30_LOOK_BACKWARD = 1,
};

enum l30_look_choice {
	L30_LOOK_CHOICE_NONE = 0,
	L30_LOOK_CHOICE_EXPIRED,
	L30_LOOK_CHOICE_FORWARD,
	L30_LOOK_CHOICE_BACKWARD,
};

static inline enum l30_look_choice
l30_look_choose(enum l30_look_direction direction, int has_expired,
		enum l30_look_direction expired_direction, int has_forward,
		int has_backward, enum l30_look_direction *next_direction)
{
	if (has_expired) {
		*next_direction = expired_direction;
		return L30_LOOK_CHOICE_EXPIRED;
	}

	if (direction == L30_LOOK_FORWARD) {
		if (has_forward) {
			*next_direction = L30_LOOK_FORWARD;
			return L30_LOOK_CHOICE_FORWARD;
		}
		if (has_backward) {
			*next_direction = L30_LOOK_BACKWARD;
			return L30_LOOK_CHOICE_BACKWARD;
		}
	} else {
		if (has_backward) {
			*next_direction = L30_LOOK_BACKWARD;
			return L30_LOOK_CHOICE_BACKWARD;
		}
		if (has_forward) {
			*next_direction = L30_LOOK_FORWARD;
			return L30_LOOK_CHOICE_FORWARD;
		}
	}

	*next_direction = direction;
	return L30_LOOK_CHOICE_NONE;
}

#endif /* _BLOCK_LOOK_IOSCHED_POLICY_H */
