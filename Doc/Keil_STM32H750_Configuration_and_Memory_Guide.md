# Keil MDK 核心配置与 STM32H750 内存架构全解析实战指南

> **适用芯片**：STM32H750VBT6 (ARM Cortex-M7 @ 480MHz)  
> **工具链**：Keil MDK-ARM (Arm Compiler 6 / AC6) + CMSIS-DAP / ST-LINK  
> **文档目的**：系统性拆解 Keil Target、Linker、Debug、Flash Download 等关键配置窗口各项参数的物理意义、底层工作原理及避坑法则。

---

## 目录
1. [Target 对话框核心配置详解](#1-target-对话框核心配置详解)
   - 1.1 Read/Only Memory Areas (Flash / ROM)
   - 1.2 Read/Write Memory Areas (RAM)
   - 1.3 `default` 勾选与不勾选的本质区别
   - 1.4 `Startup` 单选框的底层作用
   - 1.5 硬件浮点单元 (Floating Point Hardware) 与 SVD
2. [Linker 选项卡与链接器底层机制](#2-linker-选项卡与链接器底层机制)
   - 2.1 `Use Memory Layout from Target Dialog` 深度剖析
   - 2.2 armlink 命令行参数全解
   - 2.3 Scatter 分散加载脚本 (.sct) 的作用
   - 2.4 典型报错解析与根因
3. [STM32H750 多域分布式内存全景解析](#3-stm32h750-多域分布式内存全景解析)
   - 3.1 内存分布表 (1060KB 物理分区)
   - 3.2 DTCM (0x20000000) vs AXI SRAM (0x24000000) 深度对比
   - 3.3 为什么工程主 RAM 首选 AXI SRAM？
   - 3.4 为什么 DTCM 不能直接做普通外设 DMA？
4. [Flash Download 烧录机制与算法配置](#4-flash-download-烧录机制与算法配置)
   - 4.1 `RAM for Algorithm` 的物理意义与内存切分模型
   - 4.2 为什么必须设为 `Start: 0x24000000, Size: 0x10000`？
   - 4.3 “算法假返回成功”与 `Flash=FFH` 校验错误的根源
   - 4.4 STM32H750 片内 Flash 擦除失败 (Erase Failed) 排查
5. [Cortex-M 中断向量表 (SCB->VTOR) 与系统启动避坑](#5-cortex-m-中断向量表-scb-vtor-与系统启动避坑)
   - 5.1 `printf` 刚打印几个字符随即死机/腰斩的根本原因
   - 5.2 `system_stm32h7xx.c` 中 `SCB->VTOR` 的正确配置姿势
6. [推荐配置速查模板 (Best Practices)](#6-推荐配置速查模板-best-practices)

---

## 1. Target 对话框核心配置详解

进入路径：`Project -> Options for Target (Alt + F7) -> Target 选项卡`。

![Target Dialog](media/target_dialog.png)

### 1.1 Read/Only Memory Areas (只读代码区，即 Flash / ROM)
告诉链接器：“芯片有哪些合法的非易失性存储介质，编译出的机器指令（Code）、常量数据（RO-data）和中断向量表存放在哪里。”

* **`IROM1` (片内 Flash 1)**：
  - **Start**: `0x08000000`（STM32 片内 Flash 物理起始地址）。
  - **Size**: `0x00020000`（128KB，即 $128 \times 1024 = 131,072$ 字节）。
* **`ROM1 ~ ROM3` (片外扩展 ROM)**：
  - 用于外部 NOR Flash / QSPI Flash。例如当开发运行在外部 Flash 的 APP 时，可配置为 `Start: 0x90000000, Size: 0x00800000` (8MB)。

### 1.2 Read/Write Memory Areas (读写存储区，即 RAM)
告诉链接器：“芯片有哪些易失性 RAM，程序运行时的全局变量、静态变量（RW-data / ZI-data）、堆（Heap）和栈（Stack）默认分配到哪里。”

* **`IRAM1`**：Start = `0x20000000`，Size = `0x00020000`（128KB DTCM 数据紧耦合内存）。
* **`IRAM2`**：Start = `0x24000000`，Size = `0x00080000`（512KB AXI 系统总线 SRAM）。

### 1.3 `default` 勾选与不勾选的本质区别

在每一行存储器的最左侧，都有一个名为 **`default`** 的复选框：

| 状态 | 编译/链接器行为 | 实际影响 |
| :--- | :--- | :--- |
| **打勾 (勾选)** | **设定为“默认主存储段”** | 所有 C 代码中的函数、全局变量、局部变量栈，**在不加任何特殊修饰的情况下，全部默认丢到打勾的这个段里**。 |
| **未勾选 (空)** | **设定为“合法扩展段”** | 告诉链接器这块内存**物理存在且合法**，但**普通代码和变量默认不进入这里**。只有在源码中使用 `__attribute__((section("...")))` 指定段名，或者在 Keil 树状工程树右键某个 C 文件独立指定时，才会被放置进未打勾的段。 |

> **⚠️ 踩坑警示（同时勾选多个 RAM）**：  
> 若同时勾选了 `IRAM1` (DTCM) 和 `IRAM2` (AXI SRAM)，Keil 会**优先把所有变量填满第一个勾选的 IRAM1**。这会导致你定义的全局数组（如串口缓冲区）被偷偷塞进 DTCM，一旦开启 DMA 就会发生总线异常！**因此建议只勾选 IRAM2！**

### 1.4 `Startup` 单选框的底层作用
在只读区域的最右侧有一个 `Startup` 单选钮：
* 芯片上电复位后，硬件 PC 寄存器首先抓取的中断向量表（Vector Table，即 `startup_stm32h750xx.s` 中定义的 `__Vectors`）**必须严格放置在标记为 `Startup` 的段的首地址**。
* STM32 从片内 Flash 启动时，必须选中 `IROM1`（`0x08000000`）。

### 1.5 硬件浮点单元 (Floating Point Hardware) 与 SVD
* **Floating Point Hardware**:
  - Cortex-M7 核心具备双精度浮点单元（DP-FPU）。
  - 设置为 **`Double Precision`**：`double` 和 `float` 运算均由硬件 FPU 指令执行，无需软浮点库模拟，速度提升数十倍。
* **System Viewer File**:
  - 关联 `STM32H750.svd` 文件。在进入 Debug 调试界面时，菜单栏 `Peripherals` 能以友好树状图显示所有寄存器位定义（Bitfields）。

---

## 2. Linker 选项卡与链接器底层机制

进入路径：`Project -> Options for Target -> Linker 选项卡`。

### 2.1 `Use Memory Layout from Target Dialog` 深度剖析
该复选框是 Keil 链接管理的分水岭：

```
                             [Use Memory Layout from Target Dialog]
                                               |
                   +---------------------------+---------------------------+
                   |                                                       |
               [ 打勾 (Enabled) ]                                     [ 取消勾选 (Disabled) ]
                   |                                                       |
       根据 Target 选项卡参数                                   完全使用用户指定的 Scatter
      在编译时动态生成默认 Scatter 脚本                          分散加载描述文件 (如 boot_sram.sct)
   (无需手动维护链接文件，适合常规项目)                           (适合极其复杂的段分布和多核项目)
```

### 2.2 armlink 命令行参数全解
在编译信息中常见到的这一串参数：
```text
--cpu Cortex-M7.fp.dp *.o
--strict --scatter "boot_sram\boot_sram.sct"
--summary_stderr --info summarysizes --map --load_addr_map_info --xref --callgraph --symbols
--info sizes --info totals --info unused --info veneers
--list "boot_sram.map"
-o boot_sram\boot_sram.axf
```

* `--cpu Cortex-M7.fp.dp *.o`：目标 CPU 指定为带双精度浮点的 Cortex-M7，并链接全部编译出的 `*.o` 目标对象。
* `--strict`：启用严格校验模式，任何段属性或架构小冲突均视作 Error 而非 Warning。
* `--scatter "xxx.sct"`：指定分散加载描述文件。当未勾选 `Use Memory Layout from Target Dialog` 时由 Keil 传入。
* `--map` & `--list "xxx.map"`：生成符号地址映射表（Map 文件），用于查看每个函数和变量的最终绝对地址。
* `--load_addr_map_info`：记录加载域（Flash 存储地址）与运行域（RAM 执行地址）的对应关系。
* `--symbols`：在 Map 文件中列出所有全局/局部函数、变量的绝对地址。
* `--info unused`：打印出项目中哪些函数或段从未被调用（未引用垃圾回收统计）。
* `-o boot_sram\boot_sram.axf`：指定链接输出的可执行 ELF/AXF 文件路径。

### 2.3 典型报错解析与根因
1. **`*** Scatter Error: no default 'Read/Write' range selected`**：
   - **根因**：开启了 Target 自动布局，但 Target 选项卡右侧的 `IRAM1`、`IRAM2` 等所有 RAM 项最左侧的 `default` **一个小勾都没打**！链接器不知道变量往哪放。
2. **`Could not open scatter description file: No such file or directory`**：
   - **根因**：取消了 `Use Memory Layout from Target Dialog`，但输入框里指定的 `.sct` 文件物理路径不存在。

---

## 3. STM32H750 多域分布式内存全景解析

STM32H7 系列不同于传统的 STM32F1/F4（平铺单一 SRAM），它采用了**总线矩阵（Bus Matrix）+ 多时钟域（D1/D2/D3）架构**，总 SRAM 容量高达 **1060 KB（约 1MB）**。

### 3.1 内存分布表 (1060KB 物理分区)

```
0x00000000 +-----------------------------------+ 64KB (ITCM) - CPU 核心私有
           | ITCM (指令紧耦合)                   |
0x20000000 +-----------------------------------+ 128KB (DTCM) - CPU 核心私有
           | DTCM (数据紧耦合)                   |
0x24000000 +-----------------------------------+ 512KB (AXI SRAM) - D1 域中央总线枢纽 (★最推荐主 RAM)
           | AXI SRAM                          |
0x30000000 +-----------------------------------+ 128KB (SRAM1) - D2 域外设/DMA
           | SRAM1                             |
0x30020000 +-----------------------------------+ 128KB (SRAM2) - D2 域通信/网络
           | SRAM2                             |
0x30040000 +-----------------------------------+ 32KB (SRAM3) - D2 域专用缓存
           | SRAM3                             |
0x38000000 +-----------------------------------+ 64KB (SRAM4) - D3 低功耗域
           | SRAM4                             |
0x38800000 +-----------------------------------+ 4KB (Backup SRAM) - 纽扣电池保持
           | Backup SRAM                       |
           +-----------------------------------+
```

### 3.2 DTCM (`0x20000000`) vs AXI SRAM (`0x24000000`) 深度对比

| 维度 | DTCM (`0x20000000`) | AXI SRAM (`0x24000000`) |
| :--- | :--- | :--- |
| **全称** | Data Tightly-Coupled Memory | AXI System SRAM |
| **物理位置** | 直接镶嵌在 **Cortex-M7 核心内部** | 挂在 **D1 域 64-bit AXI 交叉开关总线矩阵** 上 |
| **时钟与访问速度** | **0 等待周期（直连 480MHz CPU 时钟）** | 高速（约 200~240MHz AXI 频率，受 Cache 缓冲） |
| **Cache 机制** | **天生绕过 D-Cache**（强一致性，无需 Clean/Invalidate） | 经过 D-Cache 缓存 |
| **总线连接方式** | 64 位专有直通总线 | 64 位 AXI4 系统互联互通矩阵 |
| **调试器 DAP / 算法访问**| **受限**（必须通过 AHBS-to-DTCM 总线桥中转） | **原生全双工**（DAP 调试器作为 AXI Master 平等读写） |
| **DMA 访问能力** | ❌ **普通 DMA1/DMA2 无法访问**（仅限特殊 MDMA） | ✅ **所有 DMA、MDMA、BDMA 原生全支持** |

### 3.3 为什么工程主 RAM 首选 AXI SRAM？
1. **容量巨大（512KB）**：占了芯片总 SRAM 的一半，能够容纳绝大多数复杂工程的全局变量和堆栈。
2. **零硬件陷阱**：无论以后是使用普通变量、外设 DMA 缓冲、还是接入 FreeRTOS 任务栈，都不会发生总线不可达的 HardFault 故障。

### 3.4 为什么 DTCM 不能直接做普通外设 DMA？
STM32H750 的总线矩阵架构中，普通 DMA 控制器（DMA1、DMA2）物理上位于 **D2 域**。D2 域的总线主设备无法跨越反向连接到内核私有的 DTCM。只有位于 D1 域的 **MDMA (Master DMA)** 才具备访问 DTCM 的总线桥通道。因此，如果把全局变量设在 DTCM，随手写一个 `HAL_UART_Receive_DMA(&huart1, buffer, len)` 就会因为 DMA 无法寻址而崩溃。

---

## 4. Flash Download 烧录机制与算法配置

进入路径：`Project -> Options for Target -> Debug -> CMSIS-DAP/ST-Link Settings -> Flash Download`。

![Flash Download Dialog](media/flash_download_dialog.png)

### 4.1 `RAM for Algorithm` 的物理意义与内存切分模型
`RAM for Algorithm` 并不是程序运行时的内存，它**仅仅在点击下载（F8 / LOAD）的这几秒内生效**：

```
                          RAM for Algorithm 分配空间 (例如 64KB)
    0x24000000                                                          0x24010000
    +------------------------------------+---------------------------------------+
    | 1. 算法代码与运行时堆栈 (FlashPrg.o) | 2. 烧录数据分片缓冲区 (Data Buffer)    |
    | (Keil 灌入下载算法自身的机器码)      | (通过 SWD 将要写入 Flash 的 bin 暂存在此)|
    +------------------------------------+---------------------------------------+
                      |                                     |
                      \----------------- CPU 执行 ----------/
                                   ProgramPage(adr, sz, buf)
                                               |
                                               v
                                    真正写入片内/片外 Flash
```

### 4.2 为什么必须设为 `Start: 0x24000000, Size: 0x10000`？
1. **避免撞车踩内存**：
   下载算法自身代码 + 静态数据 + 执行栈约需 16KB~24KB，Keil 下载引擎分批传送的数据块又需要 16KB~32KB。
   若 Size 填默认的 `0x1000` (4KB) 或 `0x2000` (8KB)，**数据缓冲区会直接压跨算法的栈空间**！
2. **选择 AXI SRAM 的必然性**：
   CoreSight SWD/DAP 调试器作为 AXI 总线上的 Bus Master，能以极高时钟直接高速写入 AXI SRAM，无总线桥冲突。

### 4.3 “算法假返回成功”与 `Flash=FFH` 校验错误的根源
* **现象**：
  ```text
  Erase Done.
  Programming Done.
  Contents mismatch at: 08000000H (Flash=FFH Required=B0H) !
  ```
* **根本诱因**：
  由于 `RAM for Algorithm` 大小不足，导致入参 `sz` 被踩碎变为 0，或者缓冲区指针异常。算法内部的防呆保护触发提前退出：
  ```c
  int ProgramPage (unsigned long adr, unsigned long sz, unsigned char *buf) {
      if (sz == 0) return (0); // 返回 0 (代表 OK)!
      ...
  }
  ```
  Keil 误以为写入完成，结果一校验发现 Flash 压根没被写入，依然全保留擦除后的 `0xFF`。

### 4.4 STM32H750 片内 Flash 擦除失败 (Erase Failed) 排查
* **单一 128KB 大扇区特性**：
  STM32H750 片内只有 128KB Flash，且**整块 Flash 只有一个扇区 (Sector 0)**。
  Keil 的 `Erase Sectors` 算法经常因扇区编号匹配失败而直接报错 `Erase Failed`。
* **解决对策**：
  1. 将擦除选项选为 **`Erase Full Chip` (全片擦除)**；
  2. 在 Debug 选项卡中设置 **Connect = `under Reset`**，**Reset = `SYSRESETREQ` 或 `HW RESET`**，防止 CPU 正在执行死循环或外设占用导致无法抢占 Flash 控制器。

---

## 5. Cortex-M 中断向量表 (SCB->VTOR) 与系统启动避坑

### 5.1 `printf` 刚打印几个字符随即死机/腰斩的根本原因
* **现象**：
  程序固化到片内 Flash (`0x08000000`) 后上电，串口仅打印 `STM32H750`（原本是 `STM32H750VB Bootloader Starting...`），后半段被截断，MCU 完全卡死。
* **底层原理剖析**：
  1. `printf` 往串口数据寄存器（TDR）塞入字符，硬件开始以波特率（如 115200bps）逐位向外串行发送；
  2. 几个字符发送耗时约数百微秒，此时**SysTick（1ms 系统滴答定时器）或串口外设中断到来**；
  3. CPU 硬件根据 **`SCB->VTOR`** 寄存器的值计算中断服务函数入口地址：  
     $\text{Handler Address} = \text{SCB->VTOR} + (\text{IRQn} + 16) \times 4$
  4. 如果 `SCB->VTOR` 依然残留为 SRAM 调试时的 `0x24000000`，CPU 就会跳向 AXI SRAM 去抓取向量；
  5. 此时 AXI SRAM 中只有普通的变量数据，读出的不是合法 Thumb 指令地址，**CPU 瞬间触发硬件严重故障异常（HardFault）死锁**！串口输出物理掐断！

### 5.2 `system_stm32h7xx.c` 中 `SCB->VTOR` 的正确配置姿势
检查 [boot_sram/Core/Src/system_stm32h7xx.c](file:///c:/Users/LIAN/Desktop/STM32H750/STM32H750VB/boot_sram/Core/Src/system_stm32h7xx.c) 的 `SystemInit()` 函数：
```c
  /* Configure the Vector Table location -------------------------------------*/
#if defined(USER_VECT_TAB_ADDRESS)
  SCB->VTOR = 0x08000000ul; /* 固化在片内 Flash 必须为 0x08000000ul */
  // SCB->VTOR = 0x24000000ul; /* 仅在 SRAM 中独立调试运行时使用 */
#endif /* USER_VECT_TAB_ADDRESS */
```

---

## 6. 推荐配置速查模板 (Best Practices)

### 场景一：片内 Flash Bootloader (固化运行)

| 配置选项卡 | 参数项 | 推荐设定值 |
| :--- | :--- | :--- |
| **Target** | IROM1 (Flash) | **勾选 default**, Start: `0x08000000`, Size: `0x00020000`, Startup 选中 |
| | IRAM1 (DTCM) | **不勾选 default** (保留 `0x20000000`, `0x20000` 备用) |
| | IRAM2 (AXI SRAM) | **勾选 default**, Start: `0x24000000`, Size: `0x00080000` |
| | Floating Point | **Double Precision** |
| **Linker** | Use Memory Layout from Target Dialog | **勾选 (Enabled)** |
| **Debug -> Flash Download** | Download Function | **Erase Full Chip** |
| | RAM for Algorithm | Start: **`0x24000000`**, Size: **`0x00010000`** |
| | Programming Algorithm | **STM32H7x_128kB** (08000000H - 0801FFFFH) |
| **Debug (Connect)** | Connect & Reset Options | **under Reset** / **SYSRESETREQ** |

---

### 场景二：外部 QSPI Flash 应用程序 (APP XIP 执行)

| 配置选项卡 | 参数项 | 推荐设定值 |
| :--- | :--- | :--- |
| **Target** | ROM1 (QSPI Flash) | **勾选 default**, Start: `0x90000000`, Size: `0x00800000`, Startup 选中 |
| | IRAM1 (DTCM) | **不勾选 default** |
| | IRAM2 (AXI SRAM) | **勾选 default**, Start: `0x24000000`, Size: `0x00080000` |
| **Linker** | Use Memory Layout from Target Dialog | **勾选 (Enabled)** |
| **Debug -> Flash Download** | RAM for Algorithm | Start: **`0x24000000`**, Size: **`0x00040000`** (256KB) |
| | Programming Algorithm | **STM32H750VB_W25Q64JV_Dual** (90000000H - 907FFFFFH, 8MB) |
| **App 源码启动项** | main() 入口 | 初始化第一行显式设置：`SCB->VTOR = 0x90000000;` |
