/*
 * @Description: RFAL 平台接口的 ESP-IDF 内部实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:38:14
 * @LastEditTime: 2026-07-31 09:03:47
 * @License: GPL 3.0
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <memory>

#include "bus/bus_guide.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/rfal_platform.h"
#include "platform/rfal_platform_status.h"

extern "C" {
#include "st25r3916.h"
}

namespace stsw_st25rfal002_cpp_bus_driver {

/**
 * @brief 管理 ST RFAL 官方 C 库使用的唯一平台上下文
 *
 * STSW-ST25RFAL002 使用文件级全局变量保存协议和中断状态，因此同一时间
 * 只能存在一个已初始化的平台上下文。公开驱动通过 Bind() 和 Unbind()
 * 管理该限制。
 */
class RfalPlatformContext final {
 public:
  enum class Transport {
    kI2c,
    kSpi,
  };

  RfalPlatformContext(std::shared_ptr<cpp_bus_driver::BusI2cGuide> bus,
      int32_t interrupt_pin, uint16_t address);
  RfalPlatformContext(std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus,
      int32_t interrupt_pin, int32_t chip_select_pin);

  ~RfalPlatformContext();

  // 平台上下文持有独占的官方全局状态，禁止复制。
  RfalPlatformContext(const RfalPlatformContext&) = delete;
  RfalPlatformContext& operator=(const RfalPlatformContext&) = delete;

  /**
   * @brief 将当前对象绑定为官方 RFAL 使用的平台上下文
   * @return 绑定成功或当前对象已绑定时返回 true
   */
  bool Bind();

  /**
   * @brief 取消当前对象的平台上下文绑定
   */
  void Unbind();

  /**
   * @brief 初始化构造函数选定的总线设备、同步资源和中断任务
   * @param frequency_hz I2C 或 SPI 总线频率，单位为 Hz
   * @return 所有平台资源初始化成功时返回 true
   */
  bool Init(uint32_t frequency_hz);

  /**
   * @brief 释放中断、同步资源和 I2C/SPI 设备
   * @param delete_bus 是否同时请求释放底层共享总线
   * @return 请求的资源全部释放成功时返回 true
   */
  bool Deinit(bool delete_bus);

  /**
   * @brief 获取最近一次平台错误
   * @return 最近一次平台错误
   */
  PlatformError error() const;

  /**
   * @brief 查询平台是否没有记录错误
   * @return 当前无平台错误时返回 true
   */
  bool healthy() const;

  /**
   * @brief 清除已记录的平台错误
   */
  void ClearError();

  /**
   * @brief 获取 ST25R3916 通信互斥锁
   */
  void ProtectCommunication();

  /**
   * @brief 释放 ST25R3916 通信互斥锁
   */
  void UnprotectCommunication();

  /**
   * @brief 进入 RFAL 中断状态临界区
   */
  void ProtectIrqStatus();

  /**
   * @brief 退出 RFAL 中断状态临界区
   */
  void UnprotectIrqStatus();

  /**
   * @brief 获取 RFAL Worker 互斥锁
   */
  void ProtectWorker();

  /**
   * @brief 释放 RFAL Worker 互斥锁
   */
  void UnprotectWorker();

  /**
   * @brief 配置 ST25R3916 中断输入引脚
   */
  void InitializeInterruptPin();

  /**
   * @brief 保存官方中断回调并安装 GPIO 中断
   * @param callback 官方 RFAL 中断处理回调
   */
  void SetInterruptCallback(RfalPlatformInterruptCallback callback);

  /**
   * @brief 读取 ST25R3916 中断引脚电平
   * @return 平台健康且引脚为高电平时返回 true
   */
  bool InterruptPinIsHigh() const;

  Transport transport() const;

  void I2cStart();
  void I2cStop();
  void I2cRepeatStart();
  void I2cSlaveAddressWrite(uint8_t address);
  void I2cSlaveAddressRead(uint8_t address);
  void I2cTransmit(
      const uint8_t* buffer, uint16_t length, bool last, bool transmit_only);
  void I2cReceive(uint8_t* buffer, uint16_t length);
  /**
   * @brief 标记一次 ST25R3916 SPI 事务开始
   */
  void SpiSelect();

  /**
   * @brief 标记一次 ST25R3916 SPI 事务结束
   */
  void SpiDeselect();

  /**
   * @brief 执行一次 ST25R3916 SPI 传输
   * @param transmit_buffer 发送缓冲区
   * @param receive_buffer 接收缓冲区，为空时仅发送
   * @param length 传输字节数
   */
  void SpiTransceive(
      const uint8_t* transmit_buffer, uint8_t* receive_buffer, uint16_t length);

  /**
   * @brief 获取毫秒系统时钟
   * @return 当前系统运行时间，单位为 ms
   */
  uint32_t GetSystemTick() const;

  /**
   * @brief 阻塞当前任务指定时间
   * @param delay_ms 延时时间，单位为 ms
   */
  void Delay(uint32_t delay_ms) const;

  /**
   * @brief 输出官方 RFAL 可变参数日志
   * @param format printf 风格的格式字符串
   * @param arguments 可变参数列表
   */
  void LogV(const char* format, va_list arguments) const;

  /**
   * @brief 保存平台错误并输出错误日志
   * @param error 平台错误
   * @param message 错误说明
   */
  void SetError(PlatformError error, const char* message);

  /**
   * @brief 获取当前绑定的平台上下文
   * @return 当前平台上下文，未绑定时返回 nullptr
   */
  static RfalPlatformContext* active();

 private:
  static constexpr uint32_t kMaximumI2cFrequencyHz = 1000000;
  static constexpr uint32_t kMaximumSpiFrequencyHz = 10000000;
  static constexpr uint32_t kInterruptTaskStackSize = 4096;
  static constexpr UBaseType_t kInterruptTaskPriority =
      configMAX_PRIORITIES - 1;
  static constexpr TickType_t kTaskStopTimeout = pdMS_TO_TICKS(1000);

  /**
   * @brief 在 GPIO ISR 中唤醒专用中断任务
   * @param argument 未使用的 GPIO ISR 参数
   */
  static void IRAM_ATTR InterruptHandler(void* argument);

  /**
   * @brief FreeRTOS 中断任务入口
   * @param argument 当前平台上下文
   */
  static void InterruptTaskEntry(void* argument);

  /**
   * @brief 等待 GPIO 通知并在任务上下文执行官方中断回调
   */
  void InterruptTask();

  /**
   * @brief 创建通信锁、Worker 锁、停止信号量和中断任务
   * @return 所有同步资源和中断任务创建成功时返回 true
   */
  bool CreateResources();

  /**
   * @brief 删除当前对象持有的同步资源
   */
  void DestroyResources();

  /**
   * @brief 通知中断任务退出并等待任务完成
   * @return 任务正常停止或当前没有中断任务时返回 true
   */
  bool StopInterruptTask();

  static std::atomic<RfalPlatformContext*> active_context_;
  static std::atomic<TaskHandle_t> interrupt_task_for_isr_;

  Transport transport_;
  std::shared_ptr<cpp_bus_driver::Tool> tool_;
  std::shared_ptr<cpp_bus_driver::BusI2cGuide> i2c_bus_;
  std::shared_ptr<cpp_bus_driver::BusSpiGuide> spi_bus_;
  int32_t interrupt_pin_;
  uint16_t address_;
  int32_t chip_select_pin_;
  std::atomic<PlatformError> error_{PlatformError::kNone};
  std::atomic<RfalPlatformInterruptCallback> interrupt_callback_{nullptr};
  std::atomic<bool> stopping_{false};
  std::array<uint8_t, ST25R3916_FIFO_DEPTH + 1U> i2c_transmit_buffer_ = {};
  uint16_t i2c_transmit_length_ = 0;
  std::array<uint8_t, ST25R3916_FIFO_DEPTH + 1U> spi_receive_buffer_ = {};
  SemaphoreHandle_t communication_mutex_ = nullptr;
  SemaphoreHandle_t worker_mutex_ = nullptr;
  SemaphoreHandle_t task_stopped_ = nullptr;
  TaskHandle_t interrupt_task_ = nullptr;
  portMUX_TYPE irq_status_lock_ = portMUX_INITIALIZER_UNLOCKED;
  bool bus_initialized_ = false;
  bool interrupt_initialized_ = false;
  bool i2c_started_ = false;
  bool i2c_repeat_started_ = false;
  bool i2c_address_valid_ = false;
  bool spi_selected_ = false;
  bool bound_ = false;
};

}  // namespace stsw_st25rfal002_cpp_bus_driver
