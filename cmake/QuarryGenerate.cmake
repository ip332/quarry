function(_quarry_generate_fail)
    set(_message)
    foreach(_part IN LISTS ARGV)
        string(APPEND _message "${_part}")
    endforeach()
    message(FATAL_ERROR "quarry_generate_cpp: ${_message}")
endfunction()

function(_quarry_generate_reject_unsafe value label)
    if("${value}" MATCHES "[;\n\r]")
        _quarry_generate_fail("${label} contains a semicolon, newline, or carriage return")
    endif()
endfunction()

function(_quarry_generate_reject_genex value label)
    if("${value}" MATCHES "\\$<")
        _quarry_generate_fail("${label} must not contain generator expressions")
    endif()
endfunction()

function(_quarry_generate_absolute input base output_variable label)
    _quarry_generate_reject_unsafe("${input}" "${label}")
    _quarry_generate_reject_genex("${input}" "${label}")

    if(IS_ABSOLUTE "${input}")
        set(_path "${input}")
    else()
        cmake_path(ABSOLUTE_PATH input BASE_DIRECTORY "${base}" NORMALIZE OUTPUT_VARIABLE _path)
    endif()
    cmake_path(NORMAL_PATH _path OUTPUT_VARIABLE _path)
    set(${output_variable} "${_path}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_normalize_absolute input output_variable label)
    _quarry_generate_reject_unsafe("${input}" "${label}")
    _quarry_generate_reject_genex("${input}" "${label}")
    if(NOT IS_ABSOLUTE "${input}")
        _quarry_generate_fail("${label} must be an absolute path: ${input}")
    endif()
    cmake_path(NORMAL_PATH input OUTPUT_VARIABLE _path)
    set(${output_variable} "${_path}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_path_inside root path output_variable)
    cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${root}" OUTPUT_VARIABLE _relative_path)
    if(_relative_path STREQUAL "" OR IS_ABSOLUTE "${_relative_path}" OR
       _relative_path MATCHES "^\\.\\.($|/|\\\\)")
        set(${output_variable} FALSE PARENT_SCOPE)
        return()
    endif()
    set(${output_variable} TRUE PARENT_SCOPE)
endfunction()

function(_quarry_generate_imported_compiler_location output_variable)
    if(NOT TARGET Quarry::schema_compiler)
        _quarry_generate_fail("Quarry::schema_compiler target is not available")
    endif()

    get_target_property(_imported Quarry::schema_compiler IMPORTED)
    get_target_property(_type Quarry::schema_compiler TYPE)
    if(NOT _imported OR NOT _type STREQUAL "EXECUTABLE")
        _quarry_generate_fail(
            "Quarry::schema_compiler must be an installed\n"
            "imported executable target")
    endif()

    get_target_property(_location Quarry::schema_compiler IMPORTED_LOCATION)
    if(NOT _location OR _location MATCHES "-NOTFOUND$")
        get_target_property(_configs Quarry::schema_compiler IMPORTED_CONFIGURATIONS)
        set(_locations)
        foreach(_config IN LISTS _configs)
            get_target_property(_config_location Quarry::schema_compiler
                                "IMPORTED_LOCATION_${_config}")
            if(_config_location AND NOT _config_location MATCHES "-NOTFOUND$")
                list(APPEND _locations "${_config_location}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _locations)
        list(LENGTH _locations _location_count)
        if(_location_count EQUAL 1)
            list(GET _locations 0 _location)
        else()
            _quarry_generate_fail(
                "could not resolve an unambiguous configure-time compiler executable location")
        endif()
    endif()

    _quarry_generate_reject_unsafe("${_location}" "compiler path")
    _quarry_generate_reject_genex("${_location}" "compiler path")
    if(NOT EXISTS "${_location}")
        _quarry_generate_fail("compiler executable does not exist: ${_location}")
    endif()

    cmake_path(NORMAL_PATH _location OUTPUT_VARIABLE _location)
    set(${output_variable} "${_location}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_validate_compiler_path compiler)
    if(NOT EXISTS "${compiler}")
        _quarry_generate_fail("SCHEMA_COMPILER executable does not exist: ${compiler}")
    endif()
    if(IS_DIRECTORY "${compiler}")
        _quarry_generate_fail("SCHEMA_COMPILER must name an executable file, not a directory: "
                                   "${compiler}")
    endif()
endfunction()

function(_quarry_generate_record_selected_compiler compiler schema)
    get_property(_selected GLOBAL PROPERTY _QUARRY_SELECTED_SCHEMA_COMPILER)
    if(NOT _selected)
        set_property(GLOBAL PROPERTY _QUARRY_SELECTED_SCHEMA_COMPILER "${compiler}")
        return()
    endif()
    if(NOT _selected STREQUAL compiler)
        _quarry_generate_fail(
            "only one schema compiler is allowed per CMake build tree.\n\n"
            "Previously selected compiler:\n"
            "  ${_selected}\n\n"
            "Newly requested compiler:\n"
            "  ${compiler}\n\n"
            "Current schema:\n"
            "  ${schema}")
    endif()
endfunction()

function(_quarry_generate_compiler_location output_variable dependency_variable schema)
    if(DEFINED QUARRY_GENERATE_SCHEMA_COMPILER)
        _quarry_generate_normalize_absolute("${QUARRY_GENERATE_SCHEMA_COMPILER}"
                                                 _location "SCHEMA_COMPILER")
        _quarry_generate_validate_compiler_path("${_location}")
        set(_dependency)
    else()
        if(CMAKE_CROSSCOMPILING)
            _quarry_generate_fail(
                "cross-compiling requires SCHEMA_COMPILER with an absolute path to a "
                "host-runnable quarry-schema-compiler. The target package compiler may "
                "not run on the build host; package managers or toolchains must provision "
                "the host executable explicitly.")
        endif()
        _quarry_generate_imported_compiler_location(_location)
        set(_dependency Quarry::schema_compiler)
    endif()

    _quarry_generate_record_selected_compiler("${_location}" "${schema}")
    set(${output_variable} "${_location}" PARENT_SCOPE)
    set(${dependency_variable} "${_dependency}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_validate_generated_code_api_version_value value label)
    if("${value}" STREQUAL "")
        _quarry_generate_fail("${label} must not be empty")
    endif()
    if("${value}" MATCHES "[;\r\n]")
        _quarry_generate_fail("${label} must be a\ncanonical non-negative decimal integer")
    endif()
    if(NOT "${value}" MATCHES "^(0|[1-9][0-9]*)$")
        _quarry_generate_fail("${label} must be a\ncanonical non-negative decimal integer")
    endif()
endfunction()

function(_quarry_generate_query_generated_code_api_version compiler output_variable)
    execute_process(
        COMMAND "${compiler}" --print-generated-code-api-version
        RESULT_VARIABLE _query_result
        OUTPUT_VARIABLE _query_stdout
        ERROR_VARIABLE _query_stderr)
    if(NOT _query_result EQUAL 0)
        _quarry_generate_fail(
            "failed to query the generated-code API version from the selected Quarry "
            "schema compiler.\n"
            "Compiler:\n"
            "  ${compiler}\n\n"
            "The compiler must support:\n"
            "  --print-generated-code-api-version\n\n"
            "Exit code:\n"
            "  ${_query_result}\n\n"
            "stdout:\n"
            "  ${_query_stdout}\n\n"
            "stderr:\n"
            "  ${_query_stderr}")
    endif()

    set(_normalized_stdout "${_query_stdout}")
    string(REGEX REPLACE "\r?\n$" "" _normalized_stdout "${_normalized_stdout}")
    if(_normalized_stdout STREQUAL _query_stdout)
        _quarry_generate_fail(
            "compiler did not terminate its generated-code API version output with a newline:\n"
            "  ${compiler}")
    endif()
    if(_normalized_stdout STREQUAL "")
        _quarry_generate_fail(
            "compiler reported an empty generated-code API version:\n"
            "  ${compiler}")
    endif()
    if("${_normalized_stdout}" MATCHES "[;\r\n]")
        _quarry_generate_fail(
            "compiler reported an unsafe generated-code API version:\n"
            "  ${_normalized_stdout}")
    endif()

    _quarry_generate_validate_generated_code_api_version_value(
        "${_normalized_stdout}" "compiler generated-code API version")
    set(${output_variable} "${_normalized_stdout}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_validate_package_generated_code_api_version output_variable)
    if(NOT DEFINED Quarry_GENERATED_CODE_API_VERSION)
        _quarry_generate_fail(
            "The Quarry package does not provide a valid "
            "Quarry_GENERATED_CODE_API_VERSION value.\n"
            "Reinstall a compatible Quarry package.")
    endif()

    _quarry_generate_validate_generated_code_api_version_value(
        "${Quarry_GENERATED_CODE_API_VERSION}" "Quarry_GENERATED_CODE_API_VERSION")
    set(${output_variable} "${Quarry_GENERATED_CODE_API_VERSION}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_parse_arguments)
    set(_keywords SCHEMA OUTPUT_DIR OUT_FILES ROOT_FILE_STEM FILE_EXTENSION SCHEMA_COMPILER)
    set(_pending_keyword)

    foreach(_arg IN LISTS ARGN)
        if(_pending_keyword)
            set(QUARRY_GENERATE_${_pending_keyword} "${_arg}")
            set(QUARRY_GENERATE_${_pending_keyword} "${_arg}" PARENT_SCOPE)
            set(_pending_keyword)
            continue()
        endif()

        list(FIND _keywords "${_arg}" _keyword_index)
        if(_keyword_index EQUAL -1)
            _quarry_generate_fail("unknown argument '${_arg}'")
        endif()

        if(DEFINED QUARRY_GENERATE_SEEN_${_arg})
            _quarry_generate_fail("duplicate ${_arg} argument")
        endif()
        set(QUARRY_GENERATE_SEEN_${_arg} TRUE)
        set(_pending_keyword "${_arg}")
    endforeach()

    if(_pending_keyword)
        _quarry_generate_fail("missing value for ${_pending_keyword}")
    endif()

    foreach(_required SCHEMA OUTPUT_DIR OUT_FILES)
        if(NOT DEFINED QUARRY_GENERATE_SEEN_${_required})
            _quarry_generate_fail("missing required ${_required} argument")
        endif()
    endforeach()

    foreach(_keyword IN LISTS _keywords)
        if(DEFINED QUARRY_GENERATE_${_keyword} AND
           "${QUARRY_GENERATE_${_keyword}}" STREQUAL "")
            _quarry_generate_fail("${_keyword} must not be empty")
        endif()
    endforeach()
endfunction()

function(_quarry_generate_validate_outputs output_dir schema outputs)
    set(_seen)
    foreach(_output IN LISTS ${outputs})
        if(_output STREQUAL "")
            _quarry_generate_fail("compiler reported an empty output path")
        endif()
        _quarry_generate_reject_unsafe("${_output}" "reported output path")
        _quarry_generate_reject_genex("${_output}" "reported output path")
        if(NOT IS_ABSOLUTE "${_output}")
            _quarry_generate_fail("compiler reported a non-absolute output path: ${_output}")
        endif()
        cmake_path(NORMAL_PATH _output OUTPUT_VARIABLE _normalized_output)
        _quarry_generate_path_inside("${output_dir}" "${_normalized_output}" _inside)
        if(NOT _inside)
            _quarry_generate_fail(
                "compiler reported output outside OUTPUT_DIR: ${_normalized_output}")
        endif()
        list(FIND _seen "${_normalized_output}" _duplicate_index)
        if(NOT _duplicate_index EQUAL -1)
            _quarry_generate_fail("compiler reported duplicate output: ${_normalized_output}")
        endif()
        list(APPEND _seen "${_normalized_output}")
    endforeach()
    set(${outputs} "${_seen}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_query_outputs compiler output_dir schema root_file_stem file_extension
         output_variable)
    set(_quarry_list_args
        --list-outputs
        --output-directory "${output_dir}")
    if(NOT "${root_file_stem}" STREQUAL "")
        list(APPEND _quarry_list_args --root-file-stem "${root_file_stem}")
    endif()
    if(NOT "${file_extension}" STREQUAL "")
        list(APPEND _quarry_list_args --file-extension "${file_extension}")
    endif()

    execute_process(
        COMMAND "${compiler}" ${_quarry_list_args} "${schema}"
        RESULT_VARIABLE _quarry_list_result
        OUTPUT_VARIABLE _quarry_list_stdout
        ERROR_VARIABLE _quarry_list_stderr)

    if(NOT _quarry_list_result EQUAL 0)
        _quarry_generate_fail(
            "failed to list outputs for schema '${schema}' with compiler "
            "'${compiler}' (exit ${_quarry_list_result}):\n"
            "${_quarry_list_stderr}")
    endif()

    if(_quarry_list_stderr)
        _quarry_generate_fail(
            "compiler wrote stderr while listing outputs for schema '${schema}':\n"
            "${_quarry_list_stderr}")
    endif()
    if("${_quarry_list_stdout}" MATCHES "[;\r]")
        _quarry_generate_fail(
            "compiler output contains a semicolon or carriage return and cannot be parsed safely")
    endif()

    set(_quarry_output_text "${_quarry_list_stdout}")
    if(_quarry_output_text MATCHES "(^|\n)\n")
        _quarry_generate_fail("compiler reported an empty output line")
    endif()
    if(_quarry_output_text MATCHES "\n$")
        string(REGEX REPLACE "\n$" "" _quarry_output_text "${_quarry_output_text}")
    endif()
    if(_quarry_output_text STREQUAL "")
        _quarry_generate_fail("compiler reported no outputs for schema '${schema}'")
    endif()

    string(REPLACE "\n" ";" _quarry_outputs "${_quarry_output_text}")
    _quarry_generate_validate_outputs("${output_dir}" "${schema}" _quarry_outputs)
    set(${output_variable} "${_quarry_outputs}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_format_inventory outputs output_variable)
    set(_formatted)
    foreach(_output IN LISTS ${outputs})
        string(APPEND _formatted "  ${_output}\n")
    endforeach()
    if(_formatted STREQUAL "")
        set(_formatted "  <none>\n")
    endif()
    set(${output_variable} "${_formatted}" PARENT_SCOPE)
endfunction()

function(_quarry_generate_check_output_inventory schema compiler expected_outputs current_outputs)
    list(LENGTH ${expected_outputs} _expected_count)
    list(LENGTH ${current_outputs} _current_count)
    set(_mismatch FALSE)
    if(NOT _expected_count EQUAL _current_count)
        set(_mismatch TRUE)
    else()
        if(_expected_count GREATER 0)
            math(EXPR _last_index "${_expected_count} - 1")
            foreach(_index RANGE 0 ${_last_index})
                list(GET ${expected_outputs} ${_index} _expected_output)
                list(GET ${current_outputs} ${_index} _current_output)
                if(NOT _expected_output STREQUAL _current_output)
                    set(_mismatch TRUE)
                endif()
            endforeach()
        endif()
    endif()

    if(_mismatch)
        _quarry_generate_format_inventory(${expected_outputs} _expected_formatted)
        _quarry_generate_format_inventory(${current_outputs} _current_formatted)
        _quarry_generate_fail(
            "generated-output inventory changed after configuration.\n\n"
            "Schema:\n"
            "  ${schema}\n\n"
            "Configured compiler:\n"
            "  ${compiler}\n\n"
            "Configured outputs:\n"
            "${_expected_formatted}\n"
            "Current outputs:\n"
            "${_current_formatted}\n"
            "Reconfigure the CMake build before continuing.")
    endif()
endfunction()

function(_quarry_generate_verify_output_inventory)
    foreach(_required
            QUARRY_VERIFY_COMPILER
            QUARRY_VERIFY_SCHEMA
            QUARRY_VERIFY_OUTPUT_DIR
            QUARRY_VERIFY_EXPECTED_OUTPUTS)
        if(NOT DEFINED ${_required})
            _quarry_generate_fail("missing verification variable ${_required}")
        endif()
    endforeach()

    set(_root_file_stem)
    if(DEFINED QUARRY_VERIFY_ROOT_FILE_STEM)
        set(_root_file_stem "${QUARRY_VERIFY_ROOT_FILE_STEM}")
    endif()
    set(_file_extension)
    if(DEFINED QUARRY_VERIFY_FILE_EXTENSION)
        set(_file_extension "${QUARRY_VERIFY_FILE_EXTENSION}")
    endif()

    _quarry_generate_reject_unsafe("${QUARRY_VERIFY_COMPILER}" "compiler path")
    _quarry_generate_reject_genex("${QUARRY_VERIFY_COMPILER}" "compiler path")
    _quarry_generate_reject_unsafe("${QUARRY_VERIFY_SCHEMA}" "schema path")
    _quarry_generate_reject_genex("${QUARRY_VERIFY_SCHEMA}" "schema path")
    _quarry_generate_reject_unsafe("${QUARRY_VERIFY_OUTPUT_DIR}" "output directory")
    _quarry_generate_reject_genex("${QUARRY_VERIFY_OUTPUT_DIR}" "output directory")

    _quarry_generate_query_outputs(
        "${QUARRY_VERIFY_COMPILER}"
        "${QUARRY_VERIFY_OUTPUT_DIR}"
        "${QUARRY_VERIFY_SCHEMA}"
        "${_root_file_stem}"
        "${_file_extension}"
        _current_outputs)

    set(_expected_outputs ${QUARRY_VERIFY_EXPECTED_OUTPUTS})
    _quarry_generate_validate_outputs("${QUARRY_VERIFY_OUTPUT_DIR}"
                                           "${QUARRY_VERIFY_SCHEMA}" _expected_outputs)
    _quarry_generate_check_output_inventory("${QUARRY_VERIFY_SCHEMA}"
                                                 "${QUARRY_VERIFY_COMPILER}"
                                                 _expected_outputs _current_outputs)
endfunction()

function(_quarry_generate_register_outputs schema outputs)
    get_property(_claimed_paths GLOBAL PROPERTY QUARRY_GENERATED_OUTPUT_PATHS)
    get_property(_claimed_schemas GLOBAL PROPERTY QUARRY_GENERATED_OUTPUT_SCHEMAS)

    foreach(_output IN LISTS outputs)
        list(FIND _claimed_paths "${_output}" _claimed_index)
        if(NOT _claimed_index EQUAL -1)
            list(GET _claimed_schemas "${_claimed_index}" _prior_schema)
            _quarry_generate_fail(
                "output '${_output}' is already claimed by schema '${_prior_schema}'")
        endif()
        list(APPEND _claimed_paths "${_output}")
        list(APPEND _claimed_schemas "${schema}")
    endforeach()

    set_property(GLOBAL PROPERTY QUARRY_GENERATED_OUTPUT_PATHS "${_claimed_paths}")
    set_property(GLOBAL PROPERTY QUARRY_GENERATED_OUTPUT_SCHEMAS "${_claimed_schemas}")
endfunction()

function(quarry_generate_cpp)
    _quarry_generate_parse_arguments(${ARGN})

    _quarry_generate_absolute("${QUARRY_GENERATE_SCHEMA}"
                                   "${CMAKE_CURRENT_SOURCE_DIR}" _quarry_schema
                                   "SCHEMA")
    _quarry_generate_absolute("${QUARRY_GENERATE_OUTPUT_DIR}"
                                   "${CMAKE_CURRENT_BINARY_DIR}" _quarry_output_dir
                                   "OUTPUT_DIR")
    _quarry_generate_compiler_location(_quarry_compiler _quarry_compiler_dependency
                                            "${_quarry_schema}")
    _quarry_generate_query_generated_code_api_version("${_quarry_compiler}"
                                                           _quarry_compiler_api_version)
    _quarry_generate_validate_package_generated_code_api_version(
        _quarry_runtime_api_version)

    if(NOT _quarry_compiler_api_version STREQUAL _quarry_runtime_api_version)
        _quarry_generate_fail(
            "The selected Quarry schema compiler is\n"
            "incompatible with the target runtime package.\n\n"
            "Compiler:\n"
            "  ${_quarry_compiler}\n\n"
            "Compiler generated-code API version:\n"
            "  ${_quarry_compiler_api_version}\n\n"
            "Target runtime generated-code API version:\n"
            "  ${_quarry_runtime_api_version}\n\n"
            "Use a host compiler that supports generated-code API version "
            "${_quarry_runtime_api_version}.")
    endif()

    if(NOT EXISTS "${_quarry_schema}")
        _quarry_generate_fail("schema file does not exist: ${_quarry_schema}")
    endif()

    set(_quarry_generate_args
        --output-directory "${_quarry_output_dir}")
    set(_quarry_root_file_stem)
    set(_quarry_file_extension)

    if(DEFINED QUARRY_GENERATE_ROOT_FILE_STEM)
        _quarry_generate_reject_unsafe("${QUARRY_GENERATE_ROOT_FILE_STEM}"
                                            "ROOT_FILE_STEM")
        _quarry_generate_reject_genex("${QUARRY_GENERATE_ROOT_FILE_STEM}"
                                           "ROOT_FILE_STEM")
        set(_quarry_root_file_stem "${QUARRY_GENERATE_ROOT_FILE_STEM}")
        list(APPEND _quarry_generate_args --root-file-stem
             "${QUARRY_GENERATE_ROOT_FILE_STEM}")
    endif()
    if(DEFINED QUARRY_GENERATE_FILE_EXTENSION)
        _quarry_generate_reject_unsafe("${QUARRY_GENERATE_FILE_EXTENSION}"
                                            "FILE_EXTENSION")
        _quarry_generate_reject_genex("${QUARRY_GENERATE_FILE_EXTENSION}"
                                           "FILE_EXTENSION")
        set(_quarry_file_extension "${QUARRY_GENERATE_FILE_EXTENSION}")
        list(APPEND _quarry_generate_args --file-extension
             "${QUARRY_GENERATE_FILE_EXTENSION}")
    endif()

    _quarry_generate_query_outputs("${_quarry_compiler}" "${_quarry_output_dir}"
                                        "${_quarry_schema}" "${_quarry_root_file_stem}"
                                        "${_quarry_file_extension}" _quarry_outputs)
    _quarry_generate_register_outputs("${_quarry_schema}" "${_quarry_outputs}")

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${_quarry_schema}" "${_quarry_compiler}")

    set(_quarry_module "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")

    add_custom_command(
        OUTPUT ${_quarry_outputs}
        COMMAND
            "${CMAKE_COMMAND}"
            "-DQUARRY_VERIFY_OUTPUT_INVENTORY=TRUE"
            "-DQUARRY_VERIFY_COMPILER=${_quarry_compiler}"
            "-DQUARRY_VERIFY_SCHEMA=${_quarry_schema}"
            "-DQUARRY_VERIFY_OUTPUT_DIR=${_quarry_output_dir}"
            "-DQUARRY_VERIFY_EXPECTED_OUTPUTS=${_quarry_outputs}"
            "-DQUARRY_VERIFY_ROOT_FILE_STEM=${_quarry_root_file_stem}"
            "-DQUARRY_VERIFY_FILE_EXTENSION=${_quarry_file_extension}"
            -P "${_quarry_module}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_quarry_output_dir}"
        COMMAND "${_quarry_compiler}" ${_quarry_generate_args}
                "${_quarry_schema}"
        DEPENDS
            "${_quarry_schema}"
            ${_quarry_compiler_dependency}
            "${_quarry_compiler}"
            "${_quarry_module}"
        VERBATIM)
    set_source_files_properties(${_quarry_outputs} PROPERTIES GENERATED TRUE)

    set(${QUARRY_GENERATE_OUT_FILES} "${_quarry_outputs}" PARENT_SCOPE)
endfunction()

if(DEFINED QUARRY_VERIFY_OUTPUT_INVENTORY)
    _quarry_generate_verify_output_inventory()
endif()
