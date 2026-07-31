/* Preserve the upstream ST25R3916B DPO table under a variant-specific name. */
#define ST25R3916_RUNTIME_BUILD_ST25R3916B_TABLE
#ifdef ST25R3916
#undef ST25R3916
#endif
#define ST25R3916B
#define rfalDpoDefaultSettings st25r3916bDpoSettings
#include "rfal_dpoTbl.h"

const uint8_t st25r3916bDpoSettingsLength =
    (uint8_t)sizeof(st25r3916bDpoSettings);

