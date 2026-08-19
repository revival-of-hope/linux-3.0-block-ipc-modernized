#!/usr/bin/env python3
"""Static contracts for the Linux 3.0 block/IPC modernization refactor.

These checks deliberately verify both positive architecture properties and
negative regressions.  They are not a replacement for Kbuild; they make it
hard for a future edit to silently reintroduce the exact unsafe allocation and
parsing patterns this project removes.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing file: {relative}")
    return path.read_text(encoding="utf-8")


def forbid(text: str, needle: str, message: str) -> None:
    require(needle not in text, message)


def require_regex(text: str, pattern: str, message: str) -> None:
    require(re.search(pattern, text, flags=re.MULTILINE | re.DOTALL) is not None,
            message)


def check_checked_math() -> None:
    checked = read("include/linux/l30_checked_math.h")
    for helper in (
        "l30_size_add_overflow",
        "l30_size_mul_overflow",
        "l30_size_array_bytes",
        "l30_size_flex_bytes",
        "l30_size_records_bytes",
    ):
        require(f"{helper}(" in checked, f"missing checked-size helper: {helper}")


def check_ipc_allocator_split() -> None:
    makefile = read("ipc/Makefile")
    util = read("ipc/util.c")
    alloc = read("ipc/alloc.c")
    alloc_h = read("ipc/alloc.h")
    util_h = read("ipc/util.h")

    require("util.o alloc.o msgutil.o" in makefile,
            "ipc/alloc.c is not wired into the SYSVIPC Kbuild object list")
    forbid(util, "void *ipc_alloc(size_t size)",
           "ipc_alloc implementation must not remain in util.c")
    forbid(util, "void *ipc_rcu_alloc(size_t size)",
           "ipc_rcu_alloc implementation must not remain in util.c")
    require("void *ipc_alloc(size_t size)" in alloc and
            "void *ipc_rcu_alloc(size_t size)" in alloc,
            "allocator implementations must live in ipc/alloc.c")
    require('#include "alloc.h"' in alloc,
            "ipc/alloc.c must include its focused interface header")
    forbid(alloc, '#include "util.h"',
           "ipc/alloc.c must not depend on the full IPC util.h graph")
    require("#include <linux/types.h>" in alloc_h,
            "ipc/alloc.h must define size_t through a direct Linux type dependency")
    for prototype in (
        "void *ipc_alloc(size_t size);",
        "void ipc_free(void *ptr, size_t size);",
        "void *ipc_rcu_alloc(size_t size);",
        "void ipc_rcu_getref(void *ptr);",
        "void ipc_rcu_putref(void *ptr);",
    ):
        require(prototype in alloc_h,
                f"missing allocator interface prototype: {prototype}")
    require('#include "alloc.h"' in util_h,
            "ipc/util.h must re-export the focused allocator interface")
    require("#include <linux/ipc.h>" in util_h,
            "ipc/util.h must directly provide IPCMNI/kern_ipc_perm dependencies")
    require("ipc_alloc(size_t size)" not in util_h,
            "allocator prototypes must not be duplicated in ipc/util.h")
    require("ipc_rcu_init_header" in alloc and "ipc_rcu_header_size" in alloc,
            "RCU allocation policy must be factored into named helpers")
    require("l30_size_add_overflow(header_size, size, &allocation_size)" in alloc,
            "RCU header + object size must use checked addition")


def check_block_sysfs() -> None:
    source = read("block/blk-sysfs.c")
    require("static int queue_var_parse" in source,
            "block sysfs must centralize strict numeric parsing")
    require("kstrtoul(page, 10, value)" in source,
            "block sysfs parser must use kstrtoul")
    forbid(source, "queue_var_store(",
           "old parse-and-store helper must not remain")
    forbid(source, "simple_strtoul", "legacy permissive parser is still present")
    forbid(source, "strict_strtoul", "deprecated strict_strtoul parser is still present")
    require(source.count("queue_var_parse(page,") >= 6,
            "not all writable queue attributes use the common strict parser")


def check_block_allocations() -> None:
    blk_tag = read("block/blk-tag.c")
    require("if (depth <= 0)" in blk_tag and "if (new_depth <= 0)" in blk_tag,
            "blk-tag must reject non-positive queue depths")
    require(blk_tag.count("kcalloc(") >= 2,
            "blk-tag must use count-aware array allocation")
    require("DIV_ROUND_UP(depth, BITS_PER_LONG)" in blk_tag,
            "blk-tag bitmap sizing must use DIV_ROUND_UP")
    forbid(blk_tag, "kzalloc(depth * sizeof",
           "old multiplication-based tag allocation is still present")

    genhd = read("block/genhd.c")
    require("partno < 0 || partno == INT_MAX" in genhd,
            "partition table growth must reject signed partno overflow")
    require("l30_size_flex_bytes(sizeof(*new_ptbl)" in genhd,
            "partition table allocation must use checked flexible sizing")
    forbid(genhd, "sizeof(*new_ptbl) + target * sizeof",
           "unchecked partition-table allocation formula remains")

    scsi = read("block/scsi_ioctl.c")
    require("l30_size_array_bytes((size_t)hdr->iovec_count" in scsi,
            "SCSI iovec allocation must use checked array sizing")
    forbid(scsi, "sizeof(struct sg_iovec) * hdr->iovec_count",
           "unchecked SCSI iovec allocation formula remains")


def check_look_scheduler() -> None:
    scheduler = read("block/look-iosched.c")
    policy = read("block/look-iosched-policy.h")
    kconfig = read("block/Kconfig.iosched")
    makefile = read("block/Makefile")
    cmake = read("windows_verify/CMakeLists.txt")
    policy_test = read("windows_verify/look_policy_test.c")

    require("config IOSCHED_LOOK" in kconfig,
            "LOOK scheduler is missing its Kconfig switch")
    require("config DEFAULT_LOOK" in kconfig and
            'default "look" if DEFAULT_LOOK' in kconfig,
            "LOOK scheduler cannot be selected as a configured default")
    require_regex(kconfig, r'choice.*?default DEFAULT_CFQ',
                  "CFQ must remain the default scheduler choice")
    require("obj-$(CONFIG_IOSCHED_LOOK)\t+= look-iosched.o" in makefile,
            "LOOK scheduler is not wired into the block Makefile")

    require('#include "look-iosched-policy.h"' in scheduler,
            "kernel scheduler must execute the shared policy helper")
    require("struct rb_root sort_list" in scheduler and
            "struct list_head fifo_list" in scheduler,
            "LOOK scheduler must maintain sector and arrival-order indexes")
    require("time_after_eq(jiffies, rq_fifo_time(rq))" in scheduler,
            "LOOK scheduler must detect expired FIFO requests safely")
    require("l30_look_choose(" in scheduler,
            "LOOK dispatch must use the shared policy decision")
    require("look_direction_to_request(ld, expired)" in scheduler,
            "expired requests must update the sweep direction")
    require("rq_set_fifo_time(rq, jiffies + ld->fifo_expire)" in scheduler,
            "queued requests must receive an absolute FIFO deadline")
    for callback in (
        ".elevator_merge_fn = look_merge",
        ".elevator_merged_fn = look_merged_request",
        ".elevator_merge_req_fn = look_merged_requests",
        ".elevator_dispatch_fn = look_dispatch_requests",
        ".elevator_add_req_fn = look_add_request",
        ".elevator_former_req_fn = elv_rb_former_request",
        ".elevator_latter_req_fn = elv_rb_latter_request",
        ".elevator_init_fn = look_init_queue",
        ".elevator_exit_fn = look_exit_queue",
    ):
        require(callback in scheduler, f"missing LOOK elevator callback: {callback}")
    require('.elevator_name = "look"' in scheduler,
            "LOOK elevator must register under the expected name")
    require("LOOK_ATTR(max_wait_ms)" in scheduler and
            "LOOK_ATTR(front_merges)" in scheduler,
            "LOOK sysfs tunables are incomplete")
    require(scheduler.count("kstrtoul(page, 10, &value)") == 2,
            "LOOK tunables must use strict numeric parsing")
    forbid(scheduler, "simple_strtol", "LOOK must not use permissive parsing")

    require("enum l30_look_direction" in policy and
            "enum l30_look_choice" in policy and
            "l30_look_choose(" in policy,
            "shared LOOK policy interface is incomplete")
    require("add_policy_test(look_policy_test look_policy_test.c)" in cmake,
            "portable LOOK policy test is not registered")
    require(policy_test.count("require_choice(") >= 10,
            "LOOK policy test does not cover the full decision matrix")


def check_ipc_allocations() -> None:
    mqueue = read("ipc/mqueue.c")
    require("static int mq_calculate_sizes" in mqueue,
            "mqueue must centralize table/payload byte accounting")
    require(mqueue.count("mq_calculate_sizes(") >= 4,
            "mqueue validation and allocation must share the same size calculator")
    require("l30_size_add_overflow((size_t)u->mq_bytes, mq_bytes" in mqueue,
            "per-user mqueue accounting must use checked addition")
    forbid(mqueue, "u->mq_bytes + mq_bytes < u->mq_bytes",
           "ad-hoc mqueue overflow test must not remain")
    forbid(mqueue, "info->attr.mq_maxmsg * info->attr.mq_msgsize",
           "unchecked mqueue payload multiplication remains")
    forbid(mqueue, "info->attr.mq_maxmsg * (sizeof(struct msg_msg *)",
           "mqueue eviction must not recompute accounting with unchecked arithmetic")

    sem = read("ipc/sem.c")
    require(sem.count("l30_size_flex_bytes(") >= 2,
            "semaphore flexible allocations must use checked sizing")
    require(sem.count("l30_size_array_bytes(") >= 2,
            "semaphore scratch arrays must use checked sizing")
    for unsafe in (
        "sizeof(ushort)*nsems",
        "sizeof(short)*nsems",
        "sizeof(*sops)*nsops",
    ):
        forbid(sem, unsafe, f"unchecked semaphore allocation remains: {unsafe}")

    msgutil = read("ipc/msgutil.c")
    require(msgutil.count("l30_size_add_overflow(") >= 2,
            "message chunk allocations must use checked header+payload sizing")
    forbid(msgutil, "kmalloc(sizeof(*msg) + alen",
           "unchecked first message-chunk allocation remains")
    forbid(msgutil, "kmalloc(sizeof(*seg) + alen",
           "unchecked continuation-chunk allocation remains")


def check_ci_scope() -> None:
    workflow = read(".github/workflows/linux30-object-build.yml")
    build = read("ci/build-linux30-gcc48.sh")
    required_objects = (
        "block/blk-tag.o",
        "block/blk-sysfs.o",
        "block/genhd.o",
        "block/scsi_ioctl.o",
        "block/look-iosched.o",
        "block/built-in.o",
        "ipc/alloc.o",
        "ipc/mqueue.o",
        "ipc/msg.o",
        "ipc/msgutil.o",
        "ipc/sem.o",
        "ipc/shm.o",
        "ipc/util.o",
    )
    for obj in required_objects:
        require(obj in build, f"GCC 4.8 proof build does not compile {obj}")
    require("cp ipc/alloc.c linux-3.0/ipc/alloc.c" in workflow,
            "CI overlay must include new ipc/alloc.c")
    require("cp ipc/alloc.h linux-3.0/ipc/alloc.h" in workflow,
            "CI overlay must include focused ipc/alloc.h")
    require("cp ipc/Makefile linux-3.0/ipc/Makefile" in workflow,
            "CI overlay must include the refactored IPC Makefile")
    require("cp block/look-iosched.c linux-3.0/block/look-iosched.c" in workflow,
            "CI overlay must include the LOOK scheduler")
    require("cp block/look-iosched-policy.h" in workflow,
            "CI overlay must include the shared LOOK policy")
    require("cp block/Kconfig.iosched linux-3.0/block/Kconfig.iosched" in workflow,
            "CI overlay must include the LOOK Kconfig integration")
    require("cp block/Makefile linux-3.0/block/Makefile" in workflow,
            "CI overlay must include the LOOK Makefile integration")
    require("pull_request:" in workflow,
            "CI must validate pull requests")
    require("make -j2 V=1 vmlinux" in build,
            "GCC 4.8 proof must link a complete vmlinux")
    require("CONFIG_IOSCHED_LOOK=y" in build and
            "CONFIG_DEFAULT_CFQ=y" in build,
            "GCC 4.8 proof must assert LOOK integration without changing the default")
    require("gcc:4.8.5" in workflow,
            "CI must use the Docker Official GCC 4.8.5 image")
    forbid(workflow, "ubuntu:14.04",
           "CI must not depend on the obsolete Ubuntu 14.04 runtime image")
    forbid(build, "old-releases.ubuntu.com",
           "proof build must not depend on retired Ubuntu apt archives")
    forbid(build, "apt-get",
           "proof build must not install packages at runtime")
    require('case "$gcc_version" in' in build and "4.8*)" in build,
            "proof build must reject a non-GCC-4.8 toolchain")


def main() -> int:
    checks = (
        check_checked_math,
        check_ipc_allocator_split,
        check_block_sysfs,
        check_block_allocations,
        check_look_scheduler,
        check_ipc_allocations,
        check_ci_scope,
    )
    for check in checks:
        check()
        print(f"PASS: {check.__name__}")
    print(f"PASS: {len(checks)} source-contract groups")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
