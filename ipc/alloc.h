/*
 * linux/ipc/alloc.h
 *
 * Internal IPC allocation/lifetime interface.  Keep this header deliberately
 * small so ipc/alloc.c does not need to pull in the full util.h dependency
 * graph merely to verify its own public definitions.
 */

#ifndef _IPC_ALLOC_H
#define _IPC_ALLOC_H

#include <linux/types.h>

/* Rare, potentially huge allocations.  Both functions may sleep. */
void *ipc_alloc(size_t size);
void ipc_free(void *ptr, size_t size);

/*
 * RCU-managed IPC storage. Objects start with reference count 1; the putref
 * that reaches zero schedules destruction after the RCU grace period.
 */
void *ipc_rcu_alloc(size_t size);
void ipc_rcu_getref(void *ptr);
void ipc_rcu_putref(void *ptr);

#endif /* _IPC_ALLOC_H */
