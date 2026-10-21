# ==============================================================================
# Firmware version generator - single source of truth: the latest <prefix>X.Y.Z git tag
# ==============================================================================
# Runs in CMake script mode on every build (see targets `generate_version` and
# `generate_boot_version` in CMakeLists.txt):
#   cmake -DSOURCE_DIR=<repo> -DGIT_EXECUTABLE=<git> -DVERSION_TEMPLATE=<in> -DVERSION_OUTPUT=<out>
#         [-DTAG_PREFIX=v] -DBUILD_TYPE=<type> [-DHW_TARGET_NAME=... ...] -P cmake/version.cmake
#
# TAG_PREFIX selects the release line: "v" for the Main Application (tags v2.1.0),
# "boot-v" for the Bootloader (tags boot-v1.3.0). The template receives FW_VERSION_MAJOR/
# MINOR/PATCH, FW_VERSION_STRING, GIT_DESCRIBE_STR, GIT_COMMIT_HASH and BUILD_TIMESTAMP_STR.
#
# The output header is rewritten only when the git state (tag, commit, dirty flag) or the
# build configuration changes, so incremental builds do not recompile/relink needlessly.
# The build timestamp is refreshed together with those changes.

if(NOT DEFINED TAG_PREFIX OR TAG_PREFIX STREQUAL "")
  set(TAG_PREFIX "v")
endif()

set(FW_VERSION_MAJOR 0)
set(FW_VERSION_MINOR 0)
set(FW_VERSION_PATCH 0)
set(GIT_DESCRIBE_STR "unknown")
set(GIT_COMMIT_HASH "unknown")

if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
  # safe.directory=* lets git work on the bind-mounted repository inside the Docker container
  set(_git "${GIT_EXECUTABLE}" -c safe.directory=* -C "${SOURCE_DIR}")

  execute_process(
    COMMAND ${_git} describe --tags --match "${TAG_PREFIX}[0-9]*" --abbrev=0
    OUTPUT_VARIABLE _release_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  execute_process(
    COMMAND ${_git} describe --tags --match "${TAG_PREFIX}[0-9]*" --always --dirty
    OUTPUT_VARIABLE GIT_DESCRIBE_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  execute_process(
    COMMAND ${_git} rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  string(REGEX REPLACE "^${TAG_PREFIX}" "" _release_numbers "${_release_tag}")
  if(NOT _release_numbers STREQUAL _release_tag AND _release_numbers MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    set(FW_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(FW_VERSION_MINOR ${CMAKE_MATCH_2})
    set(FW_VERSION_PATCH ${CMAKE_MATCH_3})
  else()
    message(WARNING "version.cmake: no release tag matching ${TAG_PREFIX}X.Y.Z found (got '${_release_tag}'), using v0.0.0")
  endif()
else()
  message(WARNING "version.cmake: git repository not available, using v0.0.0")
endif()

if(GIT_DESCRIBE_STR STREQUAL "")
  set(GIT_DESCRIBE_STR "unknown")
endif()
if(GIT_COMMIT_HASH STREQUAL "")
  set(GIT_COMMIT_HASH "unknown")
endif()

set(FW_VERSION_STRING "v${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.${FW_VERSION_PATCH}")
set(CMAKE_BUILD_TYPE "${BUILD_TYPE}")

file(READ "${VERSION_TEMPLATE}" _template)

# Compare against the existing header with the timestamp masked out
set(BUILD_TIMESTAMP_STR "")
string(CONFIGURE "${_template}" _candidate @ONLY)

set(_existing "")
if(EXISTS "${VERSION_OUTPUT}")
  file(READ "${VERSION_OUTPUT}" _existing)
  string(REGEX REPLACE "(_BUILD_TIMESTAMP +)\"[^\"]*\"" "\\1\"\"" _existing "${_existing}")
endif()

if(NOT _candidate STREQUAL _existing)
  string(TIMESTAMP BUILD_TIMESTAMP_STR "%Y-%m-%d %H:%M:%S UTC" UTC)
  string(CONFIGURE "${_template}" _content @ONLY)
  file(WRITE "${VERSION_OUTPUT}" "${_content}")
  get_filename_component(_header_name "${VERSION_OUTPUT}" NAME)
  message(STATUS "${_header_name}: ${FW_VERSION_STRING} (${GIT_DESCRIBE_STR})")
endif()
