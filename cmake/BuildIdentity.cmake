# SPDX-License-Identifier: GPL-3.0-or-later
# Fingerprint build inputs without requiring Git or a release-version change.
# Reconfigure when an existing input changes, not only when the glob changes.
file(GLOB_RECURSE identity_inputs CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/*" "${PROJECT_SOURCE_DIR}/ui/*"
    "${PROJECT_SOURCE_DIR}/translations/*" "${PROJECT_SOURCE_DIR}/services/*"
    "${PROJECT_SOURCE_DIR}/cmake/*")
# Native driver modules are independently built and installed, not host inputs.
list(FILTER identity_inputs EXCLUDE REGEX "^src/drivers/")
list(APPEND identity_inputs CMakeLists.txt)
list(SORT identity_inputs)
set(identity "${PROJECT_VERSION};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_BUILD_TYPE};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_DEBUG};${CMAKE_CXX_FLAGS_RELEASE};${CMAKE_CXX_FLAGS_RELWITHDEBINFO};${Qt6_VERSION};${KWin_VERSION};${BUILD_KWIN_EFFECT};${CMAKE_INSTALL_PREFIX}")
string(APPEND identity ";${ECM_VERSION};${KF6CoreAddons_VERSION};${KF6Config_VERSION};${CMAKE_SYSTEM_PROCESSOR}")
foreach(input IN LISTS identity_inputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${input}")
    file(SHA256 "${PROJECT_SOURCE_DIR}/${input}" digest)
    string(APPEND identity "\n${input}:${digest}")
endforeach()
string(SHA256 KLIGHTING_BUILD_ID "${identity}")
configure_file(cmake/build_identity.h.in generated/build_identity.h @ONLY)
set(component_metadata "{\"buildId\":\"${KLIGHTING_BUILD_ID}\",\"version\":\"${PROJECT_VERSION}\"}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/klighting-service.json" CONTENT "${component_metadata}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/klighting-service.json"
    DESTINATION "${KDE_INSTALL_DATADIR}/klighting")
if(BUILD_KWIN_EFFECT)
    foreach(component IN ITEMS metadata kwin_integration)
        file(READ "${PROJECT_SOURCE_DIR}/src/${component}.json" metadata)
        string(JSON metadata SET "${metadata}" X-KLighting "${component_metadata}")
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/${component}_build.json"
            CONTENT "${metadata}\n")
    endforeach()
endif()
