cmake_minimum_required(VERSION 3.21)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(BudanovGramPlugins LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(budanovgram_quickjs STATIC
    vendor/quickjs/quickjs.c
    vendor/quickjs/dtoa.c
    vendor/quickjs/libregexp.c
    vendor/quickjs/libunicode.c
)
set_target_properties(budanovgram_quickjs PROPERTIES C_STANDARD 11)
target_compile_definitions(budanovgram_quickjs PRIVATE
    QUICKJS_NG_BUILD _CRT_SECURE_NO_WARNINGS
)
target_include_directories(budanovgram_quickjs PUBLIC vendor/quickjs)
if (MSVC)
    target_compile_options(budanovgram_quickjs PRIVATE /w /experimental:c11atomics)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.js" BG_PLUGIN_SDK)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.js")
configure_file(sdk_source.h.in sdk_source.h @ONLY)
add_executable(BudanovPluginHost host.cpp)
target_compile_features(BudanovPluginHost PRIVATE cxx_std_20)
target_include_directories(BudanovPluginHost PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(BudanovPluginHost PRIVATE budanovgram_quickjs)
if (MSVC)
    target_link_options(BudanovPluginHost PRIVATE /STACK:8388608)
elseif (UNIX)
    target_link_libraries(BudanovPluginHost PRIVATE m pthread)
endif()

if (TARGET Telegram)
    add_library(budanovgram_plugins STATIC manager.cpp manager.h)
    target_link_libraries(budanovgram_plugins PRIVATE desktop-app::external_qt)
    target_compile_features(budanovgram_plugins PRIVATE cxx_std_20)
    if (MSVC)
        target_compile_options(budanovgram_plugins PRIVATE /utf-8 /Zc:__cplusplus /DNOMINMAX)
        target_link_libraries(budanovgram_plugins PRIVATE crypt32)
    endif()
    target_include_directories(budanovgram_plugins PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    target_link_libraries(Telegram PRIVATE budanovgram_plugins)
    add_dependencies(Telegram BudanovPluginHost)
    set_target_properties(BudanovPluginHost PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/plugin-host-$<CONFIG>.qrc"
        CONTENT "<RCC><qresource prefix=\"/budanovgram\"><file alias=\"plugin-host\">$<TARGET_FILE:BudanovPluginHost></file></qresource></RCC>"
    )
    set(host_resource "${CMAKE_CURRENT_BINARY_DIR}/qrc_plugin_host_$<CONFIG>.cpp")
    add_custom_command(OUTPUT "${host_resource}"
        COMMAND Qt${QT_VERSION_MAJOR}::rcc --name budanovgram_plugin_host
            --output "${host_resource}" "${CMAKE_CURRENT_BINARY_DIR}/plugin-host-$<CONFIG>.qrc"
        DEPENDS BudanovPluginHost "${CMAKE_CURRENT_BINARY_DIR}/plugin-host-$<CONFIG>.qrc"
        VERBATIM
    )
    add_library(budanovgram_plugin_resource OBJECT "${host_resource}")
    target_link_libraries(budanovgram_plugin_resource PRIVATE desktop-app::external_qt)
    target_sources(Telegram PRIVATE $<TARGET_OBJECTS:budanovgram_plugin_resource>)
    add_executable(BudanovPluginTests EXCLUDE_FROM_ALL tests/manager_tests.cpp)
    target_compile_features(BudanovPluginTests PRIVATE cxx_std_20)
    target_link_libraries(BudanovPluginTests PRIVATE desktop-app::external_qt)
    if (MSVC)
        target_compile_options(BudanovPluginTests PRIVATE /utf-8 /Zc:__cplusplus /DNOMINMAX)
        target_link_libraries(BudanovPluginTests PRIVATE crypt32)
    endif()
endif()
