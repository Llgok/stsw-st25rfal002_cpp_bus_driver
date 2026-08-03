/*
 * @Description: STSW-ST25RFAL002 的 ESP-IDF 平台实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:40:45
 * @LastEditTime: 2026-08-03 16:16:10
 * @License: GPL 3.0
 */
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

#include "platform/rfal_platform_internal.h"

namespace stsw_st25rfal002_cpp_bus_driver {

std::atomic<RfalPlatformContext*> RfalPlatformContext::active_context_ =
    nullptr;
std::atomic<TaskHandle_t> RfalPlatformContext::interrupt_task_for_isr_ =
    nullptr;

RfalPlatformContext::RfalPlatformContext(
    std::shared_ptr<cpp_bus_driver::BusI2cGuide> bus, int32_t interrupt_pin,
    uint16_t address)
    : transport_(Transport::kI2c),
      tool_(bus),
      i2c_bus_(std::move(bus)),
      interrupt_pin_(interrupt_pin),
      address_(address),
      chip_select_pin_(-1) {}

RfalPlatformContext::RfalPlatformContext(
    std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus, int32_t interrupt_pin,
    int32_t chip_select_pin)
    : transport_(Transport::kSpi),
      tool_(bus),
      spi_bus_(std::move(bus)),
      interrupt_pin_(interrupt_pin),
      address_(0),
      chip_select_pin_(chip_select_pin) {}

RfalPlatformContext::~RfalPlatformContext() {
  Deinit(false);
  Unbind();
}

bool RfalPlatformContext::Bind() {
  if (bound_) {
    return true;
  }

  RfalPlatformContext* expected = nullptr;
  if (!active_context_.compare_exchange_strong(expected, this)) {
    SetError(
        PlatformError::kAlreadyInUse, "ST25RFAL platform is already in use");
    return false;
  }
  bound_ = true;
  return true;
}

void RfalPlatformContext::Unbind() {
  if (!bound_) {
    return;
  }

  RfalPlatformContext* expected = this;
  active_context_.compare_exchange_strong(expected, nullptr);
  bound_ = false;
}

bool RfalPlatformContext::Init(uint32_t frequency_hz) {
  ClearError();
  if (bus_initialized_) {
    SetError(transport_ == Transport::kI2c ? PlatformError::kI2cFailed
                                           : PlatformError::kSpiFailed,
        "Previous ST25R3916 platform cleanup is incomplete");
    return false;
  }
  const bool valid_i2c = transport_ == Transport::kI2c && i2c_bus_ != nullptr &&
                         address_ <= 0x7F && frequency_hz > 0 &&
                         frequency_hz <= kMaximumI2cFrequencyHz;
  const bool valid_spi = transport_ == Transport::kSpi && spi_bus_ != nullptr &&
                         chip_select_pin_ >= 0 && frequency_hz > 0 &&
                         frequency_hz <= kMaximumSpiFrequencyHz;
  if (tool_ == nullptr || interrupt_pin_ < 0 || (!valid_i2c && !valid_spi)) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 platform configuration");
    return false;
  }
  if (!bound_ && !Bind()) {
    return false;
  }
  if (transport_ == Transport::kI2c &&
      !i2c_bus_->Init(frequency_hz, address_)) {
    SetError(PlatformError::kI2cFailed,
        "Failed to initialize the ST25R3916 I2C device");
    return false;
  }
  if (transport_ == Transport::kSpi &&
      !spi_bus_->Init(static_cast<int32_t>(frequency_hz), chip_select_pin_)) {
    SetError(PlatformError::kSpiFailed,
        "Failed to initialize the ST25R3916 SPI device");
    return false;
  }
  bus_initialized_ = true;

  if (!CreateResources()) {
    const bool bus_deinitialized = transport_ == Transport::kI2c
                                       ? i2c_bus_->Deinit(false)
                                       : spi_bus_->Deinit(false);
    if (bus_deinitialized) {
      bus_initialized_ = false;
    } else if (transport_ == Transport::kI2c) {
      SetError(PlatformError::kI2cFailed,
          "Failed to roll back the ST25R3916 I2C device");
    } else {
      SetError(PlatformError::kSpiFailed,
          "Failed to roll back the ST25R3916 SPI device");
    }
    return false;
  }
  return true;
}

bool RfalPlatformContext::Deinit(bool delete_bus) {
  bool result = true;
  interrupt_callback_.store(nullptr);
  interrupt_task_for_isr_.store(nullptr, std::memory_order_release);

  if (interrupt_initialized_) {
    const bool interrupt_deinitialized =
        tool_->DeinitGpioInterrupt(static_cast<uint32_t>(interrupt_pin_));
    // Tool::DeinitGpioInterrupt() 会在单步失败后继续执行其余清理，因此一次
    // 调用后不再重复移除同一个 ISR；首次失败仍通过返回值报告给上层。
    interrupt_initialized_ = false;
    if (!interrupt_deinitialized) {
      SetError(PlatformError::kInterruptFailed,
          "Failed to deinitialize the ST25R3916 interrupt");
      result = false;
    }
  }

  if (!StopInterruptTask()) {
    result = false;
  }
  DestroyResources();

  if (bus_initialized_) {
    const bool deinitialized = transport_ == Transport::kI2c
                                   ? i2c_bus_->Deinit(delete_bus)
                                   : spi_bus_->Deinit(delete_bus);
    if (!deinitialized) {
      if (transport_ == Transport::kI2c) {
        SetError(PlatformError::kI2cFailed,
            "Failed to deinitialize the ST25R3916 I2C device");
      } else {
        SetError(PlatformError::kSpiFailed,
            "Failed to deinitialize the ST25R3916 SPI device");
      }
      result = false;
    } else {
      bus_initialized_ = false;
    }
  }
  i2c_started_ = false;
  i2c_repeat_started_ = false;
  i2c_address_valid_ = false;
  i2c_transmit_length_ = 0;
  spi_selected_ = false;
  return result;
}

RfalPlatformContext::Transport RfalPlatformContext::transport() const {
  return transport_;
}

PlatformError RfalPlatformContext::error() const { return error_.load(); }

bool RfalPlatformContext::healthy() const {
  return error_.load() == PlatformError::kNone;
}

void RfalPlatformContext::ClearError() { error_.store(PlatformError::kNone); }

void RfalPlatformContext::ProtectCommunication() {
  if (communication_mutex_ != nullptr) {
    xSemaphoreTake(communication_mutex_, portMAX_DELAY);
  }
}

void RfalPlatformContext::UnprotectCommunication() {
  if (communication_mutex_ != nullptr) {
    xSemaphoreGive(communication_mutex_);
  }
}

void RfalPlatformContext::ProtectIrqStatus() {
  portENTER_CRITICAL(&irq_status_lock_);
}

void RfalPlatformContext::UnprotectIrqStatus() {
  portEXIT_CRITICAL(&irq_status_lock_);
}

void RfalPlatformContext::ProtectWorker() {
  if (worker_mutex_ != nullptr) {
    xSemaphoreTake(worker_mutex_, portMAX_DELAY);
  }
}

void RfalPlatformContext::UnprotectWorker() {
  if (worker_mutex_ != nullptr) {
    xSemaphoreGive(worker_mutex_);
  }
}

void RfalPlatformContext::InitializeInterruptPin() {
  if (tool_ == nullptr || !tool_->SetGpioMode(interrupt_pin_,
                              cpp_bus_driver::Tool::GpioMode::kInput,
                              cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    SetError(PlatformError::kGpioFailed,
        "Failed to configure the ST25R3916 interrupt GPIO");
  }
}

void RfalPlatformContext::SetInterruptCallback(
    RfalPlatformInterruptCallback callback) {
  interrupt_callback_.store(callback);
  if (callback == nullptr || interrupt_initialized_ || tool_ == nullptr) {
    return;
  }

  if (!tool_->InitGpioInterrupt(static_cast<uint32_t>(interrupt_pin_),
          cpp_bus_driver::Tool::InterruptMode::kRising, InterruptHandler,
          nullptr)) {
    SetError(PlatformError::kInterruptFailed,
        "Failed to install the ST25R3916 interrupt handler");
    return;
  }
  interrupt_initialized_ = true;

  // 安装边沿触发处理函数时，如果中断已经拉高，则主动唤醒中断任务。
  if (InterruptPinIsHigh() && interrupt_task_ != nullptr) {
    xTaskNotifyGive(interrupt_task_);
  }
}

bool RfalPlatformContext::InterruptPinIsHigh() const {
  // 官方中断处理会在 INT 保持高电平时持续读取中断寄存器。总线或平台已经
  // 失败时停止继续轮询，避免无法清除 INT 导致最高优先级中断任务持续循环。
  return healthy() && tool_ != nullptr && tool_->GpioRead(interrupt_pin_);
}

void RfalPlatformContext::I2cStart() {
  i2c_started_ = true;
  i2c_repeat_started_ = false;
  i2c_address_valid_ = false;
  i2c_transmit_length_ = 0;
}

void RfalPlatformContext::I2cStop() {
  i2c_started_ = false;
  i2c_repeat_started_ = false;
  i2c_address_valid_ = false;
  i2c_transmit_length_ = 0;
}

void RfalPlatformContext::I2cRepeatStart() {
  if (!i2c_started_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 I2C repeated start");
    return;
  }
  i2c_repeat_started_ = true;
  i2c_address_valid_ = false;
}

void RfalPlatformContext::I2cSlaveAddressWrite(uint8_t address) {
  // cpp_bus_driver binds the target address when the I2C device is initialized.
  // The upstream address is informational; use the constructor address so
  // hardware with a non-default strapped address is also supported.
  static_cast<void>(address);
  i2c_address_valid_ = i2c_started_;
  if (!i2c_address_valid_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 I2C write address");
  }
}

void RfalPlatformContext::I2cSlaveAddressRead(uint8_t address) {
  static_cast<void>(address);
  i2c_address_valid_ = i2c_started_ && i2c_repeat_started_;
  if (!i2c_address_valid_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 I2C read address");
  }
}

void RfalPlatformContext::I2cTransmit(
    const uint8_t* buffer, uint16_t length, bool last, bool transmit_only) {
  if (length == 0) {
    return;
  }
  if (!healthy()) {
    return;
  }
  if (!bus_initialized_ || i2c_bus_ == nullptr || buffer == nullptr ||
      !i2c_started_ || !i2c_address_valid_ || i2c_repeat_started_ ||
      i2c_transmit_length_ + length > i2c_transmit_buffer_.size()) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 I2C transmit transaction");
    return;
  }

  std::memcpy(
      i2c_transmit_buffer_.data() + i2c_transmit_length_, buffer, length);
  i2c_transmit_length_ += length;
  if (last && transmit_only &&
      !i2c_bus_->Write(i2c_transmit_buffer_.data(), i2c_transmit_length_)) {
    SetError(PlatformError::kI2cFailed, "ST25R3916 I2C write failed");
  }
}

void RfalPlatformContext::I2cReceive(uint8_t* buffer, uint16_t length) {
  if (length == 0) {
    return;
  }
  if (!healthy()) {
    return;
  }
  if (!bus_initialized_ || i2c_bus_ == nullptr || buffer == nullptr ||
      !i2c_started_ || !i2c_repeat_started_ || !i2c_address_valid_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 I2C receive transaction");
    return;
  }

  const bool result = i2c_transmit_length_ == 0
                          ? i2c_bus_->Read(buffer, length)
                          : i2c_bus_->WriteRead(i2c_transmit_buffer_.data(),
                                i2c_transmit_length_, buffer, length);
  if (!result) {
    SetError(PlatformError::kI2cFailed, "ST25R3916 I2C read failed");
  }
}

void RfalPlatformContext::SpiSelect() { spi_selected_ = true; }

void RfalPlatformContext::SpiDeselect() { spi_selected_ = false; }

void RfalPlatformContext::SpiTransceive(
    const uint8_t* transmit_buffer, uint8_t* receive_buffer, uint16_t length) {
  if (length == 0) {
    return;
  }
  if (!healthy()) {
    return;
  }
  if (!bus_initialized_ || spi_bus_ == nullptr || transmit_buffer == nullptr ||
      !spi_selected_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 SPI transaction");
    return;
  }

  bool result = false;
  if (receive_buffer == nullptr) {
    result = spi_bus_->Write(transmit_buffer, length);
  } else if (receive_buffer == transmit_buffer) {
    if (length > spi_receive_buffer_.size()) {
      SetError(PlatformError::kInvalidConfiguration,
          "ST25R3916 SPI transaction is too large");
      return;
    }
    result = spi_bus_->WriteRead(
        transmit_buffer, spi_receive_buffer_.data(), length);
    if (result) {
      std::memcpy(receive_buffer, spi_receive_buffer_.data(), length);
    }
  } else {
    result = spi_bus_->WriteRead(transmit_buffer, receive_buffer, length);
  }
  if (!result) {
    SetError(PlatformError::kSpiFailed, "ST25R3916 SPI transaction failed");
  }
}

uint32_t RfalPlatformContext::GetSystemTick() const {
  if (tool_ == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(tool_->GetSystemTimeMs());
}

void RfalPlatformContext::Delay(uint32_t delay_ms) const {
  if (tool_ != nullptr) {
    tool_->DelayMs(delay_ms);
  }
}

void RfalPlatformContext::LogV(const char* format, va_list arguments) const {
  if (tool_ == nullptr || format == nullptr) {
    return;
  }

  std::array<char, 512> message = {};
  std::vsnprintf(message.data(), message.size(), format, arguments);
  tool_->LogMessage(cpp_bus_driver::Tool::LogLevel::kInfo, __FILE__, __LINE__,
      "%s", message.data());
}

void RfalPlatformContext::SetError(PlatformError error, const char* message) {
  error_.store(error);
  if (tool_ != nullptr && message != nullptr) {
    tool_->LogMessage(cpp_bus_driver::Tool::LogLevel::kError, __FILE__,
        __LINE__, "%s\n", message);
  }
}

RfalPlatformContext* RfalPlatformContext::active() {
  return active_context_.load();
}

void IRAM_ATTR RfalPlatformContext::InterruptHandler(void* argument) {
  static_cast<void>(argument);
  TaskHandle_t task = interrupt_task_for_isr_.load(std::memory_order_acquire);
  if (task == nullptr) {
    return;
  }

  BaseType_t higher_priority_task_woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &higher_priority_task_woken);
  if (higher_priority_task_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

void RfalPlatformContext::InterruptTaskEntry(void* argument) {
  auto* context = static_cast<RfalPlatformContext*>(argument);
  if (context != nullptr) {
    context->InterruptTask();
  }
  vTaskDelete(nullptr);
}

void RfalPlatformContext::InterruptTask() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (stopping_.load()) {
      break;
    }

    const RfalPlatformInterruptCallback callback = interrupt_callback_.load();
    if (callback != nullptr) {
      callback();
    }
  }

  if (task_stopped_ != nullptr) {
    xSemaphoreGive(task_stopped_);
  }
  // 停止信号发出后保留有效任务句柄，由释放方统一删除任务。
  vTaskSuspend(nullptr);
}

bool RfalPlatformContext::CreateResources() {
  communication_mutex_ = xSemaphoreCreateMutex();
  worker_mutex_ = xSemaphoreCreateMutex();
  task_stopped_ = xSemaphoreCreateBinary();
  if (communication_mutex_ == nullptr || worker_mutex_ == nullptr ||
      task_stopped_ == nullptr) {
    SetError(PlatformError::kResourceAllocationFailed,
        "Failed to allocate ST25RFAL synchronization resources");
    DestroyResources();
    return false;
  }

  stopping_.store(false);
  if (xTaskCreate(InterruptTaskEntry, "st25r_irq", kInterruptTaskStackSize,
          this, kInterruptTaskPriority, &interrupt_task_) != pdPASS) {
    interrupt_task_ = nullptr;
    interrupt_task_for_isr_.store(nullptr, std::memory_order_release);
    SetError(PlatformError::kResourceAllocationFailed,
        "Failed to create the ST25R3916 interrupt task");
    DestroyResources();
    return false;
  }
  interrupt_task_for_isr_.store(interrupt_task_, std::memory_order_release);
  return true;
}

void RfalPlatformContext::DestroyResources() {
  if (communication_mutex_ != nullptr) {
    vSemaphoreDelete(communication_mutex_);
    communication_mutex_ = nullptr;
  }
  if (worker_mutex_ != nullptr) {
    vSemaphoreDelete(worker_mutex_);
    worker_mutex_ = nullptr;
  }
  if (task_stopped_ != nullptr) {
    vSemaphoreDelete(task_stopped_);
    task_stopped_ = nullptr;
  }
}

bool RfalPlatformContext::StopInterruptTask() {
  interrupt_task_for_isr_.store(nullptr, std::memory_order_release);
  TaskHandle_t task = interrupt_task_;
  if (task == nullptr) {
    return true;
  }

  stopping_.store(true);
  xTaskNotifyGive(task);
  const bool stopped =
      task_stopped_ != nullptr &&
      xSemaphoreTake(task_stopped_, kTaskStopTimeout) == pdTRUE;
  if (!stopped) {
    SetError(PlatformError::kInterruptFailed,
        "Timed out while stopping the ST25R3916 interrupt task");
  }
  vTaskDelete(task);
  interrupt_task_ = nullptr;
  return stopped;
}

}  // namespace stsw_st25rfal002_cpp_bus_driver

namespace {

using stsw_st25rfal002_cpp_bus_driver::PlatformError;
using stsw_st25rfal002_cpp_bus_driver::RfalPlatformContext;

RfalPlatformContext* ActivePlatform() { return RfalPlatformContext::active(); }

}  // namespace

extern "C" void rfalPlatformProtectCommunication(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->ProtectCommunication();
  }
}

extern "C" void rfalPlatformUnprotectCommunication(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->UnprotectCommunication();
  }
}

extern "C" void rfalPlatformProtectIrqStatus(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->ProtectIrqStatus();
  }
}

extern "C" void rfalPlatformUnprotectIrqStatus(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->UnprotectIrqStatus();
  }
}

extern "C" void rfalPlatformProtectWorker(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->ProtectWorker();
  }
}

extern "C" void rfalPlatformUnprotectWorker(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->UnprotectWorker();
  }
}

extern "C" void rfalPlatformInterruptPinInitialize(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->InitializeInterruptPin();
  }
}

extern "C" void rfalPlatformInterruptSetCallback(
    RfalPlatformInterruptCallback callback) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SetInterruptCallback(callback);
  }
}

extern "C" bool rfalPlatformGpioIsHigh(void) {
  auto* platform = ActivePlatform();
  return platform != nullptr && platform->InterruptPinIsHigh();
}

extern "C" bool rfalPlatformIsHealthy(void) {
  auto* platform = ActivePlatform();
  return platform != nullptr && platform->healthy();
}

extern "C" bool rfalPlatformUsesI2c(void) {
  auto* platform = ActivePlatform();
  return platform != nullptr &&
         platform->transport() == RfalPlatformContext::Transport::kI2c;
}

extern "C" void rfalPlatformI2cStart(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cStart();
  }
}

extern "C" void rfalPlatformI2cStop(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cStop();
  }
}

extern "C" void rfalPlatformI2cRepeatStart(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cRepeatStart();
  }
}

extern "C" void rfalPlatformI2cSlaveAddressWrite(uint8_t address) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cSlaveAddressWrite(address);
  }
}

extern "C" void rfalPlatformI2cSlaveAddressRead(uint8_t address) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cSlaveAddressRead(address);
  }
}

extern "C" void rfalPlatformI2cTransmit(
    const uint8_t* buffer, uint16_t length, bool last, bool transmit_only) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cTransmit(buffer, length, last, transmit_only);
  }
}

extern "C" void rfalPlatformI2cReceive(uint8_t* buffer, uint16_t length) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->I2cReceive(buffer, length);
  }
}

extern "C" void rfalPlatformSpiSelect(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SpiSelect();
  }
}

extern "C" void rfalPlatformSpiDeselect(void) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SpiDeselect();
  }
}

extern "C" void rfalPlatformSpiTransceive(
    const uint8_t* transmit_buffer, uint8_t* receive_buffer, uint16_t length) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SpiTransceive(transmit_buffer, receive_buffer, length);
  }
}

extern "C" uint32_t rfalPlatformGetSystemTick(void) {
  auto* platform = ActivePlatform();
  return platform == nullptr ? 0 : platform->GetSystemTick();
}

extern "C" uint32_t rfalPlatformTimerCreate(uint16_t timeout_ms) {
  return rfalPlatformGetSystemTick() + timeout_ms;
}

extern "C" bool rfalPlatformTimerIsExpired(uint32_t timer) {
  return static_cast<int32_t>(rfalPlatformGetSystemTick() - timer) >= 0;
}

extern "C" uint32_t rfalPlatformTimerGetRemaining(uint32_t timer) {
  const uint32_t now = rfalPlatformGetSystemTick();
  return static_cast<int32_t>(now - timer) >= 0 ? 0 : timer - now;
}

extern "C" void rfalPlatformDelay(uint32_t delay_ms) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->Delay(delay_ms);
  }
}

extern "C" void rfalPlatformLog(const char* format, ...) {
  auto* platform = ActivePlatform();
  if (platform == nullptr) {
    return;
  }

  va_list arguments;
  va_start(arguments, format);
  platform->LogV(format, arguments);
  va_end(arguments);
}

extern "C" void rfalPlatformAssertFailed(
    const char* expression, const char* file, int line) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SetError(
        PlatformError::kAssertionFailed, "ST25RFAL assertion failed");
    rfalPlatformLog("Assertion '%s' failed at %s:%d\n", expression, file, line);
  }
}

extern "C" void rfalPlatformErrorHandle(const char* file, int line) {
  if (auto* platform = ActivePlatform(); platform != nullptr) {
    platform->SetError(PlatformError::kOfficialErrorHandler,
        "ST25RFAL official error handler was invoked");
    rfalPlatformLog("Official error handler invoked at %s:%d\n", file, line);
  }
}
