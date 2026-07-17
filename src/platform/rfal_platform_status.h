/*
 * @Description: ST25RFAL 平台桥接层状态定义
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:23:10
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

namespace stsw_st25rfal002_cpp_bus_driver {

// RFAL 官方返回码以下的平台错误。
enum class PlatformError : uint8_t {
  kNone = 0,
  kInvalidConfiguration,
  kAlreadyInUse,
  kResourceAllocationFailed,
  kGpioFailed,
  kSpiFailed,
  kI2cFailed,
  kInterruptFailed,
  kAssertionFailed,
  kOfficialErrorHandler,
};

}  // namespace stsw_st25rfal002_cpp_bus_driver
