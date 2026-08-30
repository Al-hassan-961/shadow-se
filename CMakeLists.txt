cmake_minimum_required(VERSION 3.16)
project(shadowse VERSION 1.2.0 LANGUAGES CXX
        DESCRIPTION "Shadow SE - secure clearweb & .onion search engine core")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHADOWSE_BUILD_TESTS "Build the unit test suite" ON)
option(SHADOWSE_USE_CURL "Use libcurl for real network fetching when available" ON)
option(SHADOWSE_ENABLE_CRYPTO "Enable libsodium authenticated encryption" ON)
option(SHADOWSE_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)
find_package(CURL QUIET)

# libsodium for XChaCha20-Poly1305 + Argon2id authenticated encryption.
set(SODIUM_FOUND FALSE)
if(SHADOWSE_ENABLE_CRYPTO)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSODIUM QUIET IMPORTED_TARGET libsodium)
  endif()
  if(LIBSODIUM_FOUND)
    set(SODIUM_FOUND TRUE)
  else()
    find_library(SODIUM_LIBRARY NAMES sodium)
    find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
    if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
      set(SODIUM_FOUND TRUE)
    endif()
  endif()
endif()

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
  src/crypto.cpp
  src/snapshot.cpp
  src/web_server.cpp
  src/admin.cpp
  src/json.cpp
  src/bloom_filter.cpp
  src/robots_txt.cpp
  src/tor_proxy_manager.cpp
  src/tor_proxy_pool.cpp
  src/gateway.cpp
)
target_include_directories(shadowse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Vendored header-only libraries (cpp-httplib) as a system include so their
# warnings are suppressed.
target_include_directories(shadowse_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(shadowse_core PUBLIC Threads::Threads)
target_compile_features(shadowse_core PUBLIC cxx_std_20)

if(CURL_FOUND AND SHADOWSE_USE_CURL)
  target_link_libraries(shadowse_core PUBLIC CURL::libcurl)
  target_compile_definitions(shadowse_core PUBLIC HAVE_CURL)
  message(STATUS "Shadow SE: libcurl enabled - real HTTP/HTTPS/onion fetching")
else()
  message(STATUS "Shadow SE: libcurl not found or disabled - stub fetcher only")
endif()

if(SODIUM_FOUND)
  if(TARGET PkgConfig::LIBSODIUM)
    target_link_libraries(shadowse_core PUBLIC PkgConfig::LIBSODIUM)
  else()
    target_include_directories(shadowse_core PUBLIC ${SODIUM_INCLUDE_DIR})
    target_link_libraries(shadowse_core PUBLIC ${SODIUM_LIBRARY})
  endif()
  target_compile_definitions(shadowse_core PUBLIC HAVE_SODIUM)
  message(STATUS "Shadow SE: libsodium enabled - XChaCha20-Poly1305 + Argon2id")
else()
  message(STATUS "Shadow SE: libsodium not found - encryption unavailable")
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
# Privacy-hardened web front end (served through the onion service)
# ---------------------------------------------------------------------------
add_executable(shadow-se-web src/web_main.cpp)
target_link_libraries(shadow-se-web PRIVATE shadowse_core)

# ---------------------------------------------------------------------------
# Local admin-only dashboard (live site status + activity feed)
# ---------------------------------------------------------------------------
add_executable(shadow-se-admin src/admin_main.cpp)
target_link_libraries(shadow-se-admin PRIVATE shadowse_core)

# ---------------------------------------------------------------------------
# HTTP/JSON gateway (/search, /crawl, /status)
# ---------------------------------------------------------------------------
add_executable(shadow-se-gateway src/gateway_main.cpp)
target_link_libraries(shadow-se-gateway PRIVATE shadowse_core)

# ---------------------------------------------------------------------------
# Stealth onion client-authorization key generator
# ---------------------------------------------------------------------------
add_executable(se-keygen src/se_keygen.cpp)
target_link_libraries(se-keygen PRIVATE shadowse_core)

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
    tests/test_crypto.cpp
    tests/test_snapshot.cpp
    tests/test_web.cpp
    tests/test_admin.cpp
    tests/test_json.cpp
    tests/test_persistence.cpp
    tests/test_html_parser.cpp
    tests/test_bloom.cpp
    tests/test_robots.cpp
    tests/test_frontier.cpp
    tests/test_gateway.cpp
    tests/test_utf8.cpp
    tests/test_bm25_edges.cpp
    tests/test_socks5.cpp
    tests/test_tor_manager.cpp
    tests/test_tor_pool.cpp
  )
  target_link_libraries(shadow-se-tests PRIVATE shadowse_core)
  add_test(NAME shadowse-tests COMMAND shadow-se-tests)
endif()
