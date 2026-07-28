add_library(quarry_runtime_c INTERFACE)
add_library(Quarry::runtime_c ALIAS quarry_runtime_c)
set_target_properties(quarry_runtime_c PROPERTIES EXPORT_NAME runtime_c)
target_compile_features(quarry_runtime_c INTERFACE c_std_99)
target_include_directories(quarry_runtime_c
    INTERFACE
        $<BUILD_INTERFACE:${QUARRY_GENERATED_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
