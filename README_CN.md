# Linux 3.0 `block/` / `ipc/` 大规模可验证重构

本仓库不是完整 Linux 3.0 源码树，只包含 `block/`、`ipc/` 及验证设施。因此它不能在本地直接生成 `vmlinux`。本次重构的目标不是把 Linux 3.0 强行迁移成现代内核，而是在**不改变用户态 ABI、主要算法和数据结构布局**的前提下，对内存尺寸计算、IPC 分配职责、sysfs 输入解析和验证体系做一次成体系的重构。

## 新增 I/O 调度器

仓库新增了面向 Linux 3.0 elevator 接口的防饥饿 LOOK 调度器。它使用红黑树
按扇区选择请求，并以 FIFO 超时机制限制反向请求的等待时间；CFQ 仍保持默认。

完整算法、配置方法、sysfs 参数和验证边界见
[`docs/LOOK_IO_SCHEDULER.md`](docs/LOOK_IO_SCHEDULER.md)。
