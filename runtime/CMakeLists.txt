add_library(quarry_runtime INTERFACE)
add_library(Quarry::runtime ALIAS quarry_runtime)
set_target_properties(quarry_runtime PROPERTIES EXPORT_NAME runtime)
target_compile_features(quarry_runtime INTERFACE cxx_std_20)
target_include_directories(quarry_runtime
    INTERFACE
        $<BUILD_INTERFACE:${QUARRY_GENERATED_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
