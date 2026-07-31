/* Preserve the upstream ST25R3916B analog table under a variant-specific name. */
#define ST25R3916_RUNTIME_BUILD_ST25R3916B_TABLE
#ifdef ST25R3916
#undef ST25R3916
#endif
#define ST25R3916B
#define rfalAnalogConfigDefaultSettings st25r3916bAnalogConfigSettings
#include "rfal_analogConfigTbl.h"

const uint16_t st25r3916bAnalogConfigSettingsLength =
    (uint16_t)sizeof(st25r3916bAnalogConfigSettings);
