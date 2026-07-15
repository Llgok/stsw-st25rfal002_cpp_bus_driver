/*
 * @Description: STSW-ST25RFAL002 的 ESP-IDF 平台实现
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:40:45
 * @LastEditTime: 2026-07-15 09:50:25
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
    std::shared_ptr<cpp_bus_driver::BusSpiGuide> bus, int32_t interrupt_pin,
    int32_t chip_select_pin)
    : bus_(std::move(bus)),
      interrupt_pin_(interrupt_pin),
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

bool RfalPlatformContext::Init(int32_t frequency_hz) {
  ClearError();
  if (bus_initialized_) {
    return true;
  }
  if (bus_ == nullptr || interrupt_pin_ < 0 || chip_select_pin_ < 0 ||
      frequency_hz <= 0 || frequency_hz > kMaximumSpiFrequencyHz) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 platform configuration");
    return false;
  }
  if (!bound_ && !Bind()) {
    return false;
  }
  if (!bus_->Init(frequency_hz, chip_select_pin_)) {
    SetError(PlatformError::kSpiFailed,
        "Failed to initialize the ST25R3916 SPI device");
    return false;
  }
  bus_initialized_ = true;

  if (!CreateResources()) {
    bus_->Deinit(false);
    bus_initialized_ = false;
    return false;
  }
  return true;
}

bool RfalPlatformContext::Deinit(bool delete_bus) {
  bool result = true;
  interrupt_callback_.store(nullptr);
  interrupt_task_for_isr_.store(nullptr, std::memory_order_release);

  if (interrupt_initialized_) {
    if (!bus_->DeinitGpioInterrupt(static_cast<uint32_t>(interrupt_pin_))) {
      SetError(PlatformError::kInterruptFailed,
          "Failed to deinitialize the ST25R3916 interrupt");
      result = false;
    }
    interrupt_initialized_ = false;
  }

  StopInterruptTask();
  DestroyResources();

  if (bus_initialized_) {
    if (!bus_->Deinit(delete_bus)) {
      SetError(PlatformError::kSpiFailed,
          "Failed to deinitialize the ST25R3916 SPI device");
      result = false;
    }
    bus_initialized_ = false;
  }
  spi_selected_ = false;
  return result;
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
  if (bus_ == nullptr ||
      !bus_->SetGpioMode(interrupt_pin_, cpp_bus_driver::Tool::GpioMode::kInput,
          cpp_bus_driver::Tool::GpioStatus::kPulldown)) {
    SetError(PlatformError::kGpioFailed,
        "Failed to configure the ST25R3916 interrupt GPIO");
  }
}

void RfalPlatformContext::SetInterruptCallback(
    RfalPlatformInterruptCallback callback) {
  interrupt_callback_.store(callback);
  if (callback == nullptr || interrupt_initialized_ || bus_ == nullptr) {
    return;
  }

  if (!bus_->InitGpioInterrupt(static_cast<uint32_t>(interrupt_pin_),
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
  return bus_ != nullptr && bus_->GpioRead(interrupt_pin_);
}

void RfalPlatformContext::SpiSelect() { spi_selected_ = true; }

void RfalPlatformContext::SpiDeselect() { spi_selected_ = false; }

void RfalPlatformContext::SpiTransceive(
    const uint8_t* transmit_buffer, uint8_t* receive_buffer, uint16_t length) {
  if (length == 0) {
    return;
  }
  if (!bus_initialized_ || bus_ == nullptr || transmit_buffer == nullptr ||
      !spi_selected_) {
    SetError(PlatformError::kInvalidConfiguration,
        "Invalid ST25R3916 SPI transaction");
    return;
  }

  bool result = false;
  if (receive_buffer == nullptr) {
    result = bus_->Write(transmit_buffer, length);
  } else if (receive_buffer == transmit_buffer) {
    if (length > spi_receive_buffer_.size()) {
      SetError(PlatformError::kInvalidConfiguration,
          "ST25R3916 SPI transaction is too large");
      return;
    }
    result =
        bus_->WriteRead(transmit_buffer, spi_receive_buffer_.data(), length);
    if (result) {
      std::memcpy(receive_buffer, spi_receive_buffer_.data(), length);
    }
  } else {
    result = bus_->WriteRead(transmit_buffer, receive_buffer, length);
  }
  if (!result) {
    SetError(PlatformError::kSpiFailed, "ST25R3916 SPI transaction failed");
  }
}

uint32_t RfalPlatformContext::GetSystemTick() const {
  if (bus_ == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(bus_->GetSystemTimeMs());
}

void RfalPlatformContext::Delay(uint32_t delay_ms) const {
  if (bus_ != nullptr) {
    bus_->DelayMs(delay_ms);
  }
}

void RfalPlatformContext::LogV(const char* format, va_list arguments) const {
  if (bus_ == nullptr || format == nullptr) {
    return;
  }

  std::array<char, 512> message = {};
  std::vsnprintf(message.data(), message.size(), format, arguments);
  bus_->LogMessage(cpp_bus_driver::Tool::LogLevel::kInfo, __FILE__, __LINE__,
      "%s", message.data());
}

void RfalPlatformContext::SetError(PlatformError error, const char* message) {
  error_.store(error);
  if (bus_ != nullptr && message != nullptr) {
    bus_->LogMessage(cpp_bus_driver::Tool::LogLevel::kError, __FILE__, __LINE__,
        "%s\n", message);
  }
}

RfalPlatformContext* RfalPlatformContext::active() {
  return active_context_.load();
}

void IRAM_ATTR RfalPlatformContext::InterruptHandler(void* argument) {
  static_cast<void>(argument);
  TaskHandle_t task =
      interrupt_task_for_isr_.load(std::memory_order_acquire);
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

void RfalPlatformContext::StopInterruptTask() {
  interrupt_task_for_isr_.store(nullptr, std::memory_order_release);
  TaskHandle_t task = interrupt_task_;
  if (task == nullptr) {
    return;
  }

  stopping_.store(true);
  xTaskNotifyGive(task);
  const bool stopped = task_stopped_ != nullptr &&
                       xSemaphoreTake(task_stopped_, kTaskStopTimeout) == pdTRUE;
  if (!stopped) {
    SetError(PlatformError::kInterruptFailed,
        "Timed out while stopping the ST25R3916 interrupt task");
  }
  vTaskDelete(task);
  interrupt_task_ = nullptr;
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
