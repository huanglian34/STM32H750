# STM32H750VBT6 QSPI Flash 全栈开发工程仓库

本项目实现了基于 **STM32H750VBT6** 配合板载 **W25Q64JVSIQ (8MB QSPI NOR Flash)** 的完整软硬件方案，包含 Keil 外部 Flash 烧录算法、片内 Flash 固化 Bootloader、外部 Flash 高速 XIP 应用程序以及详尽的技术踩坑与配置指南。

---

## 📁 目录结构

* 📂 **`Doc/`**：项目技术文档与硬件原理图
  - 📖 **[`Doc/STM32H750VB_QSPI_Debug_Notes.md`](Doc/STM32H750VB_QSPI_Debug_Notes.md)**：QSPI 调试实战手记、硬件引脚、Winbond 型号辨析、8线/4线模式演进及完整调试记录。
  - 📖 **[`Doc/Keil_STM32H750_Configuration_and_Memory_Guide.md`](Doc/Keil_STM32H750_Configuration_and_Memory_Guide.md)**：Keil Target / Linker / Flash Download 核心参数全解析、H750 1060KB 分布式内存架构深度指南。
  - 📄 **[`Doc/schdoc.pdf`](Doc/schdoc.pdf)**：硬件开发板原理图。
* 📂 **`STM32H750VB/`**：核心代码工程
  - 🛠️ **`flashalgo_flm/`**：Keil FLM 外部 Flash 下载算法工程（生成 `STM32H750VB_W25Q64JV_Dual.flm`）。
  - 🚀 **`boot_sram/`**：Bootloader 工程（可链接至片内 Flash `0x08000000` 固化，负责开启 Quad 内存映射并跳转）。
  - 💻 **`uart_extflash/`**：用户应用程序（链接至 `0x90000000`，由外部算法直烧，支持高速 XIP 运行与丰富诊断信息）。
