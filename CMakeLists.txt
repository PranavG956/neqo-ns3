# neqo (Rust) is reached through the neqo-ns3-bridge C library.
set(NEQO_BRIDGE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/bridge"
    CACHE PATH "Path of the neqo-ns3-bridge crate (needs include/ and target/debug/)"
)

if(NOT EXISTS "${NEQO_BRIDGE_PREFIX}/target/debug/libneqo_ns3_bridge.so")
  message(STATUS "Building neqo-ns3-bridge with cargo (first time can take 15-30 min)")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env NSS_NO_PKG_CONFIG=1 cargo build
    WORKING_DIRECTORY "${NEQO_BRIDGE_PREFIX}"
    RESULT_VARIABLE neqo_cargo_rc
  )
  if(NOT neqo_cargo_rc EQUAL 0)
    message(${HIGHLIGHTED_STATUS} "Skipping contrib/neqo: cargo build of the bridge failed")
    return()
  endif()
endif()

find_external_library(
  DEPENDENCY_NAME neqo_bridge
  HEADER_NAME neqo_ns3_bridge.h
  LIBRARY_NAME neqo_ns3_bridge
  SEARCH_PATHS ${NEQO_BRIDGE_PREFIX}
  PATH_SUFFIXES target/debug
)

if(NOT neqo_bridge_FOUND)
  message(${HIGHLIGHTED_STATUS}
          "Skipping contrib/neqo: neqo_ns3_bridge not found (set -DNEQO_BRIDGE_PREFIX=...)"
  )
  return()
endif()

build_lib(
  LIBNAME neqo
  SOURCE_FILES
    helper/neqo-helper.cc
    model/neqo-client-app.cc
    model/neqo-server-app.cc
    model/neqo-link-test.cc
  HEADER_FILES
    helper/neqo-helper.h
    model/neqo-client-app.h
    model/neqo-server-app.h
    model/neqo-link-test.h
  LIBRARIES_TO_LINK ${libinternet} ${libcore} ${neqo_bridge_LIBRARIES}
)

target_include_directories(neqo PRIVATE ${neqo_bridge_INCLUDE_DIRS})
