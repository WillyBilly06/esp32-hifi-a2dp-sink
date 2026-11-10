# LHDC V5 decoder: native codec-worker adapter + optimized decoder (codecs/lhdcv5/fast).
# All decoder memory is allocated when an LHDC stream opens and freed when it closes.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES native_bt codec_core PRIV_REQUIRES log esp_common esp_timer esp_hw_support heap freertos)
    return()
endif()
if(NOT DBT_PROBE_LHDC_BUILD)
    idf_component_register()
    return()
endif()
get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(owned "${workspace}/native_bt/codecs/lhdcv5")
set(fast "${owned}/fast")
idf_component_register(SRCS "${owned}/dbt_lhdcv5_codec.c"
    "${fast}/lhdc_dec.c" "${fast}/lhdc_imdct.c" "${fast}/lhdc_entropy_dec.c" "${fast}/lhdc_sns_synth.c"
    "${fast}/lhdc_bit_reader.c" "${fast}/lhdc_diag_config.c" "${fast}/lhdc_tables.c"
    INCLUDE_DIRS "${owned}" PRIV_INCLUDE_DIRS "${fast}"
    REQUIRES native_bt codec_core PRIV_REQUIRES log esp_common esp_timer esp_hw_support heap freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11 -fwrapv -O3)
# DBT_LHDC_QUALITY: per-frame quality counters for the adapter. LHDC_DEC_DUAL_CORE=1 decodes the two
# channels on both cores (faster frames, +21 KB RAM, work on the Bluetooth core); off by default.
target_compile_definitions(${COMPONENT_LIB} PRIVATE DBT_LHDC_QUALITY=1 DBT_LHDC_FAST_MANTISSA=1)
