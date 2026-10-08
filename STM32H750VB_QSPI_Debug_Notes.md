# STM32H750VBT6 外部 QSPI Flash 调试复盘与选型参考手册

---

## 目录
1. [项目架构与运行机制](#1-项目架构与运行机制)
2. [硬件原理图引脚排查与修正](#2-硬件原理图引脚排查与修正)
3. [Keil 外部下载算法 (FLM) 填坑全过程](#3-keil-外部下载算法-flm-填坑全过程)
4. [RAM 引导器 (boot_sram) 内存映射与跳转分析](#4-ram-引导器-boot_sram-内存映射与跳转分析)
5. [华邦 (Winbond) Flash 命名规则与后缀讲究 (重点选型参考)](#5-华邦-winbond-flash-命名规则与后缀讲究-重点选型参考)
6. [后续更换 Flash 操作 Check List](#6-后续更换-flash-操作-check-list)
7. [进阶实战：4 线 Quad SPI 模式调通实录 (XIP 4倍速)](#7-进阶实战4-线-quad-spi-模式调通实录-xip-4倍速)
8. [三大工程运行位置、内存映射与 Keil 设置详解 (极重要)](#8-三大工程运行位置内存映射与-keil-设置详解-极重要)

---

## 1. 项目架构与运行机制

### 1.1 为什么需要此套架构？
STM32H750VBT6 是一款高性价比的 Cortex-M7 单片机（主频高达 480MHz），但其**内部片上 Flash 仅有 128KB**，很难容纳包含 GUI、网络协议栈或复杂逻辑的大型程序。
因此，该芯片的典型用法是：
- 将大体积固件烧录到外部廉价的高速 QSPI NOR Flash（如 8MB 的 W25Q64、16MB 的 W25Q128 等）；
- 配置 STM32H7 的 QUADSPI 控制器进入 **Memory-Mapped Mode (内存映射模式)**，将外部 Flash 映射到 CPU 寻址空间的 `0x90000000`；
- 利用 Cortex-M7 的 **XIP (eXecute In Place，就地执行)** 特性，CPU 直接从 `0x90000000` 逐条取指执行代码。

### 1.2 三个工程的职责与协同关系

```
+---------------------------------------------------------------------------------+
|                                 1. flashalgo_flm                                |
|   Keil FLM 算法工程，编译生成 .FLM 算法文件，供 Keil IDE 的 Flash Download 下载插件调用  |
|   功能：负责通过 SWD/JTAG 将 uart_extflash 的 bin/axf 写入外部 Flash 0x90000000     |
+---------------------------------------------------------------------------------+
                                        |  (一键 F8 烧录至外部 Flash)
                                        v
+---------------------------------------------------------------------------------+
|                                 2. uart_extflash                                |
|   用户的实际应用程序工程（APP），链接地址为 0x90000000                                |
|   包含启动向量表、外设驱动与应用逻辑                                                   |
+---------------------------------------------------------------------------------+
                                        ^
                                        |  (配置内存映射后跳转 PC)
+---------------------------------------------------------------------------------+
|                                 3. boot_sram                                    |
|   驻留/运行在内部 AXI SRAM (0x24000000) 的微型启动器                                    |
|   功能：复位 Flash 芯片，配置 QUADSPI 内存映射模式，重定向 VTOR 向量表并跳转至 0x90000000  |
+---------------------------------------------------------------------------------+
```

---

## 2. 硬件原理图引脚排查与修正

### 2.1 问题排查
原代码工程是按“双片 8 线 Flash (DualFlash 16MB)”编写的，片选 NCS 设为 `PB10`。
通过核对硬件原理图（`schdoc.pdf`）发现：
- 板载仅有 **单颗 8 脚 QSPI Flash 芯片 (U3: W25Q64JVSIQ)**；
- 原理图中的 U5 是挂在普通 SPI2 上的备用器件，并非 QSPI Bank 2；
- 单片 QSPI 实际引脚连接为：
  - `PB2`  -> `QSPI_CLK` (AF9)
  - `PB6`  -> **`QSPI_BK1_NCS` (AF10)** （原工程写成了 PB10，导致片选无法拉低，通信完全无响应！）
  - `PD11` -> `QSPI_BK1_IO0` (AF9)
  - `PD12` -> `QSPI_BK1_IO1` (AF9)
  - `PE2`  -> `QSPI_BK1_IO2` (AF9)
  - `PD13` -> `QSPI_BK1_IO3` (AF9)

### 2.2 修正方案
1. 在 `quadspi.c` 中，将片选 GPIO 配置修改为 **`PB6` (AF10)**；
2. 彻底禁用双片模式：`hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE`；
3. 将容量配置改为单片：`hqspi.Init.FlashSize = 22`（$2^{22+1} = 8\text{MB}$）。

---

## 3. Keil 外部下载算法 (FLM) 填坑全过程

在适配 `flashalgo_flm` 时，先后遇到了以下经典错误：

### 错误 1：`Cannot Load Flash Programming Algorithm!`
- **原因**：Keil 的 Flash Download 配置中，`RAM for Algorithm` 默认分配的 RAM 大小仅有 32KB（`0x8000`）。而编译生成的算法文件本身代码段加堆栈约为 45KB，Keil 在将算法搬移至目标 RAM 时发生越界报错。
- **解决**：在 Keil 的 Target 选项卡 -> Debug -> Settings -> Flash Download 中，将 RAM 起始地址和大小改为 STM32H7 的 AXI SRAM：
  - **Start**: `0x24000000`
  - **Size**: `0x40000` (256KB)

### 错误 2：`Flash Timeout / Erase Failed`
- **原因**：原工程遗留了双片判断逻辑。在自动轮询状态寄存器 `HAL_QSPI_AutoPolling` 中，`StatusBytesSize = 2`、`Mask = 0x0101`，算法不断等待第 2 片 Flash 返回空闲信号，但硬件上根本没有第 2 片 Flash，导致必定超时。
- **解决**：将自动轮询修改为单片单字节配置：
  - `sConf.StatusBytesSize = 1`
  - `sConf.Mask = 0x01` / `0x02`
  - `sConf.Match = 0x00` / `0x02`

### 错误 3：`Contents mismatch at: 90000000H ! Verify Failed!`
- **原因**：擦除（0xD8/0x20）和页面烧录（0x02）使用的是单线 SPI 命令，均成功写入；但当 Keil 调用 `Verify()` 时，算法切入内存映射模式并发送了 4 线快速读取命令 `0xEB`。由于新出厂的单片 `W25Q64JVSIQ` **默认 QE（Quad Enable）位为 0（禁用状态）**，导致芯片拒绝输出 4 线数据，读出全为 `0xFF`，校验失败。
- **解决**：将算法中的 `EnableMemoryMappedMode` 切换为标准的 **1-line Fast Read (`0x0B`，8 Dummy Cycles)**。单线模式无需任何 QE 握手，100% 稳定读取，Keil 立即顺利通过 `Verify OK`！

---

## 4. RAM 引导器 (boot_sram) 内存映射与跳转分析

在成功烧录 `uart_extflash` 后，运行 `boot_sram` 进行跳转时遇到了关键阻碍。

### 阻碍 1：跳转前读出的向量表数据全是 `0x88888888`
- **底层硬件成因剖析**：
  - `0x88` 转换成二进制为 `1000 1000`。
  - 在 QSPI 4 线模式下，每个时钟周期传输半个字节（4 位）：
    - `bit3` (IO3 / PD13) = `1`
    - `bit2` (IO2 / PE2)  = `0`
    - `bit1` (IO1 / PD12) = `0`
    - `bit0` (IO0 / PD11) = `0`
  - 当外部 Flash 芯片的 QE 位为 0 时，芯片内部 IO2(/WP) 和 IO3(/HOLD) 根本没有被配置为数据线，IO3 维持内部弱上拉为高电平，其余线路为低电平。
  - MCU 无论在哪个时钟周期采样，读到的都是 `1000` = `0x8`，两个半字节拼起来每个字节都是固定的 `0x88`！
  - `GoToApp()` 读取 `entry_point = 0x88888888`，无法匹配 `0x90000000`，直接拒绝跳转并退入 `while(1)` 死循环。

### 阻碍 2：QSPI 状态机未复位导致总线挂死
- **原因**：在配置内存映射前，如果在总线上发起了间接指令读取且未复位 QSPI 控制器，STM32H7 的 QUADSPI 控制器状态机容易进入挂起态，导致总线超时。
- **解决**：在进入内存映射前强制复位控制器并软复位 Flash：
  ```c
  QUADSPI->CCR &= 0xf7ffffff;
  QUADSPI->CR  &= 0xfffffffe;
  MX_QUADSPI_Init();
  QSPI_W25Q64JV_Reset();
  ```

### 阻碍 3：缺少中断向量表重定位（VTOR）
- **原因**：Bootloader 运行在 RAM，Cortex-M7 默认 VTOR 指向 `0x00000000`。跳转到外部 Flash 后，如果不更新 VTOR，APP 开启 SysTick 或串口中断将瞬间触发 HardFault。
- **解决**：在跳转前必须重定向向量表：
  ```c
  SCB->VTOR = APPLICATION_ADDRESS; // 0x90000000
  __DSB();
  __ISB();
  ```

---

## 5. 华邦 (Winbond) Flash 命名规则与后缀讲究 (重点选型参考)

在后续项目迭代或更换 Flash 物料时，**后缀字母代表了完全不同的电气特性与硬件默认行为**。如果选错型号，极易发生“芯片烧毁”、“引脚对不上”或“读出全是 0x88”的问题。

### 5.1 完整型号拆解范例
以本项目焊接的芯片 **`W25Q64JVSIQ`** 为例：

```
 W25Q    64    J    V    S    I    Q
(前缀)  (容量) (代系)(电压)(封装)(温度)(特性)
```

| 字段 | 含义 | 范例值与说明 |
| :--- | :--- | :--- |
| **W25Q** | 产品系列 | Winbond SpiFlash 串行 NOR 闪存系列 |
| **64** | 存储容量 | 64M-bit = 8M-Byte（128 = 16MB, 256 = 32MB） |
| **J** | 工艺代系 | J 代工艺（比旧版 F 代更先进，时钟频率更高） |
| **V** | 工作电压 | **V = 2.7V ~ 3.6V (常规 3.3V)**<br>**W = 1.65V ~ 1.95V (超低压 1.8V)** ⚠️ **致命区分！** |
| **S** | 封装形式 | **S / SS = SOP-8 208mil (宽体)**<br>**N = SOP-8 150mil (窄体)**<br>**P / E = WSON-8 无引脚贴片** |
| **I** | 工作温度 | **I = 工业级 (-40°C ~ +85°C)**<br>**J = 工业扩展级 (-40°C ~ +105°C)** |
| **Q** | 特性与 QE 状态 | **Q = Standard SPI & Quad SPI，出厂 QE = 0**<br>**M = Quad SPI，出厂 QE = 1 (永久/默认使能)** |

---

### 5.2 核心选型陷阱与讲究说明

#### ① 电压代系陷阱：`JV` (3.3V) vs `JW` (1.8V)
- **`W25QxxJV`**：标准 3.3V 供电（2.7V ~ 3.6V），STM32H750 标准 3.3V IO 引脚可直接直连。
- **`W25QxxJW`**：1.8V 低压供电（1.65V ~ 1.95V）。
- ⚠️ **切记**：如果采购时不小心买了 `JW` 后缀，焊在 3.3V 板上会**瞬间击穿烧毁 Flash 芯片**！若要用 1.8V Flash，必须增加外部电平转换芯片或将 STM32 VDDIO2 设为 1.8V。

#### ② 封装宽窄体陷阱：`SS` (208mil) vs `SN` (150mil)
- **SOP-8 宽体 (208mil)**：型号通常带 `S` 或 `SS`（本体宽度约 5.28mm）。绝大多数单片机核心板、开发板预留的 8 脚贴片焊盘是 208mil 宽体。
- **SOP-8 窄体 (150mil)**：型号通常带 `SN`（本体宽度约 3.90mm）。
- ⚠️ **踩坑点**：如果焊盘画的是宽体，买成了窄体，芯片引脚会跨度不够，无法平贴焊接。

#### ③ 决定通信成败的关键后缀：`IQ` vs `IM` (QE 位默认值)
华邦状态寄存器 2 的 Bit 1 是 **QE (Quad Enable)** 位：
- **后缀为 `IQ`**（如 `W25Q64JVSSIQ`）：
  - 芯片出厂时 **QE = 0（未使能）**。
  - IO2 默认为硬件写保护 `/WP`，IO3 默认为数据保持 `/HOLD`。
  - 如果软件没有先发送写状态寄存器指令（`0x06` -> `0x31` / `0x01`）将 QE 位置 1，**直接发四线读指令 `0xEB` 必定读出 `0x88` 乱码**！
- **后缀为 `IM`**（如 `W25Q64JVSSIM`）：
  - 芯片出厂时 **QE = 1（默认开启四线）**。
  - 芯片无论在何种情况下都直接支持四线 `0xEB` 读取，**无需软件写寄存器开 QE**，最省心。
- **后缀为 `IN`**：
  - 专用固定 HOLD 功能，不推荐作为通用的 QSPI 使用。

#### ④ 寻址能力代沟：容量 $\le 128\text{M}$ (24-bit) vs $\ge 256\text{M}$ (32-bit)
- **W25Q64 (8MB) / W25Q128 (16MB)**：
  - 采用 **标准 24 位（3 字节）地址**，最大寻址 16MB。
  - 代码中配置 `sCmd.AddressSize = QSPI_ADDRESS_24_BITS`。
- **W25Q256 (32MB) 及以上**：
  - 超出 16MB 边界，必须开启 **32 位（4 字节）地址模式**（发送指令 `0xB7` 进入 4 字节地址模式）。
  - 若更换为 W25Q256，所有驱动代码中的 `AddressSize` 必须改成 `QSPI_ADDRESS_32_BITS`，否则无法寻址 16MB 以上空间。

---

## 6. 后续更换 Flash 操作 Check List

如果您后续更换了外部 Flash 芯片，请对照下表调整工程：

| 变更场景 | `flashalgo_flm` 算法工程调整 | `boot_sram` 引导工程调整 | `uart_extflash` APP 工程调整 |
| :--- | :--- | :--- | :--- |
| **换为相同容量、带 IM 后缀**<br>(如 W25Q64JVSSIM) | 无需任何改动，直接使用 | 无需任何改动，直接使用 | 无需任何改动 |
| **换为更大容量 16MB**<br>(如 W25Q128JVSIQ) | 1. `FlashDev.c` 中 `DevSize` 改为 `0x01000000`<br>2. `quadspi.c` 中 `FlashSize` 改为 `23`<br>3. 重新编译生成 `.FLM` 替换 | `quadspi.c` 中 `FlashSize` 改为 `23` | 分散加载 `.sct` 中容量可调大至 `0x01000000` |
| **换为 32MB 芯片**<br>(如 W25Q256JVEIQ) | 1. 所有命令需切入 32 位地址模式 (`0xB7`)<br>2. `FlashSize` 改为 `24`<br>3. 重新编译生成 `.FLM` 替换 | 1. 初始化时发送 `0xB7` 进 4 字节地址<br>2. 内存映射配置改为 `32_BITS` 地址<br>3. `FlashSize` 改为 `24` | 分散加载 `.sct` 容量改大 |
| **更换为其它品牌**<br>(如 GD25Q64, XT25F64) | 核对该芯片 64KB 块擦除指令（通常也是 `0xD8`）和单线读指令（`0x0B`）即可直接兼容 | 直接兼容 | 无需改动 |

---

## 7. 进阶实战：4 线 Quad SPI 模式调通实录 (XIP 4倍速)

在 1 线模式点亮验证后，为了追求极致的执行效率，我们在 `boot_sram` 中成功调通了 **真正的 4 线 Quad SPI 内存映射模式 (Fast Read Quad I/O, 指令 0xEB)**。以下记录该进阶过程中的关键坑点与最终标准解法。

### 7.1 为什么升级 4 线？
- **带宽对比**：在相同时钟频率（32MHz）下，1 线带宽约 **4MB/s**，而 4 线带宽达 **16MB/s**（时钟提至 133MHz 时可达 **66MB/s**）。
- **执行体验**：Cortex-M7 每次取 32 位指令仅需 8 个时钟，CPU 取指总线不再饥饿，程序运行流畅度与内置 Flash 毫无二致。

### 7.2 4 线调试过程中的三大核心陷阱与终极解法

#### 坑点 1：STM32 HAL 官方“无地址指令接收超时”陷阱
* **现象**：在尝试读取状态寄存器 1（0x05）或状态寄存器 2（0x35）时，调用 `HAL_QSPI_Receive(&hqspi, &sr, ...)` 总是超时卡死。
* **原因剖析**：
  在 HAL 库源码中，`HAL_QSPI_Receive()` 依赖向地址寄存器 `AR` 写值来触发通信时钟（`WRITE_REG(hqspi->Instance->AR, addr_reg);`）。然而**读状态寄存器指令是没有地址阶段的（`QSPI_ADDRESS_NONE`）**，写 `AR` 根本无法启动硬件时钟，导致 HAL 库进入死等标志位超时！
* **终极解法**：
  **绕开 `HAL_QSPI_Receive`**，直接通过 `0x06` 写使能 -> 发送 `0x01`（Write Status Register）连写 2 个字节（`{0x00, 0x02}`，即 SR1 清除写保护，SR2 的 Bit 1 置 1）。发送操作使用 `HAL_QSPI_Transmit()`，直接推入 `DR` 数据寄存器，完全不受“无地址”影响；接着配合 `AutoPollingMemReady()` 确认烧录完成，彻底解决无地址接收超时的官方陷阱！

#### 坑点 2：前次运行 Memory-Mapped 状态残留导致 `HAL_BUSY`（`WriteEnable failed!`）
* **现象**：当程序在调试器中按复位重新运行后，第一句写使能报错：`Error: WriteEnable failed!`。
* **原因剖析**：
  前次运行进入了内存映射模式（`FMODE = 3`）。在 Keil 调试器重新复位运行程序时，QUADSPI 硬件控制器的 `FMODE` 没有被清零，控制器依然处于 BUSY 状态。此时如果直接调用 `HAL_QSPI_Command()` 发送写使能，HAL 库检测到 `hqspi.State != READY` 直接返回 `HAL_BUSY`。
* **终极解法**：
  在 `main()` 开头执行任何 QSPI 操作之前，强制执行硬件终止并复位控制器：
  ```c
  QUADSPI->CR |= QUADSPI_CR_ABORT;             // 硬件强制终止当前传输
  for (volatile int i = 0; i < 5000; i++);
  QUADSPI->CCR = 0;                             // 清空功能模式
  QUADSPI->CR &= ~QUADSPI_CR_EN;                // 关闭 QUADSPI
  HAL_QSPI_DeInit(&hqspi);
  MX_QUADSPI_Init();                            // 重新初始化，恢复为干净的 READY 状态
  ```

#### 坑点 3：4 线 `0xEB` 指令的 Continuous Read（免指令模式）误触发
* **现象**：4 线内存映射读取乱码或无法重入。
* **原因剖析**：
  W25Q64 在 0xEB 指令的地址发送完毕后，需要 2 个周期的模式位（M7-M0）。若此时总线电平未送 `0xFF`，芯片可能被误锁入“Continuous Read Mode”。
* **终极解法**：
  在配置 `0xEB` 内存映射时，严格送出交替字节并配置对应空周期：
  ```c
  sCmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction       = 0xEB;
  sCmd.AddressMode       = QSPI_ADDRESS_4_LINES;
  sCmd.AddressSize       = QSPI_ADDRESS_24_BITS;
  sCmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
  sCmd.AlternateBytesSize= QSPI_ALTERNATE_BYTES_8_BITS;
  sCmd.AlternateBytes    = 0xFF;  // 必须为 0xFF，禁止 Continuous Read
  sCmd.DummyCycles       = 4;     // 配合 Alternate Bytes 使用 4 个周期
  sCmd.DataMode          = QSPI_DATA_4_LINES;
  ```

### 7.3 最终成功验证日志

经过上述三大加固，`boot_sram` 在 4 线 Quad SPI 模式下一次性顺利点亮并完成 XIP 运行：

```text
STM32H750VB Bootloader Starting...
W25Q64 Manufacturer ID: 0xEF, Device ID: 0x16
Configuring QE bit via instruction 0x01 (SR1=0x00, SR2=0x02)...
QE bit successfully enabled and programmed!
Entering Quad (4-line) Memory Mapped Mode (0xEB)...
Quad Memory Mapped Mode Enabled.
Vector Table @ 0x90000000:
  MSP: 0x24000500
  PC : 0x900002AD
Jumping to Application...

STM32H750VB Demo. 
i = 0x0 
```
至此，外部 Flash 从烧录算法（FLM）、单线快速读取引导，到 4 线 Quad 高速就地执行（XIP）全栈打通！

---

## 8. 三大工程运行位置、内存映射与 Keil 设置详解 (极重要)

在 STM32H750 外置 Flash 开发中，理解各个工程**代码在物理介质上的存放位置**、**运行时的内存映射 (Memory Map)** 以及 **Keil 的配套设置**，是避免“下载报错”、“跳转 HardFault”和“地址冲突”的核心基石。

### 8.1 三大工程物理定位与运行机制一览表

| 工程名称 | 目标载体 | 代码存放地址 (ROM/Flash) | 数据运行地址 (RAM) | 启动与操作方式 |
| :--- | :--- | :--- | :--- | :--- |
| **`flashalgo_flm`**<br>(下载算法) | 编译输出 `.FLM`<br>算法插件 | **无**<br>(仅编译出插件由 Keil 调用) | **`0x24000000`**<br>(AXI SRAM, 临时加载运行) | 不单独烧录。由 Keil 在按 **`F8`** 烧录 `uart_extflash` 时自动搬运到 RAM 中临时执行 |
| **`boot_sram`**<br>(RAM 引导器) | 内部 AXI SRAM | **`0x24000000`**<br>(AXI SRAM, 占 512KB) | **`0x24000000` ~ `0x24080000`**<br>(数据与代码共用 AXI SRAM) | **不能按 F8 下载！**<br>使用 **`Ctrl + F5`** (Debug) 配合 `debug_sram.ini` 加载到 RAM 后按 **`F5`** 运行 |
| **`uart_extflash`**<br>(用户 APP) | 外部 QSPI Flash<br>(W25Q64) | **`0x90000000`**<br>(映射到外部 Flash 8MB) | **`0x24000000`**<br>(AXI SRAM) 或 DTCM | 在 Keil 中配置外部算法后，按 **`F8`** (Download) 直接烧录进外部 Flash，通过 XIP 运行 |

---

### 8.2 工程一：`flashalgo_flm` (Keil 外部下载算法)

#### ① 角色与运行原理
它**不是固化在单片机内部的固件**，而是一个由 Keil 调试器（ST-Link/J-Link）动态加载执行的工具小程序。当您在 APP 工程中点击 `Download (F8)` 时：
1. Keil 将 `.FLM` 文件注入单片机的 AXI SRAM（`0x24000000`）；
2. Keil 操控单片机 CPU 执行算法里的 `Init()`、`EraseSector()`、`ProgramPage()` 和 `Verify()`；
3. 将固件二进制数据写入外部 Flash。写入完成后，单片机复位，算法在 RAM 中的任务结束。

#### ② Keil 核心工程设置
* **Target 选项卡**：
  - 不需要配置常规的 IROM/IRAM，通常依赖算法专用的链接脚本。
* **User 选项卡 (After Build/Rebuild)**：
  - 必须配置复制命令，将生成的 `axf` 重命名为 `.FLM` 放到 Keil 的 Flash 算法目录中：
    ```cmd
    cmd.exe /C copy "flashalgo\flashalgo.axf" "..\..\STM32H750VB_W25Q64JV_Dual.flm"
    ```
* **FlashDev.c 核心描述参数**：
  - `DevAdr = 0x90000000`（外部 Flash 映射基地址）
  - `DevSize = 0x00800000`（单片 8MB，`0x00800000`）
  - `SECTOR_END` 扇区大小：`0x10000, 0x000000`（64KB 块）

---

### 8.3 工程二：`boot_sram` (内部 RAM 引导器)

#### ① 角色与运行原理
负责硬件底层的桥梁：复位外部 Flash、开启 QE 四线模式、建立 `0x90000000` 内存映射，并将中断向量表指向 `0x90000000` 后跳入 APP。它运行在内部高速 AXI SRAM。

#### ② 内存布局与分散加载 (`debug_sram.sct`)
工程使用专属的分散加载脚本，将代码段和数据段全部指定在 AXI SRAM 内部：
```sct
LR_IROM1 0x24000000 0x00080000  {    ; 加载区基地址 0x24000000，大小 512KB
  RW_IRAM1 0x24000000 0x00080000  {  ; 运行区基地址 0x24000000
   *.o (RESET, +First)               ; 中断向量表放在 0x24000000 起始
   *(InRoot$$Sections)
   .ANY (+RO)                        ; 代码段放 RAM
   .ANY (+XO)
   .ANY (+RW +ZI)                    ; 数据段与全局变量放 RAM
  }
}
```

#### ③ Keil 核心工程设置
* **Linker 选项卡**：
  - 勾选 `Use Memory Layout from Target Dialog` 取消勾选；
  - 在 `Scatter File` 中填入：`.\debug_sram.sct`。
* **Debug 选项卡 (最核心设置)**：
  - 勾选 `Initialization File`，填入：`.\debug_sram.ini`。
  - **`debug_sram.ini` 脚本必配内容**：
    ```ini
    FUNC void Setup (void) {
        SP = _RDWORD(0x24000000);        // 读取 0x24000000 作为初始栈顶
        PC = _RDWORD(0x24000004);        // 读取 0x24000004 作为初始入口 PC
        XPSR = 0x01000000;               // 设置状态寄存器
        _WDWORD(0xE000ED08, 0x24000000); // 设置 SCB->VTOR 向量表偏移
    }
    LOAD %L INCREMENTAL                  // 将 axf 直接下载灌入 RAM
    Setup();                             // 执行 Setup() 设置 PC 与 SP
    ```
* **⚠️ 关键操作禁忌**：
  - **严禁按 `F8` (Download)**：因为 `0x24000000` 是 RAM 地址，Keil 没有针对 RAM 地址的 Flash 烧录算法，按 F8 会报 `No Algorithm found for 24000000H`。
  - **正确操作**：按 **`Ctrl + F5`** 进入调试状态，Keil 会执行 ini 脚本将代码灌入 RAM，然后按 **`F5`** 全速运行即可完成引导跳转！

---

### 8.4 工程三：`uart_extflash` (外部 Flash 应用程序 APP)

#### ① 角色与运行原理
用户的最终主程序。代码存放在外部 Flash 中，CPU 取指令通过 QUADSPI 控制器的硬件 AXI 总线实时读取执行（XIP 就地执行），全局变量与栈运行在内部 AXI SRAM。

#### ② 内存布局与分散加载 (`uart_extflash.sct`)
```sct
LR_IROM1 0x90000000 0x00800000  {    ; 加载区：外部 Flash，基地址 0x90000000，容量 8MB
  ER_IROM1 0x90000000 0x00800000  {  ; 执行区：代码段运行在 0x90000000 (XIP 就地执行)
   *.o (RESET, +First)               ; APP 向量表放在 0x90000000 起始
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x24000000 0x00080000  {  ; 数据区：全局变量/静态变量在内部 AXI SRAM (512KB)
   .ANY (+RW +ZI)
  }
}
```

#### ③ Keil 核心工程设置
* **Target 选项卡**：
  - **IROM1**：Start = `0x90000000`，Size = `0x00800000`（勾选 Startup）
  - **IRAM1**：Start = `0x24000000`，Size = `0x00080000`（512KB AXI SRAM）
* **Debug -> Settings -> Flash Download 选项卡 (关键配置)**：
  - **Programming Algorithm (算法列表)**：
    - 点击 `Add`，选中添加之前生成的 `STM32H750VB_W25Q64JV_Dual` 外部算法；
    - 算法属性显示：Address Range = `90000000H - 907FFFFFH`，Size = `8M`。
  - **RAM for Algorithm (算法运行内存配置)**：
    - **Start**: `0x24000000`（指向 AXI SRAM）
    - **Size**: **`0x40000` (256KB)** ⚠️ *千万不能填默认的 32KB(0x8000)，否则会报 `Cannot Load Flash Programming Algorithm`*！
  - 勾选 `Verify`（烧录后自动比对校验）。
* **操作方式**：
  - 点击编译（Build）；
  - 直接按 **`F8` (Download)**，Keil 自动调用外部算法擦除、烧录并校验 `0x90000000` 外部 Flash！

---

### 8.5 完整开发与调试工作流 (Workflow)

```
[步骤 1: 制作算法]
打开 flashalgo_flm -> 编译生成 .FLM 算法 -> 复制到 Keil 算法目录 (只需做一次)
        |
        v
[步骤 2: 编写并烧录 APP]
打开 uart_extflash -> 添加外部 FLM 算法 -> 按 F8 烧录进外部 Flash (0x90000000)
        |
        v
[步骤 3: 启动运行]
打开 boot_sram -> 按 Ctrl + F5 灌入 RAM -> 按 F5 运行 -> 开启 4 线映射并跳转进入 APP
```


