/*
 * @Description: STSW-ST25RFAL002 ESP-IDF 平台适配接口
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:38:14
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

// 将组件 Kconfig 选项映射为官方头文件使用的芯片型号宏。
#if defined(CONFIG_STSW_ST25RFAL002_CHIP_ST25R3916) && \
    CONFIG_STSW_ST25RFAL002_CHIP_ST25R3916
#ifndef ST25R3916
#define ST25R3916
#endif
#elif defined(CONFIG_STSW_ST25RFAL002_CHIP_ST25R3916B) && \
    CONFIG_STSW_ST25RFAL002_CHIP_ST25R3916B
#ifndef ST25R3916B
#define ST25R3916B
#endif
#else
#error "No ST25R device selected"
#endif

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_I2C)
#define RFAL_USE_I2C
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 官方 RFAL 中断处理函数类型
 *
 * 该回调由 ST25R3916 官方中断模块注册，平台层只负责在任务上下文调用。
 */
typedef void (*RfalPlatformInterruptCallback)(void);

// 以下函数是 STSW-ST25RFAL002 官方 C 源码需要的平台钩子。
// 应用代码不应直接调用，应通过 St25r3916 和官方 RFAL API 使用驱动。

// 通信与并发保护。

/**
 * @brief 保护一次完整的 ST25R3916 SPI 通信事务
 *
 * 官方通信层在拉低片选前调用 Protect，在释放片选后调用 Unprotect，保证
 * 寄存器、FIFO 和命令传输不会被其他任务交叉执行。这两个函数必须成对调用，
 * 保护区内允许执行阻塞式 SPI 操作。
 */
void rfalPlatformProtectCommunication(void);

/**
 * @brief 结束 ST25R3916 SPI 通信事务保护并释放通信互斥锁
 */
void rfalPlatformUnprotectCommunication(void);

/**
 * @brief 保护官方中断模块保存的待处理中断状态
 *
 * 该临界区只保护中断掩码和状态变量，不负责 GPIO 中断，也不能在保护区内执行
 * SPI 或其他阻塞操作。Protect 和 Unprotect 必须成对调用。
 */
void rfalPlatformProtectIrqStatus(void);

/**
 * @brief 退出官方中断状态临界区
 */
void rfalPlatformUnprotectIrqStatus(void);

/**
 * @brief 防止多个任务同时执行 RFAL Worker 状态机
 *
 * RFAL 协议状态保存在官方全局变量中，因此同一时刻只能有一个任务推进状态机。
 * Protect 和 Unprotect 必须成对调用。
 */
void rfalPlatformProtectWorker(void);

/**
 * @brief 结束 RFAL Worker 状态机保护并释放互斥锁
 */
void rfalPlatformUnprotectWorker(void);

// 中断引脚与延迟处理中断。

/**
 * @brief 将 ST25R3916 INT 引脚配置为下拉输入
 *
 * ST25R3916 INT 为高电平有效。该函数只配置 GPIO，不安装回调。
 */
void rfalPlatformInterruptPinInitialize(void);

/**
 * @brief 保存官方中断回调并安装 GPIO 上升沿中断
 *
 * GPIO ISR 只唤醒专用 FreeRTOS 任务，任务再调用 callback，避免在中断上下文中
 * 执行官方 SPI 访问。传入 nullptr 只清除回调，不重复安装中断。
 *
 * @param callback 官方 ST25R3916 中断处理函数
 */
void rfalPlatformInterruptSetCallback(RfalPlatformInterruptCallback callback);

/**
 * @brief 读取高电平有效的 ST25R3916 INT 引脚
 * @return INT 当前为高电平时返回 true
 */
bool rfalPlatformGpioIsHigh(void);

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_I2C)
// I2C 事务适配。官方通信层的分段调用会在平台层合并为一次原子传输。
void rfalPlatformI2cStart(void);
void rfalPlatformI2cStop(void);
void rfalPlatformI2cRepeatStart(void);
void rfalPlatformI2cSlaveAddressWrite(uint8_t address);
void rfalPlatformI2cSlaveAddressRead(uint8_t address);
void rfalPlatformI2cTransmit(
    const uint8_t* buffer, uint16_t length, bool last, bool transmit_only);
void rfalPlatformI2cReceive(uint8_t* buffer, uint16_t length);
#else
// SPI 事务适配。

/**
 * @brief 标记官方 ST25R3916 SPI 事务的开始和结束
 *
 * Select 和 Deselect 不直接操作 GPIO 片选。cpp_bus_driver 在实际 Write 或
 * WriteRead 调用期间由硬件自动控制片选，这两个函数用于校验官方调用顺序。
 */
void rfalPlatformSpiSelect(void);

/**
 * @brief 标记官方 ST25R3916 SPI 事务结束
 */
void rfalPlatformSpiDeselect(void);

/**
 * @brief 通过 cpp_bus_driver 执行一次完整的 SPI 传输
 *
 * receive_buffer 为空时执行纯发送；不为空时执行全双工传输。官方读取流程可能
 * 让发送和接收指针指向同一缓冲区，平台层会使用独立接收缓冲区避免 DMA 覆盖。
 *
 * @param transmit_buffer 发送缓冲区，不能为空
 * @param receive_buffer 接收缓冲区，为空时仅发送
 * @param length 传输字节数
 */
void rfalPlatformSpiTransceive(
    const uint8_t* transmit_buffer, uint8_t* receive_buffer, uint16_t length);
#endif

// 系统时钟、软件计时器和延时。

/**
 * @brief 获取 RFAL 使用的 1 ms 系统时基
 * @return 当前系统运行时间，单位为 ms，允许 uint32_t 自然回绕
 */
uint32_t rfalPlatformGetSystemTick(void);

/**
 * @brief 创建无资源占用的软件截止时间
 *
 * 返回值是绝对截止时间，不会创建 FreeRTOS Timer，因此无需显式释放资源。
 * TimerIsExpired 和 TimerGetRemaining 使用回绕安全的差值判断。
 *
 * @param timeout_ms 超时时间，单位为 ms
 * @return 计时器截止时间
 */
uint32_t rfalPlatformTimerCreate(uint16_t timeout_ms);

/**
 * @brief 查询软件截止时间是否已经到达
 * @param timer 计时器截止时间
 * @return 已超时时返回 true
 */
bool rfalPlatformTimerIsExpired(uint32_t timer);

/**
 * @brief 获取软件截止时间的剩余毫秒数
 * @param timer 计时器截止时间
 * @return 剩余时间，单位为 ms
 */
uint32_t rfalPlatformTimerGetRemaining(uint32_t timer);

/**
 * @brief 阻塞当前任务指定时间
 * @param delay_ms 延时时间，单位为 ms
 */
void rfalPlatformDelay(uint32_t delay_ms);

// 日志和错误上报。

/**
 * @brief 将官方 RFAL 日志转发到 cpp_bus_driver 日志接口
 * @param format printf 风格的格式字符串
 */
void rfalPlatformLog(const char* format, ...);

/**
 * @brief 记录官方 RFAL 断言失败
 *
 * 该函数不会在内部重启或终止任务，而是保存 PlatformError 并输出日志。
 * 上层可通过 St25r3916::platform_error() 查询具体的平台错误。
 *
 * @param expression 断言表达式
 * @param file 断言所在源文件
 * @param line 断言所在行号
 */
void rfalPlatformAssertFailed(
    const char* expression, const char* file, int line);

/**
 * @brief 记录官方 RFAL 全局错误处理器调用
 *
 * 该函数与断言处理相同，只记录平台错误和源码位置，不终止当前任务。
 *
 * @param file 错误所在源文件
 * @param line 错误所在行号
 */
void rfalPlatformErrorHandle(const char* file, int line);

#ifdef __cplusplus
}  // C 语言链接
#endif

// ST25R3916 INT 为高电平有效，实际 GPIO 由平台上下文管理。
// 官方移植接口要求的端口和引脚占位值不会参与 GPIO 操作。
#define ST25R_INT_PORT 0U
#define ST25R_INT_PIN 0U

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_SPI)
// 将每条 ST25R3916 命令合并到一次由硬件控制片选的 SPI 事务中。
#ifndef ST25R_COM_SINGLETXRX
#define ST25R_COM_SINGLETXRX
#endif
#endif

#define platformProtectST25RComm() rfalPlatformProtectCommunication()
#define platformUnprotectST25RComm() rfalPlatformUnprotectCommunication()
#define platformProtectST25RIrqStatus() rfalPlatformProtectIrqStatus()
#define platformUnprotectST25RIrqStatus() rfalPlatformUnprotectIrqStatus()
#define platformProtectWorker() rfalPlatformProtectWorker()
#define platformUnprotectWorker() rfalPlatformUnprotectWorker()

#define platformIrqST25RPinInitialize() rfalPlatformInterruptPinInitialize()
#define platformIrqST25RSetCallback(callback) \
  rfalPlatformInterruptSetCallback(callback)
#define platformGpioIsHigh(port, pin) rfalPlatformGpioIsHigh()

#if defined(CONFIG_STSW_ST25RFAL002_TRANSPORT_I2C)
#define platformI2CStart() rfalPlatformI2cStart()
#define platformI2CStop() rfalPlatformI2cStop()
#define platformI2CRepeatStart() rfalPlatformI2cRepeatStart()
#define platformI2CSlaveAddrWR(address) \
  rfalPlatformI2cSlaveAddressWrite(address)
#define platformI2CSlaveAddrRD(address) rfalPlatformI2cSlaveAddressRead(address)
#define platformI2CTx(buffer, length, last, transmit_only) \
  rfalPlatformI2cTransmit((buffer), (length), (last), (transmit_only))
#define platformI2CRx(buffer, length) rfalPlatformI2cReceive((buffer), (length))
#else
#define platformSpiSelect() rfalPlatformSpiSelect()
#define platformSpiDeselect() rfalPlatformSpiDeselect()
#define platformSpiTxRx(tx_buffer, rx_buffer, length) \
  rfalPlatformSpiTransceive((tx_buffer), (rx_buffer), (length))
#endif

#define platformGetSysTick() rfalPlatformGetSystemTick()
#define platformTimerCreate(timeout_ms) rfalPlatformTimerCreate(timeout_ms)
#define platformTimerIsExpired(timer) rfalPlatformTimerIsExpired(timer)
#define platformTimerGetRemaining(timer) rfalPlatformTimerGetRemaining(timer)
#define platformTimerDestroy(timer) ((void)(timer))
#define platformDelay(delay_ms) rfalPlatformDelay(delay_ms)

#define platformLedsInitialize() ((void)0)
#define platformLedOff(port, pin) ((void)0)
#define platformLedOn(port, pin) ((void)0)
#define platformLedToggle(port, pin) ((void)0)

#define platformLog(...) rfalPlatformLog(__VA_ARGS__)
#define platformAssert(expression)                               \
  do {                                                           \
    if (!(expression)) {                                         \
      rfalPlatformAssertFailed(#expression, __FILE__, __LINE__); \
    }                                                            \
  } while (0)
#define platformErrorHandle() rfalPlatformErrorHandle(__FILE__, __LINE__)

// 完成平台接口定义后加载官方 RFAL 默认功能配置。
#include "rfal_defConfig.h"
