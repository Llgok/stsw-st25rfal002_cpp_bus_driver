/*
 * @Description: STSW-ST25RFAL002 桥接组件统一公开头文件
 * @Author: LILYGO_L
 * @Date: 2026-07-15 09:23:16
 * @LastEditTime: 2026-07-15 09:50:25
 * @License: GPL 3.0
 */
#pragma once

#include "st25r3916/st25r3916_driver.h"

// 官方代码使用 ISO C 编译，C++ 引用聚合头文件时统一使用 C 语言链接。
extern "C" {
#include "rfal_analogConfig.h"
#include "rfal_cd.h"
#include "rfal_chip.h"
#include "rfal_dlma.h"
#include "rfal_dpo.h"
#include "rfal_isoDep.h"
#include "rfal_nfc.h"
#include "rfal_nfcDep.h"
#include "rfal_nfca.h"
#include "rfal_nfcb.h"
#include "rfal_nfcf.h"
#include "rfal_nfcv.h"
#include "rfal_rf.h"
#include "rfal_st25tb.h"
#include "rfal_st25xv.h"
#include "rfal_t1t.h"
#include "rfal_t2t.h"
#include "rfal_t4t.h"
#include "rfal_utils.h"
#include "st25r3916.h"
#include "st25r3916_aat.h"
#include "st25r3916_com.h"
#include "st25r3916_irq.h"
#include "st25r3916_led.h"
}
