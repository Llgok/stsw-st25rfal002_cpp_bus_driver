set(rfal_root "${CMAKE_CURRENT_LIST_DIR}/third_party/STSW-ST25RFAL002")
set(rfal_source_dir "${rfal_root}/source")
set(st25r3916_source_dir "${rfal_source_dir}/st25r3916")

set(
        rfal_sources
        "${rfal_source_dir}/rfal_analogConfig.c"
        "${rfal_source_dir}/rfal_cd.c"
        "${rfal_source_dir}/rfal_crc.c"
        "${rfal_source_dir}/rfal_dlma.c"
        "${rfal_source_dir}/rfal_dpo.c"
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
        "${st25r3916_source_dir}/rfal_rfst25r3916.c"
        "${st25r3916_source_dir}/st25r3916.c"
        "${st25r3916_source_dir}/st25r3916_aat.c"
        "${st25r3916_source_dir}/st25r3916_com.c"
        "${st25r3916_source_dir}/st25r3916_irq.c"
        "${st25r3916_source_dir}/st25r3916_led.c"
)

idf_component_register(
        SRCS
        "src/platform/rfal_platform.cpp"
        "src/st25r3916/st25r3916_driver.cpp"
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

# ST 通信层将每条命令和响应合并为一次全双工传输，cpp_bus_driver 可以在
# 不修改官方源码的前提下管理完整事务的片选信号。
if(CONFIG_STSW_ST25RFAL002_TRANSPORT_SPI)
    target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE
            ST25R_COM_SINGLETXRX
    )
endif()

# 官方代码保持使用 ISO C99 编译。
set_source_files_properties(
        ${rfal_sources}
        ${st25r3916_sources}
        PROPERTIES
        LANGUAGE C
)
