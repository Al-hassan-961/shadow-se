cmake_minimum_required(VERSION 3.16)
project(shadowse VERSION 1.0.0 LANGUAGES CXX
        DESCRIPTION "Shadow SE - secure clearweb & .onion search engine core")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHADOWSE_BUILD_TESTS "Build the unit test suite" ON)
option(SHADOWSE_USE_CURL "Use libcurl for real network fetching when available" ON)
option(SHADOWSE_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)
find_package(CURL QUIET)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(shadowse_core STATIC
  src/tokenizer.cpp
  src/inverted_index.cpp
  src/bm25.cpp
  src/tor_proxy.cpp
  src/fetcher.cpp
  src/crawler.cpp
  src/engine.cpp
)
target_include_directories(shadowse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shadowse_core PUBLIC Threads::Threads)
target_compile_features(shadowse_core PUBLIC cxx_std_20)

if(CURL_FOUND AND SHADOWSE_USE_CURL)
  target_link_libraries(shadowse_core PUBLIC CURL::libcurl)
  target_compile_definitions(shadowse_core PUBLIC HAVE_CURL)
  message(STATUS "Shadow SE: libcurl enabled - real HTTP/HTTPS/onion fetching")
else()
  message(STATUS "Shadow SE: libcurl not found or disabled - stub fetcher only")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(shadowse_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SHADOWSE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Terminal application
# ---------------------------------------------------------------------------
add_executable(shadow-se src/main.cpp)
target_link_libraries(shadow-se PRIVATE shadowse_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SHADOWSE_BUILD_TESTS)
  enable_testing()
  add_executable(shadow-se-tests
    tests/test_main.cpp
    tests/test_tokenizer.cpp
    tests/test_index.cpp
    tests/test_bm25.cpp
    tests/test_crawler.cpp
    tests/test_tor.cpp
    tests/test_engine.cpp
  )
  target_link_libraries(shadow-se-tests PRIVATE shadowse_core)
  add_test(NAME shadowse-tests COMMAND shadow-se-tests)
endif()
