/*
 * @Description: ST25R3916 官方 RFAL 协议栈 C++ 生命周期封装
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:23:15
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>
#include <memory>

#include "bus/bus_guide.h"
#include "platform/rfal_platform_status.h"

extern "C" {
#include "rfal_nfc.h"
#include "rfal_rf.h"
}

namespace stsw_st25rfal002_cpp_bus_driver {

class RfalPlatformContext;

/**
 * @brief 将 SPI 接口的 ST25R3916 接入 ST 官方 RFAL 协议栈
 *
 * 传入的 SPI 对象必须使用 Mode 1 和 MSB first。STSW-ST25RFAL002 使用
 * 全局协议状态，因此同一时间最多只能初始化一个 St25r3916 对象。
 */
class St25r3916 final {
 public:
  static constexpr int32_t kDefaultSpiFrequencyHz = 5000000;
  static constexpr int32_t kMaximumSpiFrequencyHz = 10000000;

  St25r3916(std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus,
      int32_t interrupt_pin, int32_t chip_select_pin);

  ~St25r3916();

  // ST25R3916 对象独占官方协议状态，禁止复制和移动。
  St25r3916(const St25r3916&) = delete;
  St25r3916& operator=(const St25r3916&) = delete;
  St25r3916(St25r3916&&) = delete;
  St25r3916& operator=(St25r3916&&) = delete;

  /**
   * @brief 初始化 SPI、延迟中断任务和完整 NFC RFAL 状态机
   * @param frequency_hz SPI 时钟频率，单位为 Hz，默认 5 MHz，最大 10 MHz
   * @return 官方 RFAL 返回码
   */
  ReturnCode Init(int32_t frequency_hz = kDefaultSpiFrequencyHz);

  /**
   * @brief 反初始化 RFAL 并释放硬件资源
   * @param delete_bus 是否同时请求释放底层共享 SPI 总线
   * @return 首个官方 RFAL 或平台资源释放错误
   */
  ReturnCode Deinit(bool delete_bus = false);

  /**
   * @brief 查询 ST25R3916 是否可以使用
   * @return Init() 成功后至 Deinit() 完成前返回 true
   */
  bool initialized() const;

  /**
   * @brief 获取官方 RFAL 返回码以下的平台错误
   * @return 最近一次平台错误
   */
  PlatformError platform_error() const;

  /**
   * @brief 执行一次底层 RFAL Worker
   */
  void Worker();

  /**
   * @brief 执行一次高层 NFC 发现和激活 Worker
   */
  void NfcWorker();

 private:
  std::unique_ptr<RfalPlatformContext> platform_;
  bool initialized_ = false;
};

}  // namespace stsw_st25rfal002_cpp_bus_driver
