cmake_minimum_required(VERSION 3.20)

project(RollingEPUB
    VERSION 1.0.0
    DESCRIPTION "Portable EPUB parsing, layout and rendering library"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(REPUB_BUILD_TESTS "Build the host test suite" ON)
option(REPUB_BUILD_TOOLS "Build the repub_cli host tool" ON)
option(REPUB_BUILD_SHARED "Also build a shared library" OFF)
option(REPUB_WERROR "Treat warnings as errors" OFF)

option(REPUB_ENABLE_TOC "Parse NCX/NAV TOC" ON)
option(REPUB_ENABLE_COVER "Cover image support" ON)
option(REPUB_ENABLE_ENCRYPTION_PROBE "Detect encrypted entries" ON)
option(REPUB_ENABLE_CSS "CSS parsing and cascade" ON)
option(REPUB_ENABLE_FOOTNOTES "Footnote markers and note popups" ON)
option(REPUB_ENABLE_TABLES "Simple table layout" ON)
option(REPUB_ENABLE_DROP_CAPS "Drop caps" ON)
option(REPUB_ENABLE_RUBY "Ruby annotations" ON)
option(REPUB_ENABLE_HYPHENATION "Liang pattern hyphenation" ON)
option(REPUB_ENABLE_HYPHEN_EN "English hyphenation patterns" ON)
option(REPUB_ENABLE_HYPHEN_DE "German hyphenation patterns" OFF)
option(REPUB_ENABLE_HYPHEN_FR "French hyphenation patterns" OFF)
option(REPUB_ENABLE_HYPHEN_ES "Spanish hyphenation patterns" OFF)
option(REPUB_ENABLE_HYPHEN_IT "Italian hyphenation patterns" OFF)
option(REPUB_ENABLE_HYPHEN_RU "Russian hyphenation patterns" OFF)
option(REPUB_ENABLE_VERTICAL_TEXT "CJK vertical writing mode" ON)
option(REPUB_ENABLE_BIDI "RTL reordering" ON)
option(REPUB_ENABLE_TTF "TrueType rasterizer" ON)
option(REPUB_ENABLE_BUILTIN_FONT "Built-in bitmap fallback font" ON)
option(REPUB_ENABLE_TEXT_ANTIALIASING "Antialiased text" ON)
option(REPUB_ENABLE_DITHERING "Image dithering" ON)
option(REPUB_ENABLE_GRAYSCALE "Gray canvases" ON)
option(REPUB_ENABLE_IMAGES "Image decoding" ON)
option(REPUB_ENABLE_IMAGE_ZOOM "Pan/zoom image viewer" ON)
option(REPUB_ENABLE_GIF "GIF decoding" ON)
option(REPUB_ENABLE_WEBP "WebP decoding (external decoder hook)" OFF)
option(REPUB_ENABLE_CACHE "In-memory chapter/layout cache" ON)
option(REPUB_ENABLE_PROGRESS "Byte weighted progress" ON)

# ---------------------------------------------------------------------------
# Feature definitions shared by every target
# ---------------------------------------------------------------------------
set(REPUB_FEATURE_DEFS "")
foreach(opt
        TOC COVER ENCRYPTION_PROBE CSS FOOTNOTES TABLES DROP_CAPS RUBY
        HYPHENATION VERTICAL_TEXT BIDI TTF BUILTIN_FONT TEXT_ANTIALIASING
        DITHERING GRAYSCALE IMAGES IMAGE_ZOOM GIF WEBP CACHE PROGRESS)
    if(REPUB_ENABLE_${opt})
        list(APPEND REPUB_FEATURE_DEFS REPUB_ENABLE_${opt}=1)
    else()
        list(APPEND REPUB_FEATURE_DEFS REPUB_ENABLE_${opt}=0)
    endif()
endforeach()
foreach(lang EN DE FR ES IT RU)
    if(REPUB_ENABLE_HYPHEN_${lang})
        list(APPEND REPUB_FEATURE_DEFS REPUB_HYPH_${lang}=1)
    else()
        list(APPEND REPUB_FEATURE_DEFS REPUB_HYPH_${lang}=0)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE REPUB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(repub_thirdparty OBJECT third_party/miniz.c)
target_compile_definitions(repub_thirdparty PRIVATE
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    MINIZ_NO_ARCHIVE_WRITING_APIS)
if(NOT MSVC)
    target_compile_options(repub_thirdparty PRIVATE -w)
endif()
set_target_properties(repub_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(rolling_epub STATIC ${REPUB_SOURCES} $<TARGET_OBJECTS:repub_thirdparty>)
set_target_properties(rolling_epub PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(RollingEPUB::rolling_epub ALIAS rolling_epub)

target_compile_features(rolling_epub PUBLIC cxx_std_17)
target_compile_definitions(rolling_epub PUBLIC ${REPUB_FEATURE_DEFS})
target_include_directories(rolling_epub
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if(NOT MSVC)
    target_compile_options(rolling_epub PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(REPUB_WERROR)
        target_compile_options(rolling_epub PRIVATE -Werror)
    endif()
endif()

if(REPUB_BUILD_SHARED)
    add_library(rolling_epub_shared SHARED ${REPUB_SOURCES} $<TARGET_OBJECTS:repub_thirdparty>)
    set_target_properties(rolling_epub_shared PROPERTIES OUTPUT_NAME rolling_epub)
    target_compile_features(rolling_epub_shared PUBLIC cxx_std_17)
    target_compile_definitions(rolling_epub_shared PUBLIC ${REPUB_FEATURE_DEFS})
    target_include_directories(rolling_epub_shared
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(REPUB_BUILD_TESTS)
    enable_testing()
    add_library(repub_test_support STATIC tests/test_harness.cpp)
    target_include_directories(repub_test_support PUBLIC tests)
    target_link_libraries(repub_test_support PUBLIC rolling_epub)
    target_compile_features(repub_test_support PUBLIC cxx_std_17)
    target_compile_definitions(repub_test_support PUBLIC
        REPUB_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    file(GLOB REPUB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
    list(REMOVE_ITEM REPUB_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_harness.cpp")
    foreach(test_src ${REPUB_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE repub_test_support)
        target_compile_features(${test_name} PRIVATE cxx_std_17)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(REPUB_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/repub_cli.cpp")
    add_executable(repub_cli tools/repub_cli.cpp)
    target_link_libraries(repub_cli PRIVATE rolling_epub)
    target_compile_features(repub_cli PRIVATE cxx_std_17)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS rolling_epub
        EXPORT RollingEPUBTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/rolling_epub DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT RollingEPUBTargets
        FILE RollingEPUBTargets.cmake
        NAMESPACE RollingEPUB::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RollingEPUB)
