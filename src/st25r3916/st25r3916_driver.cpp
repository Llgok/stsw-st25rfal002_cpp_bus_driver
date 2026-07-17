/*
 * @Description: ST25R3916 官方 RFAL 协议栈 C++ 生命周期实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:25:13
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#include "st25r3916/st25r3916_driver.h"

#include <utility>

#include "platform/rfal_platform_internal.h"

namespace stsw_st25rfal002_cpp_bus_driver {

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_I2C)
St25r3916::St25r3916(std::shared_ptr<cpp_bus_driver::BusI2cGuide> bus,
    int32_t interrupt_pin, uint16_t address)
    : platform_(std::make_unique<RfalPlatformContext>(
          std::move(bus), interrupt_pin, address)) {}
#else
St25r3916::St25r3916(std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus,
    int32_t interrupt_pin, int32_t chip_select_pin)
    : platform_(std::make_unique<RfalPlatformContext>(
          std::move(bus), interrupt_pin, chip_select_pin)) {}
#endif

St25r3916::~St25r3916() { Deinit(false); }

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_I2C)
ReturnCode St25r3916::Init(uint32_t frequency_hz) {
#else
ReturnCode St25r3916::Init(int32_t frequency_hz) {
#endif
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

  ReturnCode result = rfalNfcInitialize();
  if (result == RFAL_ERR_NONE && !platform_->healthy()) {
    result = RFAL_ERR_IO;
  }
  if (result != RFAL_ERR_NONE) {
    // 官方初始化可能已安装中断或开启振荡器，先在平台仍有效时回滚芯片状态。
    const ReturnCode rollback_result = rfalDeinitialize();
    if (rollback_result != RFAL_ERR_NONE) {
      result = rollback_result;
    }
    if (platform_->Deinit(false)) {
      platform_->Unbind();
    }
    return result;
  }

  initialized_ = true;
  return RFAL_ERR_NONE;
}

ReturnCode St25r3916::Deinit(bool delete_bus) {
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
  return result;
}

bool St25r3916::initialized() const { return initialized_; }

PlatformError St25r3916::platform_error() const {
  return platform_ == nullptr ? PlatformError::kInvalidConfiguration
                              : platform_->error();
}

void St25r3916::Worker() {
  if (initialized_) {
    rfalWorker();
  }
}

void St25r3916::NfcWorker() {
  if (initialized_) {
    rfalNfcWorker();
  }
}

}  // namespace stsw_st25rfal002_cpp_bus_driver
