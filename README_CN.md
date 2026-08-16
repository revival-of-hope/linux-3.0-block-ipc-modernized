# Linux 3.0 `block/` / `ipc/` 大规模可验证重构

本仓库不是完整 Linux 3.0 源码树，只包含 `block/`、`ipc/` 及验证设施。因此它不能在本地直接生成 `vmlinux`。本次重构的目标不是把 Linux 3.0 强行迁移成现代内核，而是在**不改变用户态 ABI、主要算法和数据结构布局**的前提下，对内存尺寸计算、IPC 分配职责、sysfs 输入解析和验证体系做一次成体系的重构。

## 1. 本次重构做了什么

### 1.1 建立统一的 checked-size 兼容层

`include/linux/l30_checked_math.h` 从两个基础函数扩展为统一的尺寸策略层：

- `l30_size_add_overflow()`：检查加法；
- `l30_size_mul_overflow()`：检查乘法；
- `l30_size_array_bytes()`：计算 `count * element_size`；
- `l30_size_flex_bytes()`：计算 `header + count * element_size`；
- `l30_size_records_bytes()`：计算 `count * (payload + overhead)`。

这些函数保持 header-only，并同时被内核生产源码和原生 C 测试使用，避免“测试实现与真实实现不是同一套逻辑”。

### 1.2 把 IPC 内存/RCU 生命周期从 `util.c` 拆成独立模块

原来的 `ipc/util.c` 同时承担 IPC id、权限、proc 接口、普通分配、RCU 分配和异步释放等职责。本次将约一整块内存管理逻辑抽离为：

```text
ipc/alloc.c
```

并通过 `ipc/Makefile` 接入 Kbuild。`ipc/alloc.c` 现在集中负责：

- `ipc_alloc()` / `ipc_free()`；
- `ipc_rcu_alloc()`；
- RCU header 选择与初始化；
- refcount 增减；
- `kmalloc` / `vmalloc` 释放策略；
- RCU 后的 `vfree()` 延迟工作。

这是真正的模块边界调整，不是只改几行表达式。

### 1.3 `mqueue.c`：验证、实际分配和用户计费使用同一套尺寸模型

新增 `mq_calculate_sizes()`，统一计算：

- 消息指针表大小；
- payload + pointer overhead 的总队列大小。

`mq_attr_ok()` 与 `mqueue_get_inode()` 都调用同一个函数，不再出现“验证时一套公式、实际分配时另一套公式”。`u->mq_bytes + mq_bytes` 也改为 checked addition。

### 1.4 `sem.c`：覆盖四类动态尺寸

现在以下路径都使用统一 checked-size API：

- `sem_array` 主对象；
- `GETALL/SETALL` 临时 `ushort` 数组；
- `sem_undo` 柔性对象；
- `semtimedop()` 的 `struct sembuf` 动态数组。

旧的 `sizeof(...) * nsems`、`sizeof(*sops) * nsops`、`header + count * size` 裸表达式已从这些分配路径移除。

### 1.5 `msgutil.c`：消息分段分配显式检查 header + payload

首段 `msg_msg` 和后续 `msg_msgseg` 的动态分配都先计算安全的 byte count，同时显式拒绝负 `len`。

### 1.6 `block/genhd.c`：分区表扩容同时处理 signed overflow 与 allocation overflow

`disk_expand_part_tbl()` 先拒绝 `partno < 0` 与 `partno == INT_MAX`，避免 `partno + 1` 本身发生有符号溢出；随后用 `l30_size_flex_bytes()` 计算分区表柔性数组大小。

### 1.7 `block/scsi_ioctl.c`：iovec 分配改为 checked array sizing

SCSI ioctl 的 `struct sg_iovec` 数组改为 `size_t` byte count + `l30_size_array_bytes()`，不再把乘法结果先塞进 `int`。

### 1.8 `block/blk-sysfs.c`：把“解析”与“store 返回值”分离

原来的辅助函数同时负责解析数值和返回 sysfs `count`，职责混杂。本次改为：

```c
queue_var_parse(page, &value)
```

只负责严格 `kstrtoul()` 解析；各 store handler 自己在成功后返回 `count`。所有可写队列属性继续复用同一解析入口。

### 1.9 保留并强化 `blk-tag.c` 的原有安全化

继续保留：

- 非正 depth 拒绝；
- `kcalloc()`；
- `DIV_ROUND_UP()`；
- resize 返回真实错误码。

## 2. 重构规模

生产代码/构建层涉及的核心文件包括：

```text
block/blk-sysfs.c
block/blk-tag.c
block/genhd.c
block/scsi_ioctl.c
ipc/Makefile
ipc/alloc.c            # 新模块
ipc/mqueue.c
ipc/msgutil.c
ipc/sem.c
ipc/util.c
ipc/util.h
include/linux/l30_checked_math.h
```

验证层同时重做：

```text
windows_verify/checked_math_test.c
windows_verify/allocation_policy_test.c   # 新增
windows_verify/source_contract_test.py
windows_verify/verify-local.sh             # 新增
windows_verify/verify-native.ps1
.github/workflows/linux30-object-build.yml
ci/build-linux30-gcc48.sh
```

## 3. 本地一键验证

### Linux / macOS / Git Bash

```bash
./windows_verify/verify-local.sh
```

它会从零建立临时 CMake build，并依次执行：

1. 严格警告级别编译两个可移植 C 测试；
2. CTest 运行 checked-size 与 allocation-policy 测试；
3. Python 执行 6 组源码/架构契约。

当前重构环境中的实际结果：

```text
100% tests passed, 0 tests failed out of 2
PASS: 6 source-contract groups
PASS: local verification completed
```

同一套 C 测试已分别使用 GCC 14.2 与 Clang 17 在 `-Wall -Wextra -Werror -pedantic` 下实际通过。

### Windows 原生

安装 Visual Studio 2022 Build Tools / CMake / Python 后，可直接运行：

```text
windows_verify\verify-native.cmd
```

或：

```powershell
.\windows_verify\verify-native.ps1
```

PowerShell 脚本会先删除旧 build，再重新配置、编译、测试和执行 source contracts，避免旧二进制掩盖源码问题。

## 4. 源码契约具体会检测什么

`source_contract_test.py` 不只搜索“新函数是否存在”，还主动检查旧危险模式不得出现，例如：

- `ipc_alloc()` / `ipc_rcu_alloc()` 不得继续定义在 `util.c`；
- `ipc/alloc.c` 必须接入 Kbuild；
- `mqueue` 的验证与实际分配必须共用 `mq_calculate_sizes()`；
- `sem.c` 中指定的裸 `count * sizeof(...)` 分配表达式必须消失；
- `genhd.c` 必须处理 `partno + 1` 的 signed overflow；
- SCSI iovec 必须使用 checked array sizing；
- sysfs 不得退回 `simple_strtoul()` / `strict_strtoul()`；
- CI 必须真实编译全部受影响对象。

因此，后续如果有人只删掉安全检查或把旧表达式改回来，本地验证会直接失败。

## 5. 真实 Linux 3.0 Kbuild 验证

`.github/workflows/linux30-object-build.yml` 会：

1. 下载官方 Linux 3.0 完整源码；
2. 覆盖本仓库中所有重构文件与 `ipc/Makefile`；
3. 在 Ubuntu 14.04 / GCC 4.8 容器中运行 Linux 3.0 Kbuild；
4. 编译以下 9 个真实对象：

```text
block/blk-tag.o
block/blk-sysfs.o
block/genhd.o
block/scsi_ioctl.o
ipc/alloc.o
ipc/mqueue.o
ipc/msgutil.o
ipc/sem.o
ipc/util.o
```

5. 再运行源码契约；
6. 上传 `.o`、完整 build log 和 SHA-256 清单。

Linux 3.0 的 `compiler-gcc${__GNUC__}.h` 机制只直接支持当时的 GCC 世代，因此这里故意使用 GCC 4.8，而不是把“把整个 Linux 3.0 迁移到 GCC 14”混入本次子系统重构。

## 6. 验证结论边界

当前环境已经实际证明：

- 新 checked-size 兼容层可以被 GCC 14.2 和 Clang 17 严格编译；
- 两个原生 C 测试 2/2 通过；
- 6 组生产源码/架构契约通过；
- `git diff --check`、Python 语法检查、Shell 语法检查、GitHub Actions YAML 解析均通过。

当前环境**没有 Docker，也没有 GCC 4.8，且附件不是完整 Linux 3.0 树**，所以不能声称本地已经完成真实内核对象 Kbuild。最终的 Linux 3.0 编译证据应以 GitHub Actions 的 `kernel-objects` job 绿色通过为准。

## 7. 为什么没有重写成 blk-mq / 现代 IPC

真正把 Linux 3.0 块层替换为现代 `blk-mq`，或把 IPC 全量同步到当前 Linux，会影响大量驱动、架构代码、头文件、锁语义和 ABI 细节。在只有 `block/` 与 `ipc/` 子树的项目里，这种改造无法建立可信的完整回归证据。

本次选择的是“足够大、但边界可控”的重构：改变模块职责和多条分配路径，同时保留外部行为，能够用原生测试、源码契约和真实 Kbuild 三层证据检测效果。

## 8. 主要文件

```text
README_CN.md                         使用与设计说明
MODERNIZATION_REPORT.md              详细审查与重构报告
VERIFICATION_STATUS.md               当前实际验证状态
LARGE_REFACTOR.patch                 相对本仓库原状态的本次重构补丁
SHA256SUMS.txt                       关键交付文件校验值
```
