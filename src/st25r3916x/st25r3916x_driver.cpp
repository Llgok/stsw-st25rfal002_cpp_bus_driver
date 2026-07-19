/*
 * @Description: ST25R3916X 官方 RFAL 协议栈 C++ 生命周期实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:25:13
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#include "st25r3916x/st25r3916x_driver.h"

#include <utility>

#include "platform/rfal_platform_internal.h"
#include "vendor_port/st25r3916_runtime.h"

namespace stsw_st25rfal002_cpp_bus_driver {

St25r3916x::St25r3916x(std::shared_ptr<cpp_bus_driver::BusI2cGuide> bus,
    int32_t interrupt_pin, uint16_t address)
    : platform_(std::make_unique<RfalPlatformContext>(
          std::move(bus), interrupt_pin, address)) {}
St25r3916x::St25r3916x(std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus,
    int32_t interrupt_pin, int32_t chip_select_pin)
    : platform_(std::make_unique<RfalPlatformContext>(
          std::move(bus), interrupt_pin, chip_select_pin)) {}

St25r3916x::~St25r3916x() { Deinit(false); }

ReturnCode St25r3916x::Init() {
  if (platform_ == nullptr) {
    return RFAL_ERR_INVALID_HANDLE;
  }
  const uint32_t frequency_hz =
      platform_->transport() == RfalPlatformContext::Transport::kI2c
          ? kDefaultI2cFrequencyHz
          : kDefaultSpiFrequencyHz;
  return Init(frequency_hz);
}

ReturnCode St25r3916x::Init(uint32_t frequency_hz) {
  if (initialized_) {
    return RFAL_ERR_NONE;
  }
  if (platform_ == nullptr) {
    return RFAL_ERR_INVALID_HANDLE;
  }
  if (!platform_->Bind()) {
    return platform_->error() == PlatformError::kAlreadyInUse
               ? RFAL_ERR_BUSY
               : RFAL_ERR_INVALID_HANDLE;
  }
  if (!platform_->Init(frequency_hz)) {
    const ReturnCode result =
        platform_->error() == PlatformError::kInvalidConfiguration
            ? RFAL_ERR_PARAM
            : RFAL_ERR_IO;
    if (platform_->Deinit(false)) {
      platform_->Unbind();
    }
    return result;
  }

  st25r3916RuntimeReset();
  ReturnCode result = st25r3916RuntimeDetectChip();
  if (!platform_->healthy()) {
    result = RFAL_ERR_IO;
  }
  if (result == RFAL_ERR_NONE) {
    result = st25r3916RuntimeLoadAnalogConfig();
  }
  const bool rfal_initialization_attempted = result == RFAL_ERR_NONE;
  if (rfal_initialization_attempted) {
    result = rfalNfcInitialize();
  }
  if (result == RFAL_ERR_NONE && !platform_->healthy()) {
    result = RFAL_ERR_IO;
  }
  if (result != RFAL_ERR_NONE) {
    // 官方初始化可能已安装中断或开启振荡器，先在平台仍有效时回滚芯片状态。
    if (rfal_initialization_attempted) {
      const ReturnCode rollback_result = rfalDeinitialize();
      if (rollback_result != RFAL_ERR_NONE) {
        result = rollback_result;
      }
    }
    if (platform_->Deinit(false)) {
      platform_->Unbind();
    }
    st25r3916RuntimeReset();
    return result;
  }

  initialized_ = true;
  return RFAL_ERR_NONE;
}

ReturnCode St25r3916x::Deinit(bool delete_bus) {
  if (platform_ == nullptr) {
    return RFAL_ERR_INVALID_HANDLE;
  }

  ReturnCode result = RFAL_ERR_NONE;
  if (initialized_) {
    result = rfalDeinitialize();
    initialized_ = false;
  }
  const bool platform_deinitialized = platform_->Deinit(delete_bus);
  if (!platform_deinitialized) {
    if (result == RFAL_ERR_NONE) {
      result = RFAL_ERR_IO;
    }
    return result;
  }
  platform_->Unbind();
  st25r3916RuntimeReset();
  return result;
}

bool St25r3916x::initialized() const { return initialized_; }

uint8_t St25r3916x::chip_identity() const {
  return st25r3916RuntimeGetIdentity();
}

uint8_t St25r3916x::chip_revision() const {
  return st25r3916RuntimeGetRevision();
}

bool St25r3916x::is_st25r3916b() const {
  return st25r3916RuntimeIsSt25r3916B();
}

PlatformError St25r3916x::platform_error() const {
  return platform_ == nullptr ? PlatformError::kInvalidConfiguration
                              : platform_->error();
}

void St25r3916x::Worker() {
  if (initialized_) {
    rfalWorker();
  }
}

void St25r3916x::NfcWorker() {
  if (initialized_) {
    rfalNfcWorker();
  }
}

}  // namespace stsw_st25rfal002_cpp_bus_driver
