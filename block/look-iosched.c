/*
 * Deadline-aware LOOK I/O scheduler for the Linux 3.0 elevator API.
 *
 * Normal dispatch follows the current sector direction and reverses only
 * when no request remains ahead.  A FIFO deadline gives the oldest request
 * a soft upper waiting bound and prevents an endless one-way stream from
 * starving requests behind the simulated disk head.
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>

#include "look-iosched-policy.h"

static const unsigned long look_max_wait = HZ;
static const int look_front_merges = 1;

struct look_data {
	struct rb_root sort_list;
	struct list_head fifo_list;
	sector_t head_sector;
	enum l30_look_direction direction;
	unsigned long fifo_expire;
	int front_merges;
};

static void look_move_request(struct look_data *ld, struct request *rq,
			      enum l30_look_direction direction);

static void look_remove_request(struct request_queue *q, struct request *rq)
{
	struct look_data *ld = q->elevator->elevator_data;

	rq_fifo_clear(rq);
	elv_rb_del(&ld->sort_list, rq);
}

static enum l30_look_direction
look_direction_to_request(struct look_data *ld, struct request *rq)
{
	if (blk_rq_pos(rq) < ld->head_sector)
		return L30_LOOK_BACKWARD;
	if (blk_rq_pos(rq) > ld->head_sector)
		return L30_LOOK_FORWARD;

	return ld->direction;
}

static void look_add_rq_rb(struct look_data *ld, struct request *rq)
{
	struct request *alias;

	while (unlikely(alias = elv_rb_add(&ld->sort_list, rq)))
		look_move_request(ld, alias,
				  look_direction_to_request(ld, alias));
}

static void look_add_request(struct request_queue *q, struct request *rq)
{
	struct look_data *ld = q->elevator->elevator_data;

	look_add_rq_rb(ld, rq);
	rq_set_fifo_time(rq, jiffies + ld->fifo_expire);
	list_add_tail(&rq->queuelist, &ld->fifo_list);
}

static int look_merge(struct request_queue *q, struct request **req,
		      struct bio *bio)
{
	struct look_data *ld = q->elevator->elevator_data;
	struct request *rq;
	sector_t sector;

	if (!ld->front_merges)
		return ELEVATOR_NO_MERGE;

	sector = bio->bi_sector + bio_sectors(bio);
	rq = elv_rb_find(&ld->sort_list, sector);
	if (!rq)
		return ELEVATOR_NO_MERGE;

	BUG_ON(sector != blk_rq_pos(rq));
	if (!elv_rq_merge_ok(rq, bio))
		return ELEVATOR_NO_MERGE;

	*req = rq;
	return ELEVATOR_FRONT_MERGE;
}

static void look_merged_request(struct request_queue *q, struct request *rq,
				int type)
{
	struct look_data *ld = q->elevator->elevator_data;

	if (type != ELEVATOR_FRONT_MERGE)
		return;

	elv_rb_del(&ld->sort_list, rq);
	look_add_rq_rb(ld, rq);
}

static void look_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(rq_fifo_time(next), rq_fifo_time(rq))) {
		list_move(&rq->queuelist, &next->queuelist);
		rq_set_fifo_time(rq, rq_fifo_time(next));
	}

	look_remove_request(q, next);
}

static void look_move_request(struct look_data *ld, struct request *rq,
			      enum l30_look_direction direction)
{
	struct request_queue *q = rq->q;

	look_remove_request(q, rq);
	ld->direction = direction;
	ld->head_sector = rq_end_sector(rq);
	elv_dispatch_add_tail(q, rq);
}

static struct request *look_expired_request(struct look_data *ld)
{
	struct request *rq;

	if (list_empty(&ld->fifo_list))
		return NULL;

	rq = rq_entry_fifo(ld->fifo_list.next);
	if (time_after_eq(jiffies, rq_fifo_time(rq)))
		return rq;

	return NULL;
}

static void look_find_directional_requests(struct look_data *ld,
					   struct request **forward,
					   struct request **backward)
{
	struct rb_node *node = ld->sort_list.rb_node;

	*forward = NULL;
	*backward = NULL;

	while (node) {
		struct request *rq = rb_entry_rq(node);
		sector_t sector = blk_rq_pos(rq);

		if (sector < ld->head_sector) {
			*backward = rq;
			node = node->rb_right;
		} else if (sector > ld->head_sector) {
			*forward = rq;
			node = node->rb_left;
		} else {
			*forward = rq;
			*backward = rq;
			break;
		}
	}
}

static int look_dispatch_requests(struct request_queue *q, int force)
{
	struct look_data *ld = q->elevator->elevator_data;
	struct request *expired;
	struct request *forward;
	struct request *backward;
	struct request *rq = NULL;
	enum l30_look_direction expired_direction;
	enum l30_look_direction next_direction;
	enum l30_look_choice choice;

	(void)force;

	if (RB_EMPTY_ROOT(&ld->sort_list))
		return 0;

	expired = look_expired_request(ld);
	look_find_directional_requests(ld, &forward, &backward);
	expired_direction = expired ?
		look_direction_to_request(ld, expired) : ld->direction;
	choice = l30_look_choose(ld->direction, expired != NULL,
				 expired_direction, forward != NULL,
				 backward != NULL, &next_direction);

	switch (choice) {
	case L30_LOOK_CHOICE_EXPIRED:
		rq = expired;
		break;
	case L30_LOOK_CHOICE_FORWARD:
		rq = forward;
		break;
	case L30_LOOK_CHOICE_BACKWARD:
		rq = backward;
		break;
	case L30_LOOK_CHOICE_NONE:
		break;
	}

	BUG_ON(!rq);
	look_move_request(ld, rq, next_direction);
	return 1;
}

static void *look_init_queue(struct request_queue *q)
{
	struct look_data *ld;

	ld = kmalloc_node(sizeof(*ld), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!ld)
		return NULL;

	ld->sort_list = RB_ROOT;
	INIT_LIST_HEAD(&ld->fifo_list);
	ld->head_sector = 0;
	ld->direction = L30_LOOK_FORWARD;
	ld->fifo_expire = look_max_wait;
	ld->front_merges = look_front_merges;
	return ld;
}

static void look_exit_queue(struct elevator_queue *e)
{
	struct look_data *ld = e->elevator_data;

	BUG_ON(!list_empty(&ld->fifo_list));
	BUG_ON(!RB_EMPTY_ROOT(&ld->sort_list));
	kfree(ld);
}

static ssize_t look_max_wait_ms_show(struct elevator_queue *e, char *page)
{
	struct look_data *ld = e->elevator_data;

	return sprintf(page, "%u\n", jiffies_to_msecs(ld->fifo_expire));
}

static ssize_t look_max_wait_ms_store(struct elevator_queue *e,
				      const char *page, size_t count)
{
	struct look_data *ld = e->elevator_data;
	unsigned long value;
	int ret;

	ret = kstrtoul(page, 10, &value);
	if (ret)
		return ret;
	if (value > INT_MAX)
		return -ERANGE;

	ld->fifo_expire = msecs_to_jiffies((unsigned int)value);
	return count;
}

static ssize_t look_front_merges_show(struct elevator_queue *e, char *page)
{
	struct look_data *ld = e->elevator_data;

	return sprintf(page, "%d\n", ld->front_merges);
}

static ssize_t look_front_merges_store(struct elevator_queue *e,
				       const char *page, size_t count)
{
	struct look_data *ld = e->elevator_data;
	unsigned long value;
	int ret;

	ret = kstrtoul(page, 10, &value);
	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;

	ld->front_merges = value;
	return count;
}

#define LOOK_ATTR(name) \
	__ATTR(name, S_IRUGO | S_IWUSR, look_##name##_show, look_##name##_store)

static struct elv_fs_entry look_attrs[] = {
	LOOK_ATTR(max_wait_ms),
	LOOK_ATTR(front_merges),
	__ATTR_NULL
};

static struct elevator_type iosched_look = {
	.ops = {
		.elevator_merge_fn = look_merge,
		.elevator_merged_fn = look_merged_request,
		.elevator_merge_req_fn = look_merged_requests,
		.elevator_dispatch_fn = look_dispatch_requests,
		.elevator_add_req_fn = look_add_request,
		.elevator_former_req_fn = elv_rb_former_request,
		.elevator_latter_req_fn = elv_rb_latter_request,
		.elevator_init_fn = look_init_queue,
		.elevator_exit_fn = look_exit_queue,
	},
	.elevator_attrs = look_attrs,
	.elevator_name = "look",
	.elevator_owner = THIS_MODULE,
};

static int __init look_init(void)
{
	elv_register(&iosched_look);
	return 0;
}

static void __exit look_exit(void)
{
	elv_unregister(&iosched_look);
}

module_init(look_init);
module_exit(look_exit);

MODULE_AUTHOR("linux-3.0-block-ipc-modernized contributors");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deadline-aware LOOK I/O scheduler");
