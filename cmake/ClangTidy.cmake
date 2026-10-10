include_guard(GLOBAL)

option(ZYSU_ENABLE_CLANG_TIDY
    "Run clang-tidy on ZySU first-party userspace sources" ON)

if(NOT DEFINED ZYSU_CLANG_TIDY_CONFIG)
    set(ZYSU_CLANG_TIDY_CONFIG
        "${CMAKE_CURRENT_LIST_DIR}/../.clang-tidy")
endif()

function(zysu_enable_clang_tidy target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "clang-tidy target does not exist: ${target}")
    endif()

    if(NOT ZYSU_ENABLE_CLANG_TIDY)
        message(STATUS "clang-tidy: disabled for ${target}")
        return()
    endif()

    set(languages ${ARGN})
    if(NOT languages)
        set(languages C CXX)
    endif()

    # Android clang-tidy must match the NDK compiler and its resource headers.
    # Prefer the compiler's bin directory over an unrelated host LLVM in PATH.
    if(ANDROID AND NOT ZYSU_CLANG_TIDY_PROGRAM)
        if(CMAKE_CXX_COMPILER)
            get_filename_component(ndk_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        else()
            get_filename_component(ndk_toolchain_bin "${CMAKE_C_COMPILER}" DIRECTORY)
        endif()
        find_program(ZYSU_CLANG_TIDY_PROGRAM NAMES clang-tidy
            HINTS "${ndk_toolchain_bin}"
            NO_DEFAULT_PATH)
    endif()

    if(NOT ZYSU_CLANG_TIDY_PROGRAM)
        find_program(ZYSU_CLANG_TIDY_PROGRAM
            NAMES clang-tidy clang-tidy-22 clang-tidy-21 clang-tidy-20
                  clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16
                  clang-tidy-14)
    endif()

    if(NOT ZYSU_CLANG_TIDY_PROGRAM)
        message(FATAL_ERROR
            "clang-tidy not found. Install it or configure with "
            "-DZYSU_ENABLE_CLANG_TIDY=OFF")
    endif()
    if(NOT EXISTS "${ZYSU_CLANG_TIDY_CONFIG}")
        message(FATAL_ERROR
            ".clang-tidy not found at ${ZYSU_CLANG_TIDY_CONFIG}")
    endif()

    set(tidy_command
        "${ZYSU_CLANG_TIDY_PROGRAM};--config-file=${ZYSU_CLANG_TIDY_CONFIG}")
    foreach(language IN LISTS languages)
        if(language STREQUAL "C")
            set_property(TARGET "${target}" PROPERTY C_CLANG_TIDY "${tidy_command}")
        elseif(language STREQUAL "CXX")
            set_property(TARGET "${target}" PROPERTY CXX_CLANG_TIDY "${tidy_command}")
        else()
            message(FATAL_ERROR
                "Unsupported clang-tidy language for ${target}: ${language}")
        endif()
    endforeach()

    message(STATUS
        "clang-tidy: enabled for ${target} (${ZYSU_CLANG_TIDY_PROGRAM})")
endfunction()
