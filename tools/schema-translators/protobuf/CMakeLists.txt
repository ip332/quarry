find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

set(QUARRY_PROTOBUF_ABSL_LINK_LIBS "")
foreach(QUARRY_ABSL_CANDIDATE
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::log_internal_format
    absl::log_internal_log_sink_set
    absl::log_internal_conditions
    absl::log_internal_globals
    absl::log_severity
    absl::raw_logging_internal
    absl::strings
    absl::str_format
    absl::base
    absl::container_memory
    absl::raw_hash_set
)
    if(TARGET ${QUARRY_ABSL_CANDIDATE})
        list(APPEND QUARRY_PROTOBUF_ABSL_LINK_LIBS ${QUARRY_ABSL_CANDIDATE})
    endif()
endforeach()

add_library(quarry_protobuf_descriptor_loader
    descriptor_model.cpp
)
target_include_directories(quarry_protobuf_descriptor_loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(quarry_protobuf_descriptor_loader SYSTEM PUBLIC
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(quarry_protobuf_descriptor_loader
    PUBLIC
        protobuf::libprotobuf
        quarry_project_options
        ${QUARRY_PROTOBUF_ABSL_LINK_LIBS}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Protobuf's bundled Abseil headers use Clang nullability annotations.
    # Keep Quarry warnings-as-errors enabled for translator code without
    # promoting those third-party header annotations to errors.
    set_source_files_properties(descriptor_model.cpp PROPERTIES
        COMPILE_OPTIONS
            "-Wno-error=nullability-extension;-Wno-error=gcc-compat;-Wno-error=gnu-anonymous-struct;-Wno-error=nested-anon-types"
    )
endif()

add_executable(quarry_protobuf_translator main.cpp)
set_target_properties(quarry_protobuf_translator PROPERTIES
    OUTPUT_NAME quarry-protobuf-translator
)
target_link_libraries(quarry_protobuf_translator
    PRIVATE
        quarry_protobuf_descriptor_loader
        quarry_project_options
)
