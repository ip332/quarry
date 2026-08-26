add_library(quarry_runtime_c STATIC generic_brf.c generic_brf_api.c generic_brf_structures.c
    generic_brf_traversal.c)
add_library(Quarry::runtime_c ALIAS quarry_runtime_c)
set_target_properties(quarry_runtime_c PROPERTIES EXPORT_NAME runtime_c)
target_compile_features(quarry_runtime_c INTERFACE c_std_99)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(quarry_runtime_c PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_include_directories(quarry_runtime_c
    PUBLIC
        $<BUILD_INTERFACE:${QUARRY_GENERATED_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
