<h1 align="center">stsw-st25rfal002_cpp_bus_driver</h1>

## **[English](./README.md) | 中文**

[![Release](https://img.shields.io/github/v/release/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver/releases)
[![License](https://img.shields.io/github/license/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

`stsw-st25rfal002_cpp_bus_driver` 是 ST 官方 STSW-ST25RFAL002 RF/NFC
抽象层与
[`cpp_bus_driver`](https://github.com/Llgok/cpp_bus_driver)
之间的 ESP-IDF 桥接组件。它使用 C++ 对象管理 SPI 资源、同步、中断处理和
RFAL 生命周期，同时保留官方 RFAL API。

## 目录

- [功能](#功能)
- [支持框架](#支持框架)
- [快速开始](#快速开始)
- [注意事项](#注意事项)

## 功能

- 编译官方 ST25R3916 RFAL，以及 NFC-A/B/F/V、ST25TB、ISO-DEP、NFC-DEP、
  T1T、T2T、T4T、碰撞检测和天线调谐模块。
- 使用 `cpp_bus_driver` 提供 SPI、GPIO、延时和日志服务。
- 将每条官方命令合并为一次全双工 SPI 事务，由硬件产生一个完整的片选脉冲。
- 将高电平有效的 INT 信号延迟到专用 FreeRTOS 任务，再执行官方 RFAL SPI 操作。
- 为通信、RFAL Worker 和中断状态提供多任务并发保护。
- 通过 Kconfig 选择 ST25R3916 或 ST25R3916B。
- 通过一个使用 C 语言链接的聚合头文件公开官方公共 API。

公开头文件、构建配置和随库提供的官方 RFAL 源码，是当前支持功能的准确信息来源。

## 支持框架

| 框架 | 状态 | 说明 |
| --- | --- | --- |
| ESP-IDF 5.5.3 及以上 | 支持 | 使用 `cpp_bus_driver` 访问底层硬件 |

当前移植支持 SPI Mode 1 和 MSB first。默认 SPI 频率为 5 MHz，允许的最大频率
为 10 MHz。

## 快速开始

### 集成

#### 作为 ESP-IDF 组件使用

将 `cpp_bus_driver` 和本仓库放入工程的 `components` 目录：

```text
your_project/
├── components/
│   ├── cpp_bus_driver/
│   └── stsw-st25rfal002_cpp_bus_driver/
├── main/
└── CMakeLists.txt
```

克隆命令：

```bash
git clone https://github.com/Llgok/cpp_bus_driver.git
git clone https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver.git
```

然后包含统一入口头文件：

```cpp
#include "stsw_st25rfal002_cpp_bus_driver_library.h"
```

仅需要部分功能时，也可以直接包含对应的官方 RFAL 或 C++ 封装头文件。可用入口以
公开头文件为准。

#### 作为 Git 子模块使用

```bash
git submodule add https://github.com/Llgok/cpp_bus_driver.git
git submodule add https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver.git
git submodule update --init --recursive
```

子模块目录可以根据工程布局调整，但必须确保 ESP-IDF 能发现这两个组件。

### 选择 ST25R 芯片

组件默认选择 ST25R3916。硬件使用 ST25R3916B 时，在 menuconfig 的
`STSW-ST25RFAL002 Configuration` 中切换芯片型号。

### 创建 SPI 总线

使用 `cpp_bus_driver` 创建 Mode 1 SPI 总线：

```cpp
#include <memory>

#include "cpp_bus_driver_library.h"
#include "stsw_st25rfal002_cpp_bus_driver_library.h"

auto spi_bus = std::make_shared<cpp_bus_driver::HardwareSpi>(
    mosi_pin, sclk_pin, miso_pin, SPI2_HOST, 1);
```

根据目标硬件选择 SPI 控制器、引脚和频率。使用 SPI 接口前，模块的 `I2C_EN`
引脚必须保持低电平。

### 创建驱动对象

使用 SPI 总线、高电平有效的中断引脚和片选引脚创建生命周期对象：

```cpp
stsw_st25rfal002_cpp_bus_driver::St25r3916 nfc(
    spi_bus, interrupt_pin, chip_select_pin);

if (nfc.Init() != RFAL_ERR_NONE) {
  return;
}
```

### 使用官方 NFC API

通过未修改的官方 RFAL API 配置发现参数，并持续调用 `NfcWorker()`：

```cpp
rfalNfcDiscoverParam parameters = {};
parameters.compMode = RFAL_COMPLIANCE_MODE_NFC;
parameters.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B |
                        RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V;
parameters.totalDuration = 1000;
parameters.devLimit = 1;
parameters.nfcfBR = RFAL_BR_212;

if (rfalNfcDiscover(&parameters) != RFAL_ERR_NONE) {
  return;
}

while (true) {
  nfc.NfcWorker();
  if (nfc.platform_error() !=
      stsw_st25rfal002_cpp_bus_driver::PlatformError::kNone) {
    break;
  }
  vTaskDelay(pdMS_TO_TICKS(1));
}
```

### 生命周期与并发约束

- 使用高层发现 API 时，需要频繁调用 `NfcWorker()`。
- 直接管理底层 RFAL API 时才单独调用 `Worker()`。
- `Deinit(false)` 只释放当前对象使用的 SPI 设备。
- `Deinit(true)` 还会请求删除底层 SPI 总线，仅当当前对象独占总线时才能使用。
- 官方协议栈使用全局内部状态，因此同一时间只能初始化一个 `St25r3916` 对象。
- `platform_error()` 用于报告官方 `ReturnCode` 接口以下的总线和平台错误。
- 中断任务使用系统最高可用优先级。调用 RFAL 的任务不得长期关闭调度器，也不得
  以最高优先级持续忙等。
- 发生 SPI 或平台错误后，Worker 循环必须停止推进状态机。

## 注意事项

- SPI 模式要求 `I2C_EN` 保持低电平，并确保 `VDD_IO` 与主控逻辑电平兼容。
- 当前移植没有启用官方代码中的编译期 I2C 路径。
- `third_party/STSW-ST25RFAL002` 保存未经修改的 ST 官方源码，移植层不得修改。
- 平台适配应放在 `src/platform`；C++ 生命周期和便利接口应放在
  `src/st25r3916`。

详细 API 和硬件要求请参考公开头文件、随库提供的 RFAL 文档，以及 ST 官方
ST25R3916/ST25R3916B 文档。
