# Resolve Quarry's numeric release version from git_version and Git history.

function(quarry_resolve_version)
    set(_version_file "${CMAKE_SOURCE_DIR}/git_version")
    if(NOT EXISTS "${_version_file}")
        message(FATAL_ERROR "Quarry git_version file is missing: ${_version_file}")
    endif()

    file(READ "${_version_file}" _version_text)
    string(LENGTH "${_version_text}" _version_length)
    if(_version_length LESS 2)
        set(_major_minor "")
        set(_version_ending "")
    else()
        math(EXPR _major_minor_length "${_version_length} - 1")
        string(SUBSTRING "${_version_text}" 0 ${_major_minor_length} _major_minor)
        string(SUBSTRING "${_version_text}" ${_major_minor_length} 1 _version_ending)
    endif()
    if(NOT _version_ending STREQUAL "\n" OR
       NOT _major_minor MATCHES "^[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR
            "Quarry git_version must contain exactly Major.Minor and a final newline; "
            "got invalid content in ${_version_file}")
    endif()
    string(REPLACE "." ";" _parts "${_major_minor}")
    list(GET _parts 0 _major)
    list(GET _parts 1 _minor)

    set(_resolved FALSE)
    set(_source "")
    set(_sha "")
    set(_dirty FALSE)
    find_package(Git QUIET)
    if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --is-shallow-repository
            RESULT_VARIABLE _shallow_result
            OUTPUT_VARIABLE _shallow
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_shallow_result EQUAL 0 AND _shallow STREQUAL "true")
            set(_git_unusable TRUE)
        else()
            set(_git_unusable FALSE)
        endif()
        if(NOT _git_unusable)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" log -1 --format=%H -- git_version
                RESULT_VARIABLE _base_result
                OUTPUT_VARIABLE _base
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_base_result EQUAL 0 AND NOT _base STREQUAL "")
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-list --count "${_base}..HEAD"
                    RESULT_VARIABLE _count_result
                    OUTPUT_VARIABLE _count
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_VARIABLE _count_error
                )
                if(NOT _count_result EQUAL 0 OR NOT _count MATCHES "^[0-9]+$")
                    message(FATAL_ERROR "Unable to count commits after the git_version base commit: ${_count_error}")
                endif()
                set(_revision "${_count}")
                set(_source "git")
                set(_resolved TRUE)
            else()
                # git_version is present in the working tree but not committed yet.
                # This is the intentional bootstrap state for PR-166.
                set(_revision "0")
                set(_source "git-bootstrap")
                set(_resolved TRUE)
            endif()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --short HEAD
                RESULT_VARIABLE _sha_result
                OUTPUT_VARIABLE _sha
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT _sha_result EQUAL 0)
                set(_sha "")
            endif()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" status --porcelain --untracked-files=no
                RESULT_VARIABLE _dirty_result
                OUTPUT_VARIABLE _dirty_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_dirty_result EQUAL 0 AND NOT _dirty_output STREQUAL "")
                set(_dirty TRUE)
            endif()
        endif()
    endif()

    if(NOT _resolved AND EXISTS "${CMAKE_SOURCE_DIR}/cmake/QuarryResolvedVersion.cmake")
        include("${CMAKE_SOURCE_DIR}/cmake/QuarryResolvedVersion.cmake")
        if(DEFINED QUARRY_VERSION AND QUARRY_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
            string(REPLACE "." ";" _resolved_parts "${QUARRY_VERSION}")
            list(GET _resolved_parts 0 _fallback_major)
            list(GET _resolved_parts 1 _fallback_minor)
            list(GET _resolved_parts 2 _revision)
            if(NOT _fallback_major STREQUAL _major OR NOT _fallback_minor STREQUAL _minor)
                message(FATAL_ERROR
                    "Packaged Quarry version ${QUARRY_VERSION} does not match git_version ${_major_minor}")
            endif()
            if(DEFINED QUARRY_GIT_SHA)
                set(_sha "${QUARRY_GIT_SHA}")
            endif()
            set(_source "packaged-fallback")
            set(_resolved TRUE)
        elseif(DEFINED QUARRY_ARCHIVE_TAG AND
               QUARRY_ARCHIVE_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-rc\\.[0-9]+)?$")
            set(_fallback_major "${CMAKE_MATCH_1}")
            set(_fallback_minor "${CMAKE_MATCH_2}")
            set(_revision "${CMAKE_MATCH_3}")
            if(NOT _fallback_major STREQUAL _major OR NOT _fallback_minor STREQUAL _minor)
                message(FATAL_ERROR
                    "Packaged archive tag ${QUARRY_ARCHIVE_TAG} does not match git_version ${_major_minor}")
            endif()
            if(DEFINED QUARRY_GIT_SHA)
                set(_sha "${QUARRY_GIT_SHA}")
            endif()
            set(_source "packaged-archive")
            set(_resolved TRUE)
        endif()
    endif()

    if(NOT _resolved)
        message(FATAL_ERROR
            "Unable to resolve Quarry version: Git history is unavailable or shallow and "
            "no packaged fallback exists. Fetch full history with 'git fetch --unshallow' "
            "or provide a packaged cmake/QuarryResolvedVersion.cmake fallback.")
    endif()

    set(QUARRY_VERSION_MAJOR "${_major}" PARENT_SCOPE)
    set(QUARRY_VERSION_MINOR "${_minor}" PARENT_SCOPE)
    set(QUARRY_VERSION_REVISION "${_revision}" PARENT_SCOPE)
    set(QUARRY_VERSION "${_major}.${_minor}.${_revision}" PARENT_SCOPE)
    set(QUARRY_GIT_SHA "${_sha}" PARENT_SCOPE)
    set(QUARRY_VERSION_GIT_SHA "${_sha}" PARENT_SCOPE)
    set(QUARRY_VERSION_DIRTY "${_dirty}" PARENT_SCOPE)
    set(QUARRY_VERSION_SOURCE "${_source}" PARENT_SCOPE)
    if(_dirty)
        set(QUARRY_VERSION_DISPLAY "${_major}.${_minor}.${_revision}-dirty" PARENT_SCOPE)
    else()
        set(QUARRY_VERSION_DISPLAY "${_major}.${_minor}.${_revision}" PARENT_SCOPE)
    endif()
endfunction()

function(quarry_write_resolved_version_fallback output_file)
    file(WRITE "${output_file}"
        "# Generated release fallback; do not edit.\n"
        "set(QUARRY_VERSION \"${QUARRY_VERSION}\")\n"
        "set(QUARRY_GIT_SHA \"${QUARRY_GIT_SHA}\")\n")
endfunction()
