cmake_minimum_required(VERSION 3.20)
project(jyotish_cpp VERSION 0.1.0 LANGUAGES CXX)

# Fix CMake policy for nlohmann_json - must be before FetchContent
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----- Build options -----
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# ----- Find dependencies -----
# Swiss Ephemeris (system package or local)
find_library(SWISSEPH_LIB swe
    PATHS /usr/lib /usr/local/lib /opt/homebrew/lib ${CMAKE_SOURCE_DIR}/../.local/lib
    PATH_SUFFIXES ""
    NO_DEFAULT_PATH
)
find_path(SWISSEPH_INCLUDE_DIR swephexp.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include ${CMAKE_SOURCE_DIR}/../.local/include
    NO_DEFAULT_PATH
)
if(SWISSEPH_LIB AND SWISSEPH_INCLUDE_DIR)
    message(STATUS "Found Swiss Ephemeris: ${SWISSEPH_LIB}")
    set(USE_REAL_SWISSEPH 1)
else()
    message(WARNING "Swiss Ephemeris not found. Using mock implementation.")
    set(USE_REAL_SWISSEPH 0)
endif()

# Threads (for async)
find_package(Threads REQUIRED)

# SQLite (store: coins/sessions/admin/logs)
find_library(SQLITE3_LIB sqlite3)
find_path(SQLITE3_INCLUDE_DIR sqlite3.h)

# ----- Compiler warnings -----
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wformat=2
    )
elseif(MSVC)
    add_compile_options(/W4 /permissive- /std:c++20)
endif()

# cpp-httplib caps form-urlencoded POST bodies at an 8 KiB compile-time macro.
# Our /api/oracle sends the whole growing chat history as one form body, so a
# session past ~8 KiB of text silently broke with 413 (surfaced as our generic
# 404). Raise the bar so a long conversation keeps working.
add_compile_definitions(CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH=8388608)

# nlohmann/json (FetchContent)
include(FetchContent)
# Use newer version compatible with CMake 3.20+
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
# Set policy before making available
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib (FetchContent)
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.0
)
FetchContent_MakeAvailable(cpp-httplib)

# fmt (for logging)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# ----- Core library -----
add_library(jyotish_core
    src/core/chart.cpp
    src/core/dasha.cpp
    src/core/transits.cpp
    src/core/forecast.cpp
    src/core/store.cpp
    src/core/yogas.cpp
    src/core/geocode.cpp
    src/core/json_serialization.cpp
    src/core/config.cpp
    src/core/i18n.cpp
    src/core/chart_formatter.cpp
    src/core/utf8.cpp
    src/predictions.cpp
    src/news.cpp
    src/oracle/parsing.cpp
    src/oracle/onboarding.cpp
    src/oracle/context_builder.cpp
    src/oracle/quote_pool.cpp
    src/oracle/prompt_templates.cpp
    src/oracle/oracle.cpp
    src/oracle/wisdom.cpp
    src/oracle/grounding.cpp
    src/oracle/famous.cpp
    src/oracle/research.cpp
    src/oracle/theory.cpp
    src/oracle/feedback.cpp
)

target_include_directories(jyotish_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(SWISSEPH_INCLUDE_DIR)
    target_include_directories(jyotish_core PRIVATE ${SWISSEPH_INCLUDE_DIR})
endif()

if(SQLITE3_INCLUDE_DIR)
    target_include_directories(jyotish_core PRIVATE ${SQLITE3_INCLUDE_DIR})
endif()

if(cpp-httplib_SOURCE_DIR)
    target_include_directories(jyotish_core PRIVATE ${cpp-httplib_SOURCE_DIR})
endif()

target_link_libraries(jyotish_core PUBLIC
    Threads::Threads
    nlohmann_json::nlohmann_json
    fmt::fmt
    httplib::httplib
)

if(SWISSEPH_LIB)
    target_link_libraries(jyotish_core PRIVATE ${SWISSEPH_LIB})
endif()

if(SQLITE3_LIB)
    target_link_libraries(jyotish_core PUBLIC ${SQLITE3_LIB})
endif()

if(USE_REAL_SWISSEPH)
    target_compile_definitions(jyotish_core PRIVATE -DJYOTISH_REAL_SWISSEPH)
else()
    target_compile_definitions(jyotish_core PRIVATE -DJYOTISH_MOCK_SWISSEPH)
endif()

# Make the geocoding dataset available next to the built binaries.
set(JYOTISH_CITIES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/data/cities.tsv")
if(EXISTS "${JYOTISH_CITIES_FILE}")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/cities.tsv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${JYOTISH_CITIES_FILE}" "${CMAKE_BINARY_DIR}/cities.tsv"
        DEPENDS "${JYOTISH_CITIES_FILE}"
        COMMENT "Copying cities.tsv to build directory")
    add_custom_target(copy_cities ALL DEPENDS "${CMAKE_BINARY_DIR}/cities.tsv")
    add_dependencies(jyotish_core copy_cities)
else()
    message(WARNING "data/cities.tsv not found; geocoder will use the built-in city list")
endif()

# ----- Main executable (test) -----
add_executable(jyotish_test src/main.cpp)
target_link_libraries(jyotish_test PRIVATE jyotish_core httplib::httplib)

# ----- HTTP server executable -----
add_executable(jyotish_server
    src/http/server.cpp
    src/http/handlers.cpp
    src/ollama/client.cpp
    src/main_server.cpp
)

target_link_libraries(jyotish_server PRIVATE
    jyotish_core
    httplib::httplib
)

# ----- IPC bridge (file watcher) -----
add_executable(jyotish_ipc
    src/ipc/bridge.cpp
    src/ipc/watcher.cpp
)

target_link_libraries(jyotish_ipc PRIVATE
    jyotish_core
)

# ----- Tests -----
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    
    add_executable(jyotish_tests
        tests/test_chart.cpp
        tests/test_dasha.cpp
        tests/test_geocode.cpp
        tests/test_store.cpp
        tests/test_onboarding.cpp
        tests/test_feedback.cpp
        tests/test_regressions.cpp
    )
    
    target_link_libraries(jyotish_tests PRIVATE
        jyotish_core
        GTest::gtest
        GTest::gtest_main
    )

    if(SQLITE3_INCLUDE_DIR)
        target_include_directories(jyotish_tests PRIVATE ${SQLITE3_INCLUDE_DIR})
    endif()

    add_test(NAME jyotish_unit_tests COMMAND jyotish_tests)
endif()

# ----- Install -----
install(TARGETS jyotish_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/jyotish/ DESTINATION include/jyotish)
install(TARGETS jyotish_server jyotish_ipc RUNTIME DESTINATION bin)