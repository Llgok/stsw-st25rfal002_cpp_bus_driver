/* Runtime chip detection and analog-table selection for the local RFAL port. */
#include "vendor_port/st25r3916_runtime.h"

#include <stddef.h>

#include "rfal_analogConfig.h"
#include "st25r3916_com.h"

extern const uint8_t st25r3916AnalogConfigSettings[];
extern const uint16_t st25r3916AnalogConfigSettingsLength;
extern const uint8_t st25r3916bAnalogConfigSettings[];
extern const uint16_t st25r3916bAnalogConfigSettingsLength;

static st25r3916RuntimeChip runtimeChip = ST25R3916_RUNTIME_CHIP_UNKNOWN;
static uint8_t runtimeIdentity = 0U;

void st25r3916RuntimeReset(void) {
  runtimeChip = ST25R3916_RUNTIME_CHIP_UNKNOWN;
  runtimeIdentity = 0U;
}

bool st25r3916RuntimeSetIdentity(uint8_t identity) {
  const uint8_t type =
      (uint8_t)(identity & ST25R3916_REG_IC_IDENTITY_ic_type_mask);
  const uint8_t revision =
      (uint8_t)(identity & ST25R3916_REG_IC_IDENTITY_ic_rev_mask);

  runtimeIdentity = identity;
  if (type == ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916) {
    runtimeChip = ST25R3916_RUNTIME_CHIP_ST25R3916;
    return true;
  }
  if ((type == ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916B) &&
      (revision >= 1U)) {
    runtimeChip = ST25R3916_RUNTIME_CHIP_ST25R3916B;
    return true;
  }

  runtimeChip = ST25R3916_RUNTIME_CHIP_UNKNOWN;
  return false;
}

ReturnCode st25r3916RuntimeDetectChip(void) {
  uint8_t identity = 0U;
  st25r3916ReadRegister(ST25R3916_REG_IC_IDENTITY, &identity);
  return st25r3916RuntimeSetIdentity(identity) ? RFAL_ERR_NONE
                                               : RFAL_ERR_HW_MISMATCH;
}

st25r3916RuntimeChip st25r3916RuntimeGetChip(void) { return runtimeChip; }

bool st25r3916RuntimeIsSt25r3916(void) {
  return runtimeChip == ST25R3916_RUNTIME_CHIP_ST25R3916;
}

bool st25r3916RuntimeIsSt25r3916B(void) {
  return runtimeChip == ST25R3916_RUNTIME_CHIP_ST25R3916B;
}

uint8_t st25r3916RuntimeGetIdentity(void) { return runtimeIdentity; }

uint8_t st25r3916RuntimeGetRevision(void) {
  return (uint8_t)(runtimeIdentity & ST25R3916_REG_IC_IDENTITY_ic_rev_mask);
}

uint8_t st25r3916RuntimeGetSpaceBRegisterCount(void) {
  return st25r3916RuntimeIsSt25r3916B() ? 24U : 16U;
}

ReturnCode st25r3916RuntimeLoadAnalogConfig(void) {
  if (st25r3916RuntimeIsSt25r3916()) {
    return rfalAnalogConfigListWriteRaw(st25r3916AnalogConfigSettings,
        st25r3916AnalogConfigSettingsLength);
  }
  if (st25r3916RuntimeIsSt25r3916B()) {
    return rfalAnalogConfigListWriteRaw(st25r3916bAnalogConfigSettings,
        st25r3916bAnalogConfigSettingsLength);
  }
  return RFAL_ERR_WRONG_STATE;
}

