find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable googletest installation" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    foreach(gtest_target gtest gtest_main gmock gmock_main)
        if(TARGET ${gtest_target})
            set_target_properties(${gtest_target} PROPERTIES CXX_CLANG_TIDY "")
        endif()
    endforeach()
endif()

function(add_quarry_gtest name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library} GTest::gtest_main quarry_project_options)
    # GTest is an external dependency.  On AppleClang, recent GTest headers
    # instantiate sign-comparison helpers that trigger -Werror in consumers.
    # Keep warnings-as-errors for Quarry and test sources while excluding the
    # external header implementation from that policy.
    get_target_property(gtest_include_dirs GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
    if(gtest_include_dirs)
        target_include_directories(${name} SYSTEM PRIVATE ${gtest_include_dirs})
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        # GTest's templated ASSERT_EQ helpers diagnose mixed signedness at the
        # assertion call site.  Keep the repository-wide -Werror policy while
        # avoiding this compiler/version-specific third-party diagnostic in
        # test executables.
        target_compile_options(${name} PRIVATE -Wno-sign-compare -Wno-error=sign-compare)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Parser/AST compatibility tests intentionally stop at parser-owned behavior.
# Symbols and semantic smoke coverage live in separate targets below.
add_quarry_gtest(parser_smoke_test parser/parser_smoke_test.cpp quarry_compiler_parser)
add_quarry_gtest(lexer_test parser/lexer_test.cpp quarry_compiler_parser)
add_quarry_gtest(ast_test ast/ast_test.cpp quarry_compiler_ast)
add_quarry_gtest(symbols_smoke_test symbols/symbols_smoke_test.cpp quarry_compiler_symbols)
add_quarry_gtest(semantic_smoke_test semantic/semantic_smoke_test.cpp quarry_compiler_semantic)
add_quarry_gtest(yaml_parser_test yaml/yaml_parser_test.cpp quarry_compiler_yaml)
add_quarry_gtest(yaml_schema_decoder_test yaml/schema_decoder_test.cpp quarry_compiler_yaml)
add_quarry_gtest(normalized_source_schema_pipeline_test
                      yaml/normalized_source_schema_pipeline_test.cpp
                      quarry_compiler_yaml)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_symbols)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_semantic)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_layout)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_compiler_context)
target_link_libraries(normalized_source_schema_pipeline_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(yaml_compiler_test frontend/yaml_compiler_test.cpp
                      quarry_compiler_frontend)
target_link_libraries(yaml_compiler_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(layout_smoke_test layout/layout_smoke_test.cpp quarry_compiler_layout)
add_quarry_gtest(qbs_model_test qbs/qbs_model_test.cpp quarry_compiler_qbs)
add_quarry_gtest(qbs_serializer_test qbs/qbs_serializer_test.cpp quarry_compiler_qbs)
add_quarry_gtest(qbs_parser_test qbs/qbs_parser_test.cpp quarry_compiler_qbs)
add_quarry_gtest(schema_ir_golden_test schema_ir/schema_ir_golden_test.cpp
                      quarry_compiler_frontend)
target_link_libraries(schema_ir_golden_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(schema_ir_validation_test schema_ir/schema_ir_validation_test.cpp
                      quarry_compiler_schema_ir)
target_link_libraries(schema_ir_validation_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_smoke_test backend/backend_smoke_test.cpp quarry_compiler_backend)
target_link_libraries(backend_smoke_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_codegen_test backend/backend_codegen_test.cpp
                      quarry_compiler_backend)
target_compile_definitions(backend_codegen_test PRIVATE
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}"
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
target_link_libraries(backend_codegen_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(backend_codegen_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_c_test backend_c/backend_c_test.cpp
                      quarry_compiler_backend_c)
target_link_libraries(backend_c_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(backend_c_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(backend_python_test backend_python/backend_python_test.cpp
                      quarry_compiler_backend_python)
target_link_libraries(backend_python_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(backend_python_test PRIVATE quarry_schema_ir_warning_tweaks)

find_program(QUARRY_PYTHON3_EXECUTABLE python3)
if(QUARRY_PYTHON3_EXECUTABLE)
    add_test(NAME version_resolution_test
        COMMAND ${QUARRY_PYTHON3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/version/version_resolution_test.py"
            --cmake "${CMAKE_COMMAND}"
            --resolver "${CMAKE_SOURCE_DIR}/cmake/QuarryVersion.cmake")
endif()
add_quarry_gtest(python_execution_test backend_python/python_execution_test.cpp
                      quarry_compiler_support)
target_link_libraries(python_execution_test PRIVATE quarry_compiler_backend_python)
target_link_libraries(python_execution_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(python_execution_test PRIVATE quarry_schema_ir_warning_tweaks)
add_dependencies(python_execution_test quarry_schema_compiler)
target_compile_definitions(python_execution_test PRIVATE
    QUARRY_SCHEMA_COMPILER_TOOL="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR="${CMAKE_SOURCE_DIR}/runtime/python/src"
    QUARRY_TEST_PYTHON3="${QUARRY_PYTHON3_EXECUTABLE}")
add_quarry_gtest(python_runtime_test backend_python/python_runtime_test.cpp
                      quarry_compiler_support)
target_compile_definitions(python_runtime_test PRIVATE
    QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR="${CMAKE_SOURCE_DIR}/runtime/python/src"
    QUARRY_TEST_PYTHON_RUNTIME_TESTS_DIR="${CMAKE_SOURCE_DIR}/runtime/python/tests"
    QUARRY_TEST_PYTHON3="${QUARRY_PYTHON3_EXECUTABLE}")

if(QUARRY_PYTHON3_EXECUTABLE)
    execute_process(
        COMMAND "${QUARRY_PYTHON3_EXECUTABLE}" -c "import build, setuptools"
        RESULT_VARIABLE QUARRY_PYTHON_PACKAGING_AVAILABLE
        OUTPUT_QUIET
        ERROR_QUIET
    )
endif()

if(QUARRY_PYTHON3_EXECUTABLE AND QUARRY_PYTHON_PACKAGING_AVAILABLE EQUAL 0)
    add_test(NAME python_packaging_test
        COMMAND ${QUARRY_PYTHON3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/python/tests/test_packaging.py"
            --runtime-source "${CMAKE_SOURCE_DIR}/runtime/python"
            --compiler "$<TARGET_FILE:quarry_schema_compiler>"
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}")
    add_test(NAME public_examples_test
        COMMAND ${QUARRY_PYTHON3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/example_validation.py"
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --source-dir "${CMAKE_SOURCE_DIR}"
            --python "${QUARRY_PYTHON3_EXECUTABLE}")
else()
    message(STATUS "Skipping Python packaging and public example tests: install runtime/python test dependencies first")
endif()
add_quarry_gtest(runtime_binary_record_test runtime/binary_record_test.cpp
                      quarry_runtime)
target_compile_definitions(runtime_binary_record_test PRIVATE
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
add_quarry_gtest(runtime_binary_record_v2_test runtime/binary_record_v2_test.cpp
                      quarry_runtime)
target_link_libraries(runtime_binary_record_v2_test PRIVATE quarry_compiler_layout)
target_link_libraries(runtime_binary_record_v2_test PRIVATE quarry_schema_ir_warning_tweaks)
add_quarry_gtest(runtime_qbs_brf_reader_test runtime/qbs_brf_reader_test.cpp
                      quarry_compiler_qbs)
add_quarry_gtest(runtime_qbs_brf_encoder_test runtime/qbs_brf_encoder_test.cpp
                      quarry_compiler_qbs)
add_quarry_gtest(runtime_brf_validation_cache_test runtime/brf_validation_cache_test.cpp
                      quarry_compiler_qbs)
add_quarry_gtest(runtime_c_binary_record_test runtime_c/binary_record_test.cpp
                      quarry_runtime_c)
target_compile_definitions(runtime_c_binary_record_test PRIVATE
    QUARRY_TEST_GENERATED_CODE_API_VERSION_C=${QUARRY_GENERATED_CODE_API_VERSION_C})
add_quarry_gtest(runtime_c_binary_record_v2_test runtime_c/binary_record_v2_test.cpp
                      quarry_runtime_c)
add_quarry_gtest(runtime_package_consumer_test consumer/runtime_package_test.cpp
                      quarry_compiler_support)
target_compile_definitions(runtime_package_consumer_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_RUNTIME_PACKAGE_CONSUMER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/consumer/runtime_package/project")
add_quarry_gtest(schema_compiler_install_test consumer/schema_compiler_install_test.cpp
                      quarry_compiler_support)
target_compile_definitions(schema_compiler_install_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_VERSION_DISPLAY="${QUARRY_VERSION_DISPLAY}"
    QUARRY_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    QUARRY_TEST_INSTALL_BINDIR="${CMAKE_INSTALL_BINDIR}")
add_quarry_gtest(schema_compiler_package_test consumer/schema_compiler_package_test.cpp
                      quarry_compiler_support)
target_compile_definitions(schema_compiler_package_test PRIVATE
    QUARRY_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}"
    QUARRY_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_C_COMPILER="${CMAKE_C_COMPILER}"
    QUARRY_SCHEMA_COMPILER_PACKAGE_CONSUMER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/examples/cpp/schema_compiler_cmake"
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION})
add_quarry_gtest(schema_compiler_tool_test tools/schema_compiler_tool_test.cpp
                      quarry_compiler_support)
add_dependencies(schema_compiler_tool_test quarry_schema_compiler)
target_compile_definitions(schema_compiler_tool_test PRIVATE
    QUARRY_SCHEMA_COMPILER_TOOL="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_VERSION_DISPLAY="${QUARRY_VERSION_DISPLAY}"
    QUARRY_TEST_GENERATED_CODE_API_VERSION=${QUARRY_GENERATED_CODE_API_VERSION}
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_REPO_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}")
add_quarry_gtest(protobuf_descriptor_loader_test
                      tools/protobuf_descriptor_loader_test.cpp
                      quarry_protobuf_descriptor_loader)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(tools/protobuf_descriptor_loader_test.cpp PROPERTIES
        COMPILE_OPTIONS
            "-Wno-error=nullability-extension;-Wno-error=gcc-compat;-Wno-error=gnu-anonymous-struct;-Wno-error=nested-anon-types"
    )
endif()
target_compile_definitions(protobuf_descriptor_loader_test PRIVATE
    QUARRY_PROTOC_EXECUTABLE="${Protobuf_PROTOC_EXECUTABLE}")
add_quarry_gtest(protobuf_translation_test
                      tools/protobuf_translation_test.cpp
                      quarry_protobuf_descriptor_loader)
add_dependencies(protobuf_translation_test quarry_protobuf_translator quarry_schema_compiler)
target_compile_definitions(protobuf_translation_test PRIVATE
    QUARRY_PROTOC_EXECUTABLE="${Protobuf_PROTOC_EXECUTABLE}"
    QUARRY_PROTOBUF_OPTIONS_PROTO="${CMAKE_SOURCE_DIR}/tools/schema-translators/protobuf/quarry_options.proto"
    QUARRY_PROTOBUF_TRANSLATOR="$<TARGET_FILE:quarry_protobuf_translator>"
    QUARRY_SCHEMA_COMPILER="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_VERSION_DISPLAY="${QUARRY_VERSION_DISPLAY}")
add_quarry_gtest(c_cpp_codec_interop_test interop/c_cpp_codec_interop_test.cpp
                      quarry_compiler_support)
add_dependencies(c_cpp_codec_interop_test quarry_schema_compiler)
target_compile_definitions(c_cpp_codec_interop_test PRIVATE
    QUARRY_SCHEMA_COMPILER_TOOL="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_REPO_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}")
add_quarry_gtest(c_cpp_nested_record_interop_test
                      interop/c_cpp_nested_record_interop_test.cpp
                      quarry_compiler_backend)
target_link_libraries(c_cpp_nested_record_interop_test PRIVATE quarry_compiler_backend_c)
target_link_libraries(c_cpp_nested_record_interop_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(c_cpp_nested_record_interop_test PRIVATE quarry_schema_ir_warning_tweaks)
target_compile_definitions(c_cpp_nested_record_interop_test PRIVATE
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_REPO_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}")
add_quarry_gtest(python_cpp_c_codec_interop_test
                      interop/python_cpp_c_codec_interop_test.cpp
                      quarry_compiler_support)
target_link_libraries(python_cpp_c_codec_interop_test PRIVATE quarry_compiler_backend)
target_link_libraries(python_cpp_c_codec_interop_test PRIVATE quarry_compiler_backend_c)
target_link_libraries(python_cpp_c_codec_interop_test PRIVATE quarry_compiler_backend_python)
target_link_libraries(python_cpp_c_codec_interop_test PRIVATE quarry_compiler_schema_ir)
target_link_libraries(python_cpp_c_codec_interop_test PRIVATE quarry_schema_ir_warning_tweaks)
add_dependencies(python_cpp_c_codec_interop_test quarry_schema_compiler)
target_compile_definitions(python_cpp_c_codec_interop_test PRIVATE
    QUARRY_SCHEMA_COMPILER_TOOL="$<TARGET_FILE:quarry_schema_compiler>"
    QUARRY_TEST_C_COMPILER="${CMAKE_C_COMPILER}"
    QUARRY_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    QUARRY_TEST_REPO_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
    QUARRY_TEST_GENERATED_INCLUDE_DIR="${QUARRY_GENERATED_INCLUDE_DIR}"
    QUARRY_TEST_PYTHON_RUNTIME_SRC_DIR="${CMAKE_SOURCE_DIR}/runtime/python/src"
    QUARRY_TEST_PYTHON3="${QUARRY_PYTHON3_EXECUTABLE}")
add_quarry_gtest(diagnostics_smoke_test diagnostics/diagnostics_smoke_test.cpp quarry_compiler_context)
add_quarry_gtest(support_test support/support_test.cpp quarry_compiler_support)
