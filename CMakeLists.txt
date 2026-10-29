cmake_minimum_required(VERSION 3.25)

project(OpenA2DP VERSION 0.1.0 LANGUAGES C CXX)

option(OPENA2DP_BUILD_WASAPI_POC "Build the Windows WASAPI + LDAC PoC" ON)

add_library(opena2dp_protocol STATIC
    src/avdtp.cpp
)
target_include_directories(opena2dp_protocol PUBLIC include)
target_compile_features(opena2dp_protocol PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(opena2dp_protocol PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(opena2dp_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(opena2dp_protocol_tests
        tests/avdtp_tests.cpp
    )
    target_link_libraries(opena2dp_protocol_tests PRIVATE opena2dp_protocol)
    target_compile_features(opena2dp_protocol_tests PRIVATE cxx_std_20)
    add_test(NAME opena2dp_protocol_tests COMMAND opena2dp_protocol_tests)
endif()

if(OPENA2DP_BUILD_WASAPI_POC)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The WASAPI PoC requires Windows. Set OPENA2DP_BUILD_WASAPI_POC=OFF to build the portable protocol core.")
    endif()
    if(CMAKE_SIZEOF_VOID_P LESS 8)
        message(FATAL_ERROR "Build x64 or ARM64; 32-bit Windows is not supported.")
    endif()

    include(FetchContent)

    # The pinned upstream wrapper declares CMake 3.0. Keep it usable with
    # CMake 4, which removed policy compatibility below 3.5.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    endif()

    # EHfive's CMake wrapper around Sony's Apache-2.0 libldac. Pin the wrapper
    # commit; the wrapper pins its AOSP libldac source as a git submodule.
    FetchContent_Declare(
        ldacbt
        GIT_REPOSITORY https://github.com/EHfive/ldacBT.git
        GIT_TAG        fbffba45d15d959da6ee04eafe14c0d4721f6030
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(ldacbt)

    # The upstream CMake file links the POSIX math library unconditionally.
    # MSVC's math routines are provided by the CRT, so remove that link item.
    if(MSVC)
        foreach(_link_property IN ITEMS
                LINK_LIBRARIES
                INTERFACE_LINK_LIBRARIES
                LINK_INTERFACE_LIBRARIES)
            get_target_property(_ldac_link_libraries ldacBT_enc ${_link_property})
            if(_ldac_link_libraries)
                list(REMOVE_ITEM _ldac_link_libraries m)
                set_property(TARGET ldacBT_enc PROPERTY ${_link_property}
                    "${_ldac_link_libraries}")
            endif()
        endforeach()
        # The upstream header does not decorate LDACBT_API with __declspec(dllexport).
        set_target_properties(ldacBT_enc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

        # This AOSP snapshot has two POSIX/GCC-only decorations that are not
        # used by the encoder on Windows: an unused attribute and unistd.h.
        target_include_directories(ldacBT_enc BEFORE PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat"
        )
        target_compile_options(ldacBT_enc PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/ldac_msvc_compat.h"
        )
    endif()

    add_executable(opena2dp
        src/main.cpp
    )

    target_compile_features(opena2dp PRIVATE cxx_std_20)
    target_include_directories(opena2dp PRIVATE
        "${ldacbt_SOURCE_DIR}/libldac/inc"
    )
    target_link_libraries(opena2dp PRIVATE
        opena2dp_protocol
        ldacBT_enc
        ole32
        avrt
    )

    if(MSVC)
        target_compile_options(opena2dp PRIVATE /W4 /permissive- /EHsc /utf-8)
        target_compile_definitions(opena2dp PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
    endif()

    # FetchContent builds libldac as a DLL. Place it beside the PoC executable.
    add_custom_command(TARGET opena2dp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:ldacBT_enc>"
            "$<TARGET_FILE_DIR:opena2dp>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:opena2dp>/LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ldacbt_SOURCE_DIR}/libldac/LICENSE"
            "$<TARGET_FILE_DIR:opena2dp>/LICENSE.libldac.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ldacbt_SOURCE_DIR}/libldac/NOTICE"
            "$<TARGET_FILE_DIR:opena2dp>/NOTICE.libldac.txt"
        VERBATIM
    )
endif()
