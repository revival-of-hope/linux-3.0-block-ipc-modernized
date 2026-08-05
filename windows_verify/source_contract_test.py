#!/usr/bin/env python3
"""Verify that the submitted kernel sources contain the intended hardening."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    blk_tag = read("block/blk-tag.c")
    require("if (depth <= 0)" in blk_tag and
            "if (new_depth <= 0)" in blk_tag,
            "blk-tag must reject non-positive queue depths")
    require(blk_tag.count("kcalloc(") >= 2,
            "blk-tag must use overflow-aware array allocation")
    require("DIV_ROUND_UP(depth, BITS_PER_LONG)" in blk_tag,
            "blk-tag must use direct bitmap word rounding")
    require("kzalloc(depth * sizeof" not in blk_tag,
            "old multiplication-based tag allocation is still present")

    blk_sysfs = read("block/blk-sysfs.c")
    require("kstrtoul(page, 10, &value)" in blk_sysfs,
            "block sysfs parser must use strict kstrtoul parsing")
    require("simple_strtoul(p, &p, 10)" not in blk_sysfs and
            "strict_strtoul(page, 10, &value)" not in blk_sysfs,
            "legacy block sysfs parser is still present")
    require(blk_sysfs.count("if (ret < 0)") >= 6,
            "block sysfs callers must stop after parse errors")

    mqueue = read("ipc/mqueue.c")
    require("l30_size_add_overflow" in mqueue and
            "l30_size_mul_overflow" in mqueue,
            "mqueue must use checked size arithmetic")
    require("ULONG_MAX/attr->mq_maxmsg" not in mqueue,
            "old partial mqueue overflow check is still present")

    sem = read("ipc/sem.c")
    require("size_t sem_bytes;" in sem and "size_t size;" in sem,
            "semaphore allocation sizes must use size_t")
    require("l30_size_mul_overflow" in sem and
            "l30_size_add_overflow" in sem,
            "semaphore allocation must use checked arithmetic")
    require("nsems > ns->sc_semmns - ns->used_sems" in sem,
            "semaphore quota check must avoid addition overflow")

    util_h = read("ipc/util.h")
    util_c = read("ipc/util.c")
    require("ipc_alloc(size_t size)" in util_h,
            "IPC allocation interface must use size_t")
    require("ipc_rcu_alloc(size_t size)" in util_h,
            "IPC RCU allocation interface must use size_t")
    require("l30_size_add_overflow(HDRLEN_VMALLOC" in util_c,
            "vmalloc header addition must be checked")
    require("l30_size_add_overflow(HDRLEN_KMALLOC" in util_c,
            "kmalloc header addition must be checked")

    print("PASS: source contracts for block/ and ipc/")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
