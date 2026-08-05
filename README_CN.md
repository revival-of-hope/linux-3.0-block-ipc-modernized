# Linux 3.0 `block/` 与 `ipc/` 现代化修复包

## 1. 适用范围与结论边界

上传的 `linux.zip` 只有两个子系统：

- `block/`：通用块设备层、I/O 调度器、请求队列、SCSI ioctl 等；
- `ipc/`：System V IPC、POSIX 消息队列、共享内存、信号量等。

它不是完整 Linux 3.0 源码树，因此原压缩包本身不能生成 `vmlinux` 或内核镜像。本修复包提供两级证据：

1. **Windows 原生验证**：不使用虚拟机、WSL 或 Docker，直接用 MSVC/Clang 编译并运行与补丁共用的溢出检查代码，同时检查生产源码确实采用了这些修复；
2. **完整内核对象验证**：通过 GitHub Actions 的远程 Linux 环境下载官方 Linux 3.0 源码，覆盖本补丁后编译所有发生修改的 `.o` 文件。此步骤不要求本机安装虚拟机。

Windows 原生验证可以证明新增加的可移植代码能够编译、边界测试通过且补丁结构正确；只有第二级验证才能证明修改后的真实内核翻译单元通过 Linux Kbuild 编译。二者不能混为一谈。

## 2. 修改总览

### 2.1 `block/blk-tag.c`：标签表分配安全化与简化

原代码直接计算：

```c
kzalloc(depth * sizeof(struct request *), GFP_ATOMIC);
```

问题：

- `depth <= 0` 时缺少明确拒绝；
- 元素数与元素大小先相乘，再交给分配器，表达式本身可能溢出；
- `ALIGN(depth, BITS_PER_LONG) / BITS_PER_LONG` 可读性较差。

修改：

- 初始化阶段两处检查 `depth <= 0`，并在调整标签深度时拒绝 `new_depth <= 0`；
- 用 `kcalloc(count, size, flags)` 代替 `kzalloc(count * size, flags)`；
- 用 `DIV_ROUND_UP(depth, BITS_PER_LONG)` 计算位图字数；
- `blk_queue_resize_tags()` 返回 `init_tag_map()` 的真实错误码，不再把全部错误伪装成 `-ENOMEM`。

收益：

- 消除数组分配乘法溢出的典型风险；
- 非法参数更早失败；
- 错误诊断更准确；
- 不改变请求队列结构、磁盘 ABI 或标签分配算法。

### 2.2 `block/blk-sysfs.c`：严格解析 sysfs 数值

原代码使用 `simple_strtoul()`，它会接受部分非法输入，例如数字后混入非数字字符，且调用者没有可靠区分解析失败。

修改：

- 改用 Linux 3.0 已存在、且延续至现代内核风格的 `kstrtoul()`；
- 所有调用点在写入队列参数前检查负错误码；
- 解析失败时保持原配置不变。

收益：

- 防止错误的 sysfs 输入被“部分解析后静默接受”；
- 避免解析失败后使用未初始化局部变量；
- 正常合法输入的行为不变。

### 2.3 `ipc/mqueue.c`：修复消息队列容量检查中的未定义行为

原代码先在 `long` 类型中进行加法和乘法，之后才转换为 `unsigned long`。若算术已经溢出，后续强制转换不能补救；有符号溢出属于 C 语言未定义行为。

修改：

- 新增 `include/linux/l30_checked_math.h`；
- 先把已经验证为正数的参数转换为 `size_t`；
- 分别检查“单条消息大小 + 指针开销”和“消息数 × 单条总开销”；
- 删除两段重复、难读且不完整的手工比较。

收益：

- 每一步算术都在发生前检查；
- 逻辑更短、更容易单元测试；
- 不改变 `mq_open()`、`mq_send()` 等用户态接口。

### 2.4 `ipc/sem.c`：信号量集合分配使用 `size_t` 与检查算术

原代码：

```c
int size = sizeof(*sma) + nsems * sizeof(struct sem);
```

修改：

- `size` 与中间结果改为 `size_t`；
- 乘法和加法分别使用检查函数；
- 溢出时返回 `-EOVERFLOW`。

### 2.5 `ipc/util.c`、`ipc/util.h`：统一 IPC 分配接口类型

修改：

- `ipc_alloc()`、`ipc_free()`、`ipc_rcu_alloc()` 的大小参数由 `int` 改为 `size_t`；
- RCU 头部与对象大小相加前执行溢出检查；
- 选择 `kmalloc`/`vmalloc` 时改为无溢出的比较形式；
- 顺带统一指针与条件语句格式。

这些接口只在 `ipc/` 内部使用，不改变用户空间 ABI。

## 3. Windows 原生验证：完全不用虚拟机

### 3.1 安装一次性工具

在 Windows 10/11 安装：

1. **Visual Studio 2022 Build Tools**；
2. 工作负载选择“使用 C++ 的桌面开发”；
3. 勾选 MSVC v143、Windows SDK、CMake tools；
4. 安装 Python 3，并确认安装器中的“Add Python to PATH”已选中。

也可以使用已安装的 Visual Studio 2022。无需 WSL、VirtualBox、VMware 或 Docker Desktop。

### 3.2 一键执行

在资源管理器中进入：

```text
windows_verify
```

双击：

```text
verify-native.cmd
```

也可在 PowerShell 中执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\windows_verify\verify-native.ps1
```

### 3.3 预期结果

控制台末尾应出现：

```text
100% tests passed, 0 tests failed
PASS: source contracts for block/ and ipc/
PASS: native Windows verification completed
```

### 3.4 这一验证具体证明什么

- `l30_size_add_overflow()` 和 `l30_size_mul_overflow()` 能被 Windows C 编译器以高警告等级编译；
- 正常值、零、`SIZE_MAX` 边界、消息队列和信号量分配场景均通过；
- 修改后的内核源文件确实使用 `kcalloc`、`kstrtoul`、`size_t` 和检查算术；
- 原有高风险表达式已经被删除。

它不声称 Windows 编译器能直接编译整个 Linux 内核，因为 Linux Kbuild、体系结构汇编、内核头文件和目标格式都不是 Windows 原生工程。

## 4. 不在本机使用虚拟机的真实 Linux 对象编译

本包提供：

```text
.github/workflows/linux30-object-build.yml
```

### 操作步骤

1. 在 GitHub 新建一个空仓库；
2. 把本修复包全部文件提交并推送；
3. 打开仓库的 **Actions** 页面；
4. 选择 **Linux 3.0 modified object build**；
5. 点击 **Run workflow**；
6. 等待两个任务均显示绿色勾号；
7. 展开 `Build changed kernel objects`，确认以下对象均生成；
8. 在运行页面底部下载 `linux-3.0-modified-objects`，其中包含五个 `.o`、完整构建日志和 SHA-256 清单：

```text
block/blk-tag.o
block/blk-sysfs.o
ipc/mqueue.o
ipc/sem.o
ipc/util.o
```

工作流会自动：

- 从 kernel.org 下载官方 `linux-3.0.tar.xz`；
- 覆盖本包中修改的文件；
- 执行 `make defconfig`、`make prepare scripts`；
- 仅编译发生修改的真实内核目标对象；
- 再次运行 Windows 同源的可移植单元测试和源码约束检查。

这比为了课程展示完整启动一个虚拟机更直接：验证目标是“补丁能否进入 Linux 3.0 的实际构建系统并生成对象文件”，而不是“内核能否在某个虚拟硬件上启动”。

## 5. 将补丁应用到完整 Linux 3.0 源码

获得完整官方源码后，在源码根目录执行：

```bash
patch -p1 < linux-3.0-block-ipc-modernization.patch
```

或者直接复制本包中的：

```text
block/blk-tag.c
block/blk-sysfs.c
ipc/mqueue.c
ipc/sem.c
ipc/util.c
ipc/util.h
include/linux/l30_checked_math.h
```

然后编译修改对象：

```bash
make defconfig
make prepare scripts
make -j2 block/blk-tag.o block/blk-sysfs.o ipc/mqueue.o ipc/sem.o ipc/util.o
```

## 6. 课程展示建议

建议按以下顺序演示，约 6—10 分钟：

1. 展示原始 `depth * sizeof(...)`、`simple_strtoul()` 和消息队列溢出检查；
2. 解释“强制转换发生在运算之后，不能挽救已经发生的有符号溢出”；
3. 展示 `kcalloc`、现代 `kstrtoul()` 严格解析和检查算术后的代码；
4. 在 Windows 双击 `verify-native.cmd`，展示所有测试通过；
5. 展示 GitHub Actions 中五个真实内核 `.o` 文件编译成功；
6. 强调未改变系统调用 ABI、数据结构布局和调度算法，因此回归风险可控。

## 7. 当前交付包的验证状态

当前环境已经实际完成 GCC 与 Clang 高警告编译测试、边界单元测试、源码契约检查和补丁干净应用测试。由于附件不是完整源码，且当前沙箱无法下载 kernel.org 源码，GitHub Actions 中的真实 Kbuild 步骤尚未替你远程执行；运行工作流后的绿色结果与下载得到的五个 `.o` 才是“真实 Linux 3.0 翻译单元无编译错误”的最终证明。详见 `VERIFICATION_STATUS.md`。

## 8. 文件说明

```text
block/                                  修改后的块设备层源码
ipc/                                    修改后的 IPC 源码
include/linux/l30_checked_math.h         Linux 3.0 缺少的检查算术小型兼容层
windows_verify/                          Windows 原生编译、单元测试和源码检查
.github/workflows/linux30-object-build.yml 远程真实 Kbuild 对象编译
linux-3.0-block-ipc-modernization.patch  可应用到完整 Linux 3.0 的补丁
MODERNIZATION_REPORT.md                  详细设计与风险分析
VERIFICATION_STATUS.md                    已执行与尚未执行的验证边界
```
