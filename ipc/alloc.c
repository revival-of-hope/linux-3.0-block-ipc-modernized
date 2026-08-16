/*
 * linux/ipc/alloc.c
 *
 * IPC allocation and RCU lifetime helpers split out of util.c.  Keeping
 * allocation policy in one translation unit makes size validation auditable
 * and prevents unrelated IPC identifier/permission code from accumulating
 * memory-management details.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/l30_checked_math.h>

#include "util.h"

/**
 * ipc_alloc - allocate IPC scratch space
 * @size: requested byte count
 *
 * Large allocations use vmalloc(), smaller ones use kmalloc().  The matching
 * size must be supplied to ipc_free() so the same allocator family is used.
 */
void *ipc_alloc(size_t size)
{
	if (size > PAGE_SIZE)
		return vmalloc(size);

	return kmalloc(size, GFP_KERNEL);
}

/**
 * ipc_free - free a block created by ipc_alloc()
 * @ptr: allocation returned by ipc_alloc()
 * @size: original requested byte count
 */
void ipc_free(void *ptr, size_t size)
{
	if (size > PAGE_SIZE)
		vfree(ptr);
	else
		kfree(ptr);
}

/*
 * RCU allocations prepend one of three headers to the user-visible object:
 * ipc_rcu_hdr while live, ipc_rcu_grace during the grace period and, for
 * vmalloc-backed objects, ipc_rcu_sched while vfree() is deferred to process
 * context.  The lifetimes do not overlap, so the headers share storage.
 */
struct ipc_rcu_hdr {
	int refcount;
	int is_vmalloc;
	void *data[0];
};

struct ipc_rcu_grace {
	struct rcu_head rcu;
	void *data[0];
};

struct ipc_rcu_sched {
	struct work_struct work;
	void *data[0];
};

#define HDRLEN_KMALLOC\
	(sizeof(struct ipc_rcu_grace) > sizeof(struct ipc_rcu_hdr) ? \
	 sizeof(struct ipc_rcu_grace) : sizeof(struct ipc_rcu_hdr))
#define HDRLEN_VMALLOC\
	(sizeof(struct ipc_rcu_sched) > HDRLEN_KMALLOC ? \
	 sizeof(struct ipc_rcu_sched) : HDRLEN_KMALLOC)

static inline int ipc_rcu_use_vmalloc(size_t size)
{
	/* HDRLEN_KMALLOC is far below PAGE_SIZE, but avoid an unchecked sum. */
	return size > PAGE_SIZE - HDRLEN_KMALLOC;
}

static inline size_t ipc_rcu_header_size(int use_vmalloc)
{
	return use_vmalloc ? HDRLEN_VMALLOC : HDRLEN_KMALLOC;
}

static void ipc_rcu_init_header(void *object, int is_vmalloc)
{
	struct ipc_rcu_hdr *header;

	header = container_of(object, struct ipc_rcu_hdr, data);
	header->is_vmalloc = is_vmalloc;
	header->refcount = 1;
}

/**
 * ipc_rcu_alloc - allocate reference-counted IPC storage freed through RCU
 * @size: user-visible object size
 */
void *ipc_rcu_alloc(size_t size)
{
	size_t allocation_size;
	size_t header_size;
	void *base;
	void *object;
	int use_vmalloc;

	use_vmalloc = ipc_rcu_use_vmalloc(size);
	header_size = ipc_rcu_header_size(use_vmalloc);
	if (l30_size_add_overflow(header_size, size, &allocation_size))
		return NULL;

	if (use_vmalloc)
		base = vmalloc(allocation_size);
	else
		base = kmalloc(allocation_size, GFP_KERNEL);
	if (!base)
		return NULL;

	object = base + header_size;
	ipc_rcu_init_header(object, use_vmalloc);
	return object;
}

void ipc_rcu_getref(void *ptr)
{
	container_of(ptr, struct ipc_rcu_hdr, data)->refcount++;
}

static void ipc_do_vfree(struct work_struct *work)
{
	vfree(container_of(work, struct ipc_rcu_sched, work));
}

/* RCU callback context cannot call vfree(), so defer it to a work item. */
static void ipc_schedule_free(struct rcu_head *head)
{
	struct ipc_rcu_grace *grace;
	struct ipc_rcu_sched *sched;

	grace = container_of(head, struct ipc_rcu_grace, rcu);
	sched = container_of(&grace->data[0], struct ipc_rcu_sched, data[0]);

	INIT_WORK(&sched->work, ipc_do_vfree);
	schedule_work(&sched->work);
}

static void ipc_immediate_free(struct rcu_head *head)
{
	struct ipc_rcu_grace *free;

	free = container_of(head, struct ipc_rcu_grace, rcu);
	kfree(free);
}

void ipc_rcu_putref(void *ptr)
{
	struct ipc_rcu_hdr *header;
	struct ipc_rcu_grace *grace;

	header = container_of(ptr, struct ipc_rcu_hdr, data);
	if (--header->refcount > 0)
		return;

	grace = container_of(ptr, struct ipc_rcu_grace, data);
	if (header->is_vmalloc)
		call_rcu(&grace->rcu, ipc_schedule_free);
	else
		call_rcu(&grace->rcu, ipc_immediate_free);
}
