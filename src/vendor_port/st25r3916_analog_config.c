/* Preserve the upstream ST25R3916 analog table under a variant-specific name. */
#ifndef ST25R3916
#define ST25R3916
#endif
#define rfalAnalogConfigDefaultSettings st25r3916AnalogConfigSettings
#include "rfal_analogConfigTbl.h"

const uint16_t st25r3916AnalogConfigSettingsLength =
    (uint16_t)sizeof(st25r3916AnalogConfigSettings);
