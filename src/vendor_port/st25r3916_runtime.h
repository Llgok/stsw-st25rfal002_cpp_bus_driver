/*
 * Runtime ST25R3916/ST25R3916B selection for the local RFAL port.
 *
 * This file is part of the cpp_bus_driver platform adaptation.  The upstream
 * STSW-ST25RFAL002 sources in third_party are intentionally left unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rfal_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ST25R3916_RUNTIME_CHIP_UNKNOWN = 0,
  ST25R3916_RUNTIME_CHIP_ST25R3916,
  ST25R3916_RUNTIME_CHIP_ST25R3916B,
} st25r3916RuntimeChip;

// Space-A 包含从 IO_CONF1 到 IC_IDENTITY 的全部64个寄存器。
#define ST25R3916_RUNTIME_SPACE_A_REGISTER_COUNT 64U

// ST25R3916B 最多包含24个有效的非连续 Space-B 寄存器。
#define ST25R3916_RUNTIME_SPACE_B_REGISTER_COUNT_MAX 24U

/**
 * @brief 保存运行时识别芯片的完整寄存器转储
 */
typedef struct {
  // Space-A 寄存器，数组索引与寄存器地址一致。
  uint8_t RsA[ST25R3916_RUNTIME_SPACE_A_REGISTER_COUNT];
  // Space-B 寄存器，顺序与 ST 官方 st25r3916GetRegsDump() 保持一致。
  uint8_t RsB[ST25R3916_RUNTIME_SPACE_B_REGISTER_COUNT_MAX];
  // RsB 中实际有效的寄存器数量，ST25R3916 为16，ST25R3916B 为24。
  uint8_t RsBCount;
} st25r3916RuntimeRegs;

void st25r3916RuntimeReset(void);
ReturnCode st25r3916RuntimeDetectChip(void);
bool st25r3916RuntimeSetIdentity(uint8_t identity);
st25r3916RuntimeChip st25r3916RuntimeGetChip(void);
bool st25r3916RuntimeIsSt25r3916(void);
bool st25r3916RuntimeIsSt25r3916B(void);
uint8_t st25r3916RuntimeGetIdentity(void);
uint8_t st25r3916RuntimeGetRevision(void);

/**
 * @brief 获取当前识别芯片的有效 Space-B 寄存器数量
 * @return ST25R3916 返回16，ST25R3916B 返回24，尚未识别时返回0
 */
uint8_t st25r3916RuntimeGetSpaceBRegisterCount(void);
ReturnCode st25r3916RuntimeLoadAnalogConfig(void);

/**
 * @brief 读取运行时识别芯片的完整 Space-A 和 Space-B 寄存器
 * @param regDump 保存寄存器值和有效 Space-B 数量的缓冲区
 * @return RFAL_ERR_NONE 读取成功，RFAL_ERR_PARAM 参数无效，
 *         RFAL_ERR_WRONG_STATE 芯片型号尚未识别，RFAL_ERR_IO 总线访问失败
 */
ReturnCode st25r3916RuntimeGetRegsDump(st25r3916RuntimeRegs* regDump);

#ifdef __cplusplus
}
#endif

