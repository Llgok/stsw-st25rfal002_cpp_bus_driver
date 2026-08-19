set(rfal_root "${CMAKE_CURRENT_LIST_DIR}/third_party/STSW-ST25RFAL002")
set(rfal_source_dir "${rfal_root}/source")
set(st25r3916_source_dir "${rfal_source_dir}/st25r3916")

set(
        rfal_sources
        "${rfal_source_dir}/rfal_analogConfig.c"
        "${rfal_source_dir}/rfal_cd.c"
        "${rfal_source_dir}/rfal_crc.c"
        "${rfal_source_dir}/rfal_dlma.c"
        "src/vendor_port/rfal_dpo_runtime.c"
        "${rfal_source_dir}/rfal_iso15693_2.c"
        "${rfal_source_dir}/rfal_isoDep.c"
        "${rfal_source_dir}/rfal_nfc.c"
        "${rfal_source_dir}/rfal_nfcDep.c"
        "${rfal_source_dir}/rfal_nfca.c"
        "${rfal_source_dir}/rfal_nfcb.c"
        "${rfal_source_dir}/rfal_nfcf.c"
        "${rfal_source_dir}/rfal_nfcv.c"
        "${rfal_source_dir}/rfal_st25tb.c"
        "${rfal_source_dir}/rfal_st25xv.c"
        "${rfal_source_dir}/rfal_t1t.c"
        "${rfal_source_dir}/rfal_t2t.c"
        "${rfal_source_dir}/rfal_t4t.c"
)

set(
        st25r3916_sources
        "src/vendor_port/rfal_rfst25r3916_runtime.c"
        "src/vendor_port/st25r3916_runtime_driver.c"
        "src/vendor_port/st25r3916_com_runtime.c"
        "src/vendor_port/st25r3916_runtime.c"
        "src/vendor_port/st25r3916_analog_config.c"
        "src/vendor_port/st25r3916b_analog_config.c"
        "src/vendor_port/st25r3916_dpo_config.c"
        "src/vendor_port/st25r3916b_dpo_config.c"
        "${st25r3916_source_dir}/st25r3916_aat.c"
        "${st25r3916_source_dir}/st25r3916_irq.c"
        "${st25r3916_source_dir}/st25r3916_led.c"
)

idf_component_register(
        SRCS
        "src/platform/rfal_platform.cpp"
        "src/st25r3916x/st25r3916x_driver.cpp"
        ${rfal_sources}
        ${st25r3916_sources}
        INCLUDE_DIRS
        "src"
        "src/platform"
        "${rfal_root}/include"
        "${rfal_source_dir}"
        "${st25r3916_source_dir}"
        REQUIRES
        "cpp_bus_driver"
        PRIV_REQUIRES
        "esp_driver_gpio"
        "esp_timer"
        "freertos"
)

# 启用 ST25R3916X 的 RFAL Power Down 支持。
target_compile_definitions(${COMPONENT_LIB} PRIVATE
        RFAL_FEATURE_LOWPOWER_MODE=1)

# 官方代码和本地 RFAL 移植文件保持使用 ISO C 编译。
set_source_files_properties(
        ${rfal_sources}
        ${st25r3916_sources}
        PROPERTIES
        LANGUAGE C
)

# RFAL T4T 的 Lc 为 uint8_t；当前 APDU 缓冲区配置使官方范围检查恒假。
# 仅对该第三方源文件关闭类型范围警告，保持 ST 原始代码不变。
set_source_files_properties(
        "${rfal_source_dir}/rfal_t4t.c"
        PROPERTIES
        COMPILE_OPTIONS "-Wno-type-limits"
)
