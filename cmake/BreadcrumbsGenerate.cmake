function(_breadcrumbs_generate_fail message)
    message(FATAL_ERROR "breadcrumbs_generate_cpp: ${message}")
endfunction()

function(_breadcrumbs_generate_reject_unsafe value label)
    if("${value}" MATCHES "[;\n\r]")
        _breadcrumbs_generate_fail("${label} contains a semicolon, newline, or carriage return")
    endif()
endfunction()

function(_breadcrumbs_generate_reject_genex value label)
    if("${value}" MATCHES "\\$<")
        _breadcrumbs_generate_fail("${label} must not contain generator expressions")
    endif()
endfunction()

function(_breadcrumbs_generate_absolute input base output_variable label)
    _breadcrumbs_generate_reject_unsafe("${input}" "${label}")
    _breadcrumbs_generate_reject_genex("${input}" "${label}")

    if(IS_ABSOLUTE "${input}")
        set(_path "${input}")
    else()
        cmake_path(ABSOLUTE_PATH input BASE_DIRECTORY "${base}" NORMALIZE OUTPUT_VARIABLE _path)
    endif()
    cmake_path(NORMAL_PATH _path OUTPUT_VARIABLE _path)
    set(${output_variable} "${_path}" PARENT_SCOPE)
endfunction()

function(_breadcrumbs_generate_path_inside root path output_variable)
    cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${root}" OUTPUT_VARIABLE _relative_path)
    if(_relative_path STREQUAL "" OR IS_ABSOLUTE "${_relative_path}" OR
       _relative_path MATCHES "^\\.\\.($|/|\\\\)")
        set(${output_variable} FALSE PARENT_SCOPE)
        return()
    endif()
    set(${output_variable} TRUE PARENT_SCOPE)
endfunction()

function(_breadcrumbs_generate_compiler_location output_variable)
    if(CMAKE_CROSSCOMPILING)
        _breadcrumbs_generate_fail(
            "cross-compiling is not supported by the first helper contract; use explicit custom commands")
    endif()

    if(NOT TARGET Breadcrumbs::schema_compiler)
        _breadcrumbs_generate_fail("Breadcrumbs::schema_compiler target is not available")
    endif()

    get_target_property(_imported Breadcrumbs::schema_compiler IMPORTED)
    get_target_property(_type Breadcrumbs::schema_compiler TYPE)
    if(NOT _imported OR NOT _type STREQUAL "EXECUTABLE")
        _breadcrumbs_generate_fail(
            "Breadcrumbs::schema_compiler must be an installed imported executable target")
    endif()

    get_target_property(_location Breadcrumbs::schema_compiler IMPORTED_LOCATION)
    if(NOT _location OR _location MATCHES "-NOTFOUND$")
        get_target_property(_configs Breadcrumbs::schema_compiler IMPORTED_CONFIGURATIONS)
        set(_locations)
        foreach(_config IN LISTS _configs)
            get_target_property(_config_location Breadcrumbs::schema_compiler
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
            _breadcrumbs_generate_fail(
                "could not resolve an unambiguous configure-time compiler executable location")
        endif()
    endif()

    _breadcrumbs_generate_reject_unsafe("${_location}" "compiler path")
    _breadcrumbs_generate_reject_genex("${_location}" "compiler path")
    if(NOT EXISTS "${_location}")
        _breadcrumbs_generate_fail("compiler executable does not exist: ${_location}")
    endif()

    set(${output_variable} "${_location}" PARENT_SCOPE)
endfunction()

function(_breadcrumbs_generate_parse_arguments)
    set(_keywords SCHEMA OUTPUT_DIR OUT_FILES ROOT_FILE_STEM FILE_EXTENSION)
    set(_pending_keyword)

    foreach(_arg IN LISTS ARGN)
        if(_pending_keyword)
            set(BREADCRUMBS_GENERATE_${_pending_keyword} "${_arg}")
            set(BREADCRUMBS_GENERATE_${_pending_keyword} "${_arg}" PARENT_SCOPE)
            set(_pending_keyword)
            continue()
        endif()

        list(FIND _keywords "${_arg}" _keyword_index)
        if(_keyword_index EQUAL -1)
            _breadcrumbs_generate_fail("unknown argument '${_arg}'")
        endif()

        if(DEFINED BREADCRUMBS_GENERATE_SEEN_${_arg})
            _breadcrumbs_generate_fail("duplicate ${_arg} argument")
        endif()
        set(BREADCRUMBS_GENERATE_SEEN_${_arg} TRUE)
        set(_pending_keyword "${_arg}")
    endforeach()

    if(_pending_keyword)
        _breadcrumbs_generate_fail("missing value for ${_pending_keyword}")
    endif()

    foreach(_required SCHEMA OUTPUT_DIR OUT_FILES)
        if(NOT DEFINED BREADCRUMBS_GENERATE_SEEN_${_required})
            _breadcrumbs_generate_fail("missing required ${_required} argument")
        endif()
    endforeach()

    foreach(_keyword IN LISTS _keywords)
        if(DEFINED BREADCRUMBS_GENERATE_${_keyword} AND
           "${BREADCRUMBS_GENERATE_${_keyword}}" STREQUAL "")
            _breadcrumbs_generate_fail("${_keyword} must not be empty")
        endif()
    endforeach()
endfunction()

function(_breadcrumbs_generate_validate_outputs output_dir schema outputs)
    set(_seen)
    foreach(_output IN LISTS ${outputs})
        if(_output STREQUAL "")
            _breadcrumbs_generate_fail("compiler reported an empty output path")
        endif()
        _breadcrumbs_generate_reject_unsafe("${_output}" "reported output path")
        _breadcrumbs_generate_reject_genex("${_output}" "reported output path")
        if(NOT IS_ABSOLUTE "${_output}")
            _breadcrumbs_generate_fail("compiler reported a non-absolute output path: ${_output}")
        endif()
        cmake_path(NORMAL_PATH _output OUTPUT_VARIABLE _normalized_output)
        _breadcrumbs_generate_path_inside("${output_dir}" "${_normalized_output}" _inside)
        if(NOT _inside)
            _breadcrumbs_generate_fail(
                "compiler reported output outside OUTPUT_DIR: ${_normalized_output}")
        endif()
        list(FIND _seen "${_normalized_output}" _duplicate_index)
        if(NOT _duplicate_index EQUAL -1)
            _breadcrumbs_generate_fail("compiler reported duplicate output: ${_normalized_output}")
        endif()
        list(APPEND _seen "${_normalized_output}")
    endforeach()
    set(${outputs} "${_seen}" PARENT_SCOPE)
endfunction()

function(_breadcrumbs_generate_register_outputs schema outputs)
    get_property(_claimed_paths GLOBAL PROPERTY BREADCRUMBS_GENERATED_OUTPUT_PATHS)
    get_property(_claimed_schemas GLOBAL PROPERTY BREADCRUMBS_GENERATED_OUTPUT_SCHEMAS)

    foreach(_output IN LISTS outputs)
        list(FIND _claimed_paths "${_output}" _claimed_index)
        if(NOT _claimed_index EQUAL -1)
            list(GET _claimed_schemas "${_claimed_index}" _prior_schema)
            _breadcrumbs_generate_fail(
                "output '${_output}' is already claimed by schema '${_prior_schema}'")
        endif()
        list(APPEND _claimed_paths "${_output}")
        list(APPEND _claimed_schemas "${schema}")
    endforeach()

    set_property(GLOBAL PROPERTY BREADCRUMBS_GENERATED_OUTPUT_PATHS "${_claimed_paths}")
    set_property(GLOBAL PROPERTY BREADCRUMBS_GENERATED_OUTPUT_SCHEMAS "${_claimed_schemas}")
endfunction()

function(breadcrumbs_generate_cpp)
    _breadcrumbs_generate_parse_arguments(${ARGN})

    _breadcrumbs_generate_compiler_location(_breadcrumbs_compiler)
    _breadcrumbs_generate_absolute("${BREADCRUMBS_GENERATE_SCHEMA}"
                                   "${CMAKE_CURRENT_SOURCE_DIR}" _breadcrumbs_schema
                                   "SCHEMA")
    _breadcrumbs_generate_absolute("${BREADCRUMBS_GENERATE_OUTPUT_DIR}"
                                   "${CMAKE_CURRENT_BINARY_DIR}" _breadcrumbs_output_dir
                                   "OUTPUT_DIR")

    if(NOT EXISTS "${_breadcrumbs_schema}")
        _breadcrumbs_generate_fail("schema file does not exist: ${_breadcrumbs_schema}")
    endif()

    set(_breadcrumbs_list_args
        --list-outputs
        --output-directory "${_breadcrumbs_output_dir}")
    set(_breadcrumbs_generate_args
        --output-directory "${_breadcrumbs_output_dir}")

    if(DEFINED BREADCRUMBS_GENERATE_ROOT_FILE_STEM)
        _breadcrumbs_generate_reject_unsafe("${BREADCRUMBS_GENERATE_ROOT_FILE_STEM}"
                                            "ROOT_FILE_STEM")
        _breadcrumbs_generate_reject_genex("${BREADCRUMBS_GENERATE_ROOT_FILE_STEM}"
                                           "ROOT_FILE_STEM")
        list(APPEND _breadcrumbs_list_args --root-file-stem
             "${BREADCRUMBS_GENERATE_ROOT_FILE_STEM}")
        list(APPEND _breadcrumbs_generate_args --root-file-stem
             "${BREADCRUMBS_GENERATE_ROOT_FILE_STEM}")
    endif()
    if(DEFINED BREADCRUMBS_GENERATE_FILE_EXTENSION)
        _breadcrumbs_generate_reject_unsafe("${BREADCRUMBS_GENERATE_FILE_EXTENSION}"
                                            "FILE_EXTENSION")
        _breadcrumbs_generate_reject_genex("${BREADCRUMBS_GENERATE_FILE_EXTENSION}"
                                           "FILE_EXTENSION")
        list(APPEND _breadcrumbs_list_args --file-extension
             "${BREADCRUMBS_GENERATE_FILE_EXTENSION}")
        list(APPEND _breadcrumbs_generate_args --file-extension
             "${BREADCRUMBS_GENERATE_FILE_EXTENSION}")
    endif()

    execute_process(
        COMMAND "${_breadcrumbs_compiler}" ${_breadcrumbs_list_args} "${_breadcrumbs_schema}"
        RESULT_VARIABLE _breadcrumbs_list_result
        OUTPUT_VARIABLE _breadcrumbs_list_stdout
        ERROR_VARIABLE _breadcrumbs_list_stderr)

    if(NOT _breadcrumbs_list_result EQUAL 0)
        _breadcrumbs_generate_fail(
            "failed to list outputs for schema '${_breadcrumbs_schema}' with compiler "
            "'${_breadcrumbs_compiler}' (exit ${_breadcrumbs_list_result}):\n"
            "${_breadcrumbs_list_stderr}")
    endif()

    if(_breadcrumbs_list_stderr)
        _breadcrumbs_generate_fail(
            "compiler wrote stderr while listing outputs for schema '${_breadcrumbs_schema}':\n"
            "${_breadcrumbs_list_stderr}")
    endif()
    if("${_breadcrumbs_list_stdout}" MATCHES "[;\r]")
        _breadcrumbs_generate_fail(
            "compiler output contains a semicolon or carriage return and cannot be parsed safely")
    endif()

    set(_breadcrumbs_output_text "${_breadcrumbs_list_stdout}")
    if(_breadcrumbs_output_text MATCHES "\n$")
        string(REGEX REPLACE "\n$" "" _breadcrumbs_output_text "${_breadcrumbs_output_text}")
    endif()
    if(_breadcrumbs_output_text STREQUAL "")
        _breadcrumbs_generate_fail("compiler reported no outputs for schema '${_breadcrumbs_schema}'")
    endif()
    if(_breadcrumbs_output_text MATCHES "(^|\n)\n")
        _breadcrumbs_generate_fail("compiler reported an empty output line")
    endif()

    string(REPLACE "\n" ";" _breadcrumbs_outputs "${_breadcrumbs_output_text}")
    _breadcrumbs_generate_validate_outputs("${_breadcrumbs_output_dir}" "${_breadcrumbs_schema}"
                                           _breadcrumbs_outputs)
    _breadcrumbs_generate_register_outputs("${_breadcrumbs_schema}" "${_breadcrumbs_outputs}")

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${_breadcrumbs_schema}" "${_breadcrumbs_compiler}")

    add_custom_command(
        OUTPUT ${_breadcrumbs_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_breadcrumbs_output_dir}"
        COMMAND "${_breadcrumbs_compiler}" ${_breadcrumbs_generate_args}
                "${_breadcrumbs_schema}"
        DEPENDS
            "${_breadcrumbs_schema}"
            Breadcrumbs::schema_compiler
            "${_breadcrumbs_compiler}"
        VERBATIM)
    set_source_files_properties(${_breadcrumbs_outputs} PROPERTIES GENERATED TRUE)

    set(${BREADCRUMBS_GENERATE_OUT_FILES} "${_breadcrumbs_outputs}" PARENT_SCOPE)
endfunction()
