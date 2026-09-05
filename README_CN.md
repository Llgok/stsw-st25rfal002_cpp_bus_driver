<h1 align="center">stsw-st25rfal002_cpp_bus_driver</h1>

## **[English](./README.md) | 中文**

[![Release](https://img.shields.io/github/v/release/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver/releases)
[![License](https://img.shields.io/github/license/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

`stsw-st25rfal002_cpp_bus_driver` 是 ST 官方 STSW-ST25RFAL002 RF/NFC
抽象层与
[`cpp_bus_driver`](https://github.com/Llgok/cpp_bus_driver)
之间的 ESP-IDF 桥接组件。它使用 C++ 对象管理 I2C/SPI 资源、同步、中断处理和
RFAL 生命周期，同时保留官方 RFAL API。
当前随组件提供的上游 RFAL 版本为 STSW-ST25RFAL002 v4.2.0。

## 目录

- [功能](#功能)
- [支持框架](#支持框架)
- [快速开始](#快速开始)
- [注意事项](#注意事项)

## 功能

- 编译官方 ST25R3916 RFAL，以及 NFC-A/B/F/V、ST25TB、ISO-DEP、NFC-DEP、
  T1T、T2T、T4T、碰撞检测和天线调谐模块。
- 使用 `cpp_bus_driver` 提供 I2C、SPI、GPIO、延时和日志服务。
- 将每条官方命令合并为一次全双工 SPI 事务，由硬件产生一个完整的片选脉冲。
- 将高电平有效的 INT 信号延迟到专用 FreeRTOS 任务，再执行官方 RFAL SPI 操作。
- 为通信、RFAL Worker 和中断状态提供多任务并发保护。
- 由传入的总线对象自动选择 I2C 或 SPI，并通过 `IC_IDENTITY` 自动识别
  ST25R3916 或 ST25R3916B。
- 自动加载对应型号的模拟配置和 DPO 表。DPO 初始化后默认保持关闭，需要时由
  应用显式开启。
- 通过一个使用 C 语言链接的聚合头文件公开官方公共 API。

公开头文件、构建配置和随库提供的官方 RFAL 源码，是当前支持功能的准确信息来源。

## 支持框架

| 框架 | 状态 | 说明 |
| --- | --- | --- |
| ESP-IDF 5.5.3 及以上 | 支持 | 使用 `cpp_bus_driver` 访问底层硬件 |

当前移植支持 I2C 和 SPI。I2C 默认频率为 400 kHz、最大为 1 MHz；SPI 要求
Mode 1 和 MSB first，默认频率为 5 MHz、最大为 10 MHz。

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
#include "stsw_st25rfal002_cpp_bus_driver.h"
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

### 自动识别 ST25R 芯片

无需通过 Kconfig 或构造函数选择芯片型号。`Init()` 会先读取 `IC_IDENTITY`，
然后自动加载 ST25R3916 或 ST25R3916B 对应的初始化、模拟配置和 DPO 参数。
驱动会执行 `rfalDpoInitialize()`，但动态功率调整仍保持关闭；需要使用时由应用
调用 `rfalDpoSetEnabled(true)`。

应用构建配置不要定义 `ST25R3916`、`ST25R3916B`，也不要关闭动态模拟配置或
DPO 功能。这些宏由组件统一管理，实际芯片始终根据 `IC_IDENTITY` 在运行时选择。

官方 `t_st25r3916Regs` 的 Space-B 数组长度由编译期型号决定。需要按照运行时
识别结果读取完整寄存器时，应使用 `st25r3916RuntimeGetRegsDump()`；该接口在
ST25R3916B 上会读取全部 24 个 Space-B 寄存器：

```cpp
st25r3916RuntimeRegs registers = {};
if (st25r3916RuntimeGetRegsDump(&registers) == RFAL_ERR_NONE) {
  // ST25R3916 的 RsBCount 为16，ST25R3916B 为24。
}
```

### 创建 SPI 总线

使用 `cpp_bus_driver` 创建 Mode 1 SPI 总线：

```cpp
#include <memory>

#include "cpp_bus_driver.h"
#include "stsw_st25rfal002_cpp_bus_driver.h"

auto spi_bus = std::make_shared<cpp_bus_driver::HardwareSpi>(
    mosi_pin, sclk_pin, miso_pin, SPI2_HOST, 1);
```

根据目标硬件选择 SPI 控制器、引脚和频率。使用 SPI 接口前，模块的 `I2C_EN`
引脚必须保持低电平。

### 创建驱动对象

使用 SPI 总线、高电平有效的中断引脚和片选引脚创建生命周期对象：

```cpp
stsw_st25rfal002_cpp_bus_driver::St25r3916x nfc(
    spi_bus, interrupt_pin, chip_select_pin);

if (nfc.Init() != RFAL_ERR_NONE) {
  return;
}
```

I2C 使用同一个类的另一个构造函数；传入 I2C 总线、中断引脚和设备地址即可，
无需设置任何传输宏：

```cpp
std::shared_ptr<cpp_bus_driver::BusI2cGuide> i2c_bus = /* 板级 I2C 总线 */;
stsw_st25rfal002_cpp_bus_driver::St25r3916x nfc(
    i2c_bus, interrupt_pin, 0x50);
```

`Init()` 成功后可通过 `chip_identity()`、`chip_revision()` 和
`is_st25r3916b()` 查询自动识别结果。

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
- `Deinit(false)` 只释放当前对象使用的 I2C/SPI 设备。
- `Deinit(true)` 还会请求删除底层总线，仅当当前对象独占总线时才能使用。
- 调用 `Deinit()` 或销毁对象前，必须先停止所有正在调用 `Worker()`、
  `NfcWorker()` 或直接 RFAL/芯片 API 的任务；资源释放不能与协议栈或芯片访问并发。
- `Deinit()` 返回平台 I/O 错误时，对象会保留释放失败的资源状态，可以再次调用
  `Deinit()` 重试清理。
- 官方协议栈使用全局内部状态，因此同一时间只能初始化一个 `St25r3916x` 对象。
- `platform_error()` 用于报告官方 `ReturnCode` 接口以下的总线和平台错误。
- 中断任务使用系统最高可用优先级。调用 RFAL 的任务不得长期关闭调度器，也不得
  以最高优先级持续忙等。
- C++ 的 `Worker()` 和 `NfcWorker()` 会在检测到平台错误后停止推进状态机；
  直接调用官方 RFAL/芯片 API 的任务也必须在发现错误后退出。

## 注意事项

- SPI 模式要求 `I2C_EN` 保持低电平，并确保 `VDD_IO` 与主控逻辑电平兼容。
- `third_party/STSW-ST25RFAL002` 保存未经修改的 ST 官方源码，移植层不得修改。
- 平台适配应放在 `src/platform`；C++ 生命周期和便利接口应放在
  `src/st25r3916x`。
- `src/vendor_port` 保存通过 CMake 替换编译的运行时总线和芯片适配版本。

详细 API 和硬件要求请参考公开头文件、随库提供的 RFAL 文档，以及 ST 官方
ST25R3916/ST25R3916B 文档。
