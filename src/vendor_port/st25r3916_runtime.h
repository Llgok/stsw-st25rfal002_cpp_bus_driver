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

void st25r3916RuntimeReset(void);
ReturnCode st25r3916RuntimeDetectChip(void);
bool st25r3916RuntimeSetIdentity(uint8_t identity);
st25r3916RuntimeChip st25r3916RuntimeGetChip(void);
bool st25r3916RuntimeIsSt25r3916(void);
bool st25r3916RuntimeIsSt25r3916B(void);
uint8_t st25r3916RuntimeGetIdentity(void);
uint8_t st25r3916RuntimeGetRevision(void);
uint8_t st25r3916RuntimeGetSpaceBRegisterCount(void);
ReturnCode st25r3916RuntimeLoadAnalogConfig(void);

#ifdef __cplusplus
}
#endif

