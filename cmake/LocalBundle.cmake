# SPDX-License-Identifier: GPL-3.0-or-later
# Use each module's normal install rules; no duplicate artifact registry.
file(REMOVE_RECURSE "${BUNDLE_DIR}")
file(MAKE_DIRECTORY "${BUNDLE_DIR}/root")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${BUNDLE_DIR}/root"
    "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
    COMMAND_ERROR_IS_FATAL ANY)
file(COPY_FILE "${CHECKER}" "${BUNDLE_DIR}/klighting-plugin-check")
string(REGEX REPLACE "^/" "" PLUGIN_DIR "${PLUGIN_DIR}")
string(REGEX REPLACE "^/" "" DATA_DIR "${DATA_DIR}")
file(WRITE "${BUNDLE_DIR}/bundle.json"
    "{\"version\":1,\"plugins\":\"${PLUGIN_DIR}\",\"data\":\"${DATA_DIR}\"}\n")
file(GLOB_RECURSE artifacts RELATIVE "${BUNDLE_DIR}" "${BUNDLE_DIR}/*")
list(SORT artifacts)
set(checksums "")
foreach(artifact IN LISTS artifacts)
    if(IS_SYMLINK "${BUNDLE_DIR}/${artifact}")
        message(FATAL_ERROR "Local bundles require regular files: ${artifact}")
    endif()
    file(SHA256 "${BUNDLE_DIR}/${artifact}" digest)
    string(APPEND checksums "${digest}  ${artifact}\n")
endforeach()
file(WRITE "${BUNDLE_DIR}/SHA256SUMS" "${checksums}")
message(STATUS "Local bundle: ${BUNDLE_DIR}")
