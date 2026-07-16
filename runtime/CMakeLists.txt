add_library(breadcrumbs_runtime INTERFACE)
add_library(Breadcrumbs::runtime ALIAS breadcrumbs_runtime)
set_target_properties(breadcrumbs_runtime PROPERTIES EXPORT_NAME runtime)
target_compile_features(breadcrumbs_runtime INTERFACE cxx_std_20)
target_include_directories(breadcrumbs_runtime
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
