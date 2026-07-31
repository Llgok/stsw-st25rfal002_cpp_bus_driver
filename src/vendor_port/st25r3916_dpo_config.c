/* Preserve the upstream ST25R3916 DPO table under a variant-specific name. */
#ifdef ST25R3916B
#undef ST25R3916B
#endif
#ifndef ST25R3916
#define ST25R3916
#endif
#define rfalDpoDefaultSettings st25r3916DpoSettings
#include "rfal_dpoTbl.h"

const uint8_t st25r3916DpoSettingsLength =
    (uint8_t)sizeof(st25r3916DpoSettings);
