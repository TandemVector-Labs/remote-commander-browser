cmake_minimum_required(VERSION 3.21)
project(chatgpt_cef_v2 LANGUAGES C CXX)

# Headless companion contract tests do not load CEF or launch a browser.
option(CGWA_COMPANION_TESTS_ONLY "Build CEF-free companion contract tests only" OFF)
if(CGWA_COMPANION_TESTS_ONLY)
  enable_testing()
  set(COMPANION_CONTRACT_SRCS src/commander_companion.cc src/commander_companion_test.cc)
  if(WIN32)
    list(APPEND COMPANION_CONTRACT_SRCS src/windows_private_file.cc src/windows_private_file.h)
  endif()
  add_executable(companion-contract-test ${COMPANION_CONTRACT_SRCS})
  target_compile_features(companion-contract-test PRIVATE cxx_std_17)
  if(WIN32)
    target_link_libraries(companion-contract-test PRIVATE Advapi32)
  endif()
  add_test(NAME companion-contract COMMAND companion-contract-test --fixture "${CMAKE_CURRENT_SOURCE_DIR}/src/companion-emitted-fixture.json")
  add_executable(tab-order-contract-test src/ui_tab_order_test.cc)
  target_compile_features(tab-order-contract-test PRIVATE cxx_std_17)
  add_test(NAME tab-order-contract COMMAND tab-order-contract-test)
  return()
endif()

if(NOT CEF_ROOT)
  if(DEFINED ENV{CEF_ROOT})
    set(CEF_ROOT "$ENV{CEF_ROOT}")
  else()
    message(FATAL_ERROR "Set CEF_ROOT to the extracted CEF binary distribution")
  endif()
endif()
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

set(APP_SRCS
  src/main.cc
  src/app.cc
  src/app.h
  src/controller.cc
  src/controller.h
  src/client.cc
  src/client.h
  src/commander_companion.cc
  src/commander_companion.h
)
if(WIN32)
  enable_language(RC)
  list(APPEND APP_SRCS src/windows_private_file.cc src/windows_private_file.h src/windows-app.rc)
endif()

if(WIN32)
  add_executable(chatgpt-cef-v2 WIN32 ${APP_SRCS})
else()
  add_executable(chatgpt-cef-v2 ${APP_SRCS})
endif()
SET_EXECUTABLE_TARGET_PROPERTIES(chatgpt-cef-v2)
target_compile_features(chatgpt-cef-v2 PRIVATE cxx_std_17)
target_include_directories(chatgpt-cef-v2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CEF_ROOT}")
target_link_libraries(chatgpt-cef-v2 PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
if(WIN32)
  target_link_libraries(chatgpt-cef-v2 PRIVATE Shell32 Advapi32)
  add_executable(windows-private-file-test
    src/windows_private_file.cc
    src/windows_private_file.h
    src/windows_private_file_test.cc)
  target_compile_features(windows-private-file-test PRIVATE cxx_std_17)
  target_include_directories(windows-private-file-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(windows-private-file-test PRIVATE Advapi32)
  set_target_properties(windows-private-file-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()

set(APP_OUT_DIR "${CMAKE_BINARY_DIR}/bin")
set_target_properties(chatgpt-cef-v2 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${APP_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${APP_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${APP_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${APP_OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${APP_OUT_DIR}"
)
if(UNIX AND NOT APPLE)
  set_target_properties(chatgpt-cef-v2 PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()

COPY_FILES("chatgpt-cef-v2" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${APP_OUT_DIR}")
COPY_FILES("chatgpt-cef-v2" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${APP_OUT_DIR}")
if(OS_LINUX)
  SET_LINUX_SUID_PERMISSIONS("chatgpt-cef-v2" "${APP_OUT_DIR}/chrome-sandbox")
endif()
