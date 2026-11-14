/*
 * @Description: ST25R3916X 官方 RFAL 协议栈 C++ 生命周期封装
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "bus/bus_base.h"
#include "platform/rfal_platform_status.h"

extern "C" {
#include "rfal_nfc.h"
#include "rfal_rf.h"
}

namespace stsw_st25rfal002_cpp_bus_driver {

class RfalPlatformContext;

/**
 * @brief 将 I2C 或 SPI 接口的 ST25R3916/3916B 接入 ST 官方 RFAL 协议栈
 *
 * 总线类型由构造函数参数自动确定；SPI 对象必须使用 Mode 1 和 MSB first。
 * Init() 会读取 IC_IDENTITY 自动识别芯片。STSW-ST25RFAL002 使用全局协议
 * 状态，因此同一时间最多只能初始化一个 St25r3916x 对象。
 */
class St25r3916x final {
 public:
  static constexpr uint32_t kDefaultI2cFrequencyHz = 400000;
  static constexpr uint32_t kMaximumI2cFrequencyHz = 1000000;
  static constexpr uint16_t kDefaultI2cAddress = 0x50;
  static constexpr uint32_t kDefaultSpiFrequencyHz = 5000000;
  static constexpr uint32_t kMaximumSpiFrequencyHz = 10000000;

  St25r3916x(std::shared_ptr<cpp_bus_driver::I2cBusBase> bus,
      int32_t interrupt_pin, uint16_t address = kDefaultI2cAddress);
  St25r3916x(std::shared_ptr<cpp_bus_driver::SpiBusBase> bus,
      int32_t interrupt_pin, int32_t chip_select_pin);

  ~St25r3916x();

  // ST25R3916X 对象独占官方协议状态，禁止复制和移动。
  St25r3916x(const St25r3916x&) = delete;
  St25r3916x& operator=(const St25r3916x&) = delete;
  St25r3916x(St25r3916x&&) = delete;
  St25r3916x& operator=(St25r3916x&&) = delete;

  /**
   * @brief 初始化总线、中断任务、NFC RFAL 状态机及默认关闭的 DPO 参数
   * @param frequency_hz 总线频率，单位为 Hz；I2C 最大 1 MHz，SPI 最大 10 MHz
   * @return 官方 RFAL 返回码
   */
  ReturnCode Init();
  ReturnCode Init(uint32_t frequency_hz);

  /**
   * @brief 反初始化 RFAL 并释放硬件资源
   *
   * 调用前必须停止其他任务中的 Worker()、NfcWorker() 以及直接 RFAL API
   * 调用，释放过程不能与协议栈或芯片访问并发执行。
   *
   * @param delete_bus 是否同时请求释放底层共享 I2C/SPI 总线
   * @return 官方 RFAL、芯片通信或平台资源释放错误
   */
  ReturnCode Deinit(bool delete_bus = false);

  /**
   * @brief 在不访问芯片的情况下释放平台和总线资源
   *
   * 当芯片无法通信时，跳过 RFAL 反初始化和寄存器访问，
   * 仅释放中断、任务及总线等本地资源。
   *
   * @param delete_bus 是否同时请求释放底层共享 I2C/SPI 总线
   * @return 平台资源释放结果
   */
  ReturnCode DeinitLocalResources(bool delete_bus = false);

  /**
   * @brief 进入 RFAL 低功耗模式
   * @param mode RFAL 低功耗模式；ST25R3916X 当前支持 Power Down
   * @return 官方 RFAL 返回码
   */
  ReturnCode StartLowPowerMode(rfalLpMode mode = RFAL_LP_MODE_PD);

  /**
   * @brief 退出 RFAL 低功耗模式
   * @return 官方 RFAL 返回码
   */
  ReturnCode StopLowPowerMode();

  /**
   * @brief 查询 ST25R3916X 是否可以使用
   * @return Init() 成功后至 Deinit() 完成前返回 true
   */
  bool initialized() const;

  /** @return 当前保存的完整 IC_IDENTITY 寄存器值，未初始化时为 0 */
  uint8_t chip_identity() const;
  /** @return IC_IDENTITY 中的芯片修订版本字段 */
  uint8_t chip_revision() const;
  /** @return 当前识别到的芯片是否为 ST25R3916B */
  bool is_st25r3916b() const;

  /**
   * @brief 获取官方 RFAL 返回码以下的平台错误
   * @return 最近一次平台错误
   */
  PlatformError platform_error() const;

  /**
   * @brief 平台健康时执行一次底层 RFAL Worker
   */
  void Worker();

  /**
   * @brief 平台健康时执行一次高层 NFC 发现和激活 Worker
   */
  void NfcWorker();

 private:
  std::unique_ptr<RfalPlatformContext> platform_;
  mutable std::recursive_mutex lifecycle_mutex_;
  bool initialized_ = false;
  bool low_power_mode_active_ = false;
};

}  // namespace stsw_st25rfal002_cpp_bus_driver
