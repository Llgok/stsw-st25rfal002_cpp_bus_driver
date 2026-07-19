<h1 align="center">stsw-st25rfal002_cpp_bus_driver</h1>

## **English | [Chinese](./README_CN.md)**

[![Release](https://img.shields.io/github/v/release/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver/releases)
[![License](https://img.shields.io/github/license/Llgok/stsw-st25rfal002_cpp_bus_driver?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

`stsw-st25rfal002_cpp_bus_driver` is an ESP-IDF bridge between ST's official
STSW-ST25RFAL002 RF/NFC Abstraction Layer and
[`cpp_bus_driver`](https://github.com/Llgok/cpp_bus_driver).
It manages I2C/SPI resources, synchronization, interrupt processing, and the RFAL
lifecycle with C++ objects while preserving the official RFAL APIs.

## Table of Contents

- [Features](#features)
- [Supported Frameworks](#supported-frameworks)
- [Quick Start](#quick-start)
- [Notes](#notes)

## Features

- Builds the official ST25R3916 RFAL and NFC-A/B/F/V, ST25TB, ISO-DEP,
  NFC-DEP, T1T, T2T, T4T, collision-detection, and antenna-tuning modules.
- Uses `cpp_bus_driver` for I2C, SPI, GPIO, delay, and logging services.
- Combines each official command into one full-duplex SPI transaction with one
  complete hardware-controlled chip-select pulse.
- Defers the active-high INT signal to a dedicated FreeRTOS task before
  executing official RFAL SPI operations.
- Protects communication, RFAL Worker execution, and interrupt status in a
  multi-task environment.
- Selects I2C or SPI from the supplied bus object and detects ST25R3916 or
  ST25R3916B through `IC_IDENTITY` at runtime.
- Exposes the official public C APIs through one C-linkage aggregate header.

The public headers, build configuration, and bundled official RFAL sources are
the source of truth for the currently supported functionality.

## Supported Frameworks

| Framework | Status | Description |
| --- | --- | --- |
| ESP-IDF 5.5.3 or later | Supported | Uses `cpp_bus_driver` for low-level hardware access |

The current port supports I2C and SPI. I2C defaults to 400 kHz and accepts up
to 1 MHz. SPI uses mode 1 with MSB-first transfers, defaults to 5 MHz, and
accepts up to 10 MHz.

## Quick Start

### Integration

#### Use as ESP-IDF Components

Place `cpp_bus_driver` and this repository in the project's `components`
directory:

```text
your_project/
├── components/
│   ├── cpp_bus_driver/
│   └── stsw-st25rfal002_cpp_bus_driver/
├── main/
└── CMakeLists.txt
```

Clone commands:

```bash
git clone https://github.com/Llgok/cpp_bus_driver.git
git clone https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver.git
```

Then include the unified entry header:

```cpp
#include "stsw_st25rfal002_cpp_bus_driver_library.h"
```

When only part of the component is needed, include the relevant official RFAL
or C++ wrapper header directly. Refer to the public headers for available entry
points.

#### Use as Git Submodules

```bash
git submodule add https://github.com/Llgok/cpp_bus_driver.git
git submodule add https://github.com/Llgok/stsw-st25rfal002_cpp_bus_driver.git
git submodule update --init --recursive
```

Submodule directories may be customized to match the project layout, as long
as ESP-IDF can discover both components.

### Automatic ST25R Chip Detection

No Kconfig or constructor chip selection is required. `Init()` reads
`IC_IDENTITY` and loads the matching ST25R3916 or ST25R3916B initialization,
analog-configuration, and DPO data.

### Create the SPI Bus

Create a mode 1 SPI bus with `cpp_bus_driver`:

```cpp
#include <memory>

#include "cpp_bus_driver_library.h"
#include "stsw_st25rfal002_cpp_bus_driver_library.h"

auto spi_bus = std::make_shared<cpp_bus_driver::HardwareSpi>(
    mosi_pin, sclk_pin, miso_pin, SPI2_HOST, 1);
```

Select the SPI host, pins, and frequency for the target hardware. The module's
`I2C_EN` pin must be held low before using the SPI interface.

### Create the Driver Object

Construct the lifecycle owner with the SPI bus, active-high interrupt pin, and
chip-select pin:

```cpp
stsw_st25rfal002_cpp_bus_driver::St25r3916 nfc(
    spi_bus, interrupt_pin, chip_select_pin);

if (nfc.Init() != RFAL_ERR_NONE) {
  return;
}
```

I2C uses the other constructor of the same class. Supply the I2C bus, interrupt
pin, and device address; no transport macro is required:

```cpp
std::shared_ptr<cpp_bus_driver::BusI2cGuide> i2c_bus = /* board I2C bus */;
stsw_st25rfal002_cpp_bus_driver::St25r3916 nfc(
    i2c_bus, interrupt_pin, 0x50);
```

After `Init()` succeeds, use `chip_identity()`, `chip_revision()`, and
`is_st25r3916b()` to inspect the detected chip.

### Use the Official NFC API

Configure discovery through the unchanged official RFAL API, then call
`NfcWorker()` continuously:

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

### Lifecycle and Concurrency

- Call `NfcWorker()` frequently while using the high-level discovery API.
- Call `Worker()` only when directly managing the low-level RFAL API.
- `Deinit(false)` releases only the I2C/SPI device used by the current object.
- `Deinit(true)` also requests deletion of the underlying bus; use it only
  when the current object exclusively owns that bus.
- The official stack uses global internal state. Only one `St25r3916` object
  may be initialized at a time.
- `platform_error()` reports transport and platform failures below the
  official `ReturnCode` interface.
- The interrupt worker uses the highest available system priority. Tasks that
  call RFAL must not keep scheduling disabled or continuously busy-loop at the
  highest priority.
- Worker loops must stop advancing the state machine after a transport or platform
  failure.

## Notes

- SPI mode requires `I2C_EN` to be held low and `VDD_IO` to be compatible with
  the host controller's logic level.
- Files below `third_party/STSW-ST25RFAL002` are unchanged official ST sources
  and must not be modified by the port.
- Platform adaptations belong in `src/platform`; C++ lifecycle and convenience
  interfaces belong in `src/st25r3916`.
- `src/vendor_port` contains the runtime transport and chip adaptations selected
  by CMake instead of the corresponding upstream translation units.

Refer to the public headers, the bundled RFAL documentation, and the official
ST25R3916/ST25R3916B documentation for detailed APIs and hardware requirements.
