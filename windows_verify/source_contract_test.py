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
    require("ipc_rcu_init_header" in alloc and "ipc_rcu_header_size" in alloc,
            "RCU allocation policy must be factored into named helpers")
    require("l30_size_add_overflow(header_size, size, &allocation_size)" in alloc,
            "RCU header + object size must use checked addition")
    require("ipc_alloc(size_t size)" in util_h and
            "ipc_free(void *ptr, size_t size)" in util_h,
            "IPC allocation interface must use size_t")


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
        "ipc/alloc.o",
        "ipc/mqueue.o",
        "ipc/msgutil.o",
        "ipc/sem.o",
        "ipc/util.o",
    )
    for obj in required_objects:
        require(obj in build, f"GCC 4.8 proof build does not compile {obj}")
    require("cp ipc/alloc.c linux-3.0/ipc/alloc.c" in workflow,
            "CI overlay must include new ipc/alloc.c")
    require("cp ipc/Makefile linux-3.0/ipc/Makefile" in workflow,
            "CI overlay must include the refactored IPC Makefile")


def main() -> int:
    checks = (
        check_checked_math,
        check_ipc_allocator_split,
        check_block_sysfs,
        check_block_allocations,
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
