/*
 * @Description: ST25R3916X 官方 RFAL 协议栈 C++ 生命周期实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:25:13
 * @LastEditTime: 2026-07-31 09:03:47
 * @License: GPL 3.0
 */
#include "st25r3916x/st25r3916x_driver.h"

#include <utility>

#include "platform/rfal_platform_internal.h"
#include "rfal_dpo.h"
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
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
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
  if (result == RFAL_ERR_NONE) {
    // DPO 默认保持关闭，仅初始化当前芯片对应的默认参数和回调。
    rfalDpoInitialize();
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
  low_power_mode_active_ = false;
  return RFAL_ERR_NONE;
}

ReturnCode St25r3916x::Deinit(bool delete_bus) {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (platform_ == nullptr) {
    return RFAL_ERR_INVALID_HANDLE;
  }

  ReturnCode result = RFAL_ERR_NONE;
  if (initialized_) {
    // rfalDeinitialize() 会直接停止当前活动并进入 Power Down，
    // 无需先退出低功耗模式再短暂唤醒振荡器。
    result = rfalDeinitialize();
    if (result == RFAL_ERR_NONE && !platform_->healthy()) {
      result = RFAL_ERR_IO;
    }
    initialized_ = false;
    low_power_mode_active_ = false;
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

ReturnCode St25r3916x::DeinitLocalResources(bool delete_bus) {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (platform_ == nullptr) {
    return RFAL_ERR_INVALID_HANDLE;
  }

  initialized_ = false;
  low_power_mode_active_ = false;
  const bool platform_deinitialized = platform_->Deinit(delete_bus);
  if (platform_deinitialized) {
    platform_->Unbind();
  }
  st25r3916RuntimeReset();
  return platform_deinitialized ? RFAL_ERR_NONE : RFAL_ERR_IO;
}

ReturnCode St25r3916x::StartLowPowerMode(rfalLpMode mode) {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (!initialized_ || platform_ == nullptr) {
    return RFAL_ERR_WRONG_STATE;
  }
  if (mode != RFAL_LP_MODE_PD) {
    return RFAL_ERR_NOTSUPP;
  }
  if (low_power_mode_active_) {
    return RFAL_ERR_NONE;
  }
  if (!platform_->healthy()) {
    return RFAL_ERR_IO;
  }

  const ReturnCode result = rfalLowPowerModeStart(mode);
  if (result != RFAL_ERR_NONE) {
    return result;
  }
  // RFAL 成功后立即同步软件状态；平台错误是独立的返回值，
  // 不能让包装层与 RFAL 内部的低功耗状态分离。
  low_power_mode_active_ = true;
  return platform_->healthy() ? RFAL_ERR_NONE : RFAL_ERR_IO;
}

ReturnCode St25r3916x::StopLowPowerMode() {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (!initialized_ || platform_ == nullptr) {
    return RFAL_ERR_WRONG_STATE;
  }
  if (!low_power_mode_active_) {
    return RFAL_ERR_NONE;
  }
  if (!platform_->healthy()) {
    return RFAL_ERR_IO;
  }

  const ReturnCode result = rfalLowPowerModeStop();
  if (result != RFAL_ERR_NONE) {
    return result;
  }
  low_power_mode_active_ = false;
  return platform_->healthy() ? RFAL_ERR_NONE : RFAL_ERR_IO;
}

bool St25r3916x::initialized() const {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  return initialized_;
}

uint8_t St25r3916x::chip_identity() const {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  return st25r3916RuntimeGetIdentity();
}

uint8_t St25r3916x::chip_revision() const {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  return st25r3916RuntimeGetRevision();
}

bool St25r3916x::is_st25r3916b() const {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  return st25r3916RuntimeIsSt25r3916B();
}

PlatformError St25r3916x::platform_error() const {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  return platform_ == nullptr ? PlatformError::kInvalidConfiguration
                              : platform_->error();
}

void St25r3916x::Worker() {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (initialized_ && !low_power_mode_active_ && platform_ != nullptr &&
      platform_->healthy()) {
    rfalWorker();
  }
}

void St25r3916x::NfcWorker() {
  std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
  if (initialized_ && !low_power_mode_active_ && platform_ != nullptr &&
      platform_->healthy()) {
    rfalNfcWorker();
  }
}

}  // namespace stsw_st25rfal002_cpp_bus_driver
