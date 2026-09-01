cmake_minimum_required(VERSION 3.20)

project(EqualizerAPOMacOS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(eapo_dsp STATIC
    src/dsp_engine.cpp
)

target_include_directories(eapo_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(eapo_dsp PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

add_executable(eapo-macos
    src/main.cpp
    src/audio_host.cpp
)

target_link_libraries(eapo-macos PRIVATE eapo_dsp)

target_compile_options(eapo-macos PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(src/audio_host.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(eapo-macos PRIVATE
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )

    add_executable(eapo-macos-app MACOSX_BUNDLE
        src/app_main.mm
        src/audio_host.cpp
        config.example.txt
    )
    set_source_files_properties(config.example.txt PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    set_target_properties(eapo-macos-app PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "EqualizerAPO for macOS"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.equalizerapo.macos"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
    target_link_libraries(eapo-macos-app PRIVATE
        eapo_dsp
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework Cocoa"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
    target_compile_options(eapo-macos-app PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
    )
else()
    message(STATUS "Building the DSP and a non-macOS host stub. Core Audio is only available on macOS.")
endif()

add_executable(eapo-dsp-smoke
    tests/dsp_smoke.cpp
)
target_link_libraries(eapo-dsp-smoke PRIVATE eapo_dsp)
target_compile_options(eapo-dsp-smoke PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)

enable_testing()
add_test(NAME eapo-dsp-smoke COMMAND eapo-dsp-smoke)