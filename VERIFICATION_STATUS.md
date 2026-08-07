# 验证状态记录

日期：2026-08-05

## 已在当前环境实际完成

1. 将补丁应用到上传的原始 `block/`、`ipc/` 子树副本，7 个生产文件逐字节比对一致；结果：`PATCH_APPLY_PASS`。
2. 使用 GCC 14.2.0、CMake 3.31.6，在 `-Wall -Wextra -Werror -pedantic` 下编译并运行可移植测试；结果：1/1 通过。
3. 使用 Clang 17.0.0，在同等高警告设置下编译并运行可移植测试；结果：1/1 通过。
4. 执行 `source_contract_test.py`，确认真实 `block/`、`ipc/` 源码采用补丁要求，旧危险表达式不存在；结果：通过。
5. 核对 Linux v3.0 官方头文件：该版本已经提供 `kcalloc()`、`DIV_ROUND_UP()` 和 `kstrtoul()`，补丁没有调用版本不存在的这些 API。

## 尚未在当前环境实际执行

1. **MSVC/Windows 实机运行**：已提供 `windows_verify/verify-native.cmd` 和 PowerShell 脚本，但当前执行环境不是 Windows。
2. **完整 Linux 3.0 Kbuild 对象编译**：上传附件只有两个子树，且当前沙箱不能从 kernel.org 下载完整源码。已提供 GitHub Actions 工作流；运行后应生成五个真实 `.o` 文件、构建日志及 SHA-256 清单。

因此，目前可以确认：补丁可干净应用；新增可移植核心逻辑在 GCC 与 Clang 下以“警告即错误”方式通过；源码约束通过。完整内核翻译单元是否无编译错误，需要以仓库中的 GitHub Actions 绿色结果为最终证据，不能用当前局部源码测试替代。

## 2026-08-05 GitHub Actions 首次运行故障与修复

首次远程运行停在 `scripts/basic/fixdep` 的宿主链接阶段，错误为：

```text
relocation R_X86_64_32 against `.rodata' can not be used when making a PIE object
```

这不是 `block/` 或 `ipc/` 修改代码的编译错误。原因是旧版 Kbuild 对 `fixdep` 使用单条 `HOSTCC` 命令完成编译和链接；工作流曾向 `HOSTCFLAGS` 传入 `-fno-pie`，但 Linux 3.0 的该规则没有可靠采用单独给出的 `HOSTLDFLAGS=-no-pie`。结果是编译阶段生成非 PIE 重定位，链接阶段仍按 Ubuntu 默认设置生成 PIE，两者冲突。

修复方式：删除工作流中的 `HOSTCFLAGS` 和 `HOSTLDFLAGS` 覆盖，使宿主工具按发行版默认方式生成 PIE；`-fcommon -fno-pie` 只通过 `KCFLAGS` 施加到待验证的内核对象。该修复不改动任何内核生产源码。

## 2026-08-07 CI toolchain correction (v3)

The proof build now intentionally uses Ubuntu 14.04 / GCC 4.8 inside Docker.
Linux 3.0's `include/linux/compiler-gcc.h` selects a version-specific header
named `compiler-gcc${__GNUC__}.h`; the v3.0 tree predates GCC 9 and therefore
has no `compiler-gcc9.h`.  Using GCC 4.8 keeps the proof focused on whether the
modified `block/` and `ipc/` translation units compile in a toolchain generation
the source tree actually supports.  It avoids pretending that porting all of
Linux 3.0 to GCC 9 is part of this module modernization task.
