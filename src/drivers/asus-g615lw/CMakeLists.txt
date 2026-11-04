find_package(PkgConfig REQUIRED)
pkg_check_modules(ASUS_G615LW_USB REQUIRED IMPORTED_TARGET libusb-1.0)
# Hardware code has no dependency on the editor, presets, KDE, or KWin.
add_library(ambient_asus_g615lw STATIC
    aura_protocol.cpp asusd_client.cpp
    lamp_array.cpp led_transport.h
    g615lw_layout.cpp asus_controls.cpp)
target_include_directories(ambient_asus_g615lw PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ambient_asus_g615lw PUBLIC ambient_driver_api Qt6::DBus PkgConfig::ASUS_G615LW_USB)
target_compile_options(ambient_asus_g615lw PRIVATE -Wall -Wextra -Wpedantic)
add_library(asus_g615lw MODULE asus_driver.cpp asus_translations.cpp)
set_property(SOURCE asus_driver.cpp APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/asus_driver.json")
target_link_libraries(asus_g615lw PRIVATE ambient_asus_g615lw)
set_target_properties(ambient_asus_g615lw asus_g615lw PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
klighting_register_driver(asus_g615lw)

# Driver-owned catalogs and resources never enter the settings/core catalogs.
set(ASUS_TS_FILES)
foreach(language IN ITEMS ru fr it es uk pl de zh_CN ar he)
    list(APPEND ASUS_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/asus-g615lw_${language}.ts")
endforeach()
qt_add_lrelease(TS_FILES ${ASUS_TS_FILES} QM_FILES_OUTPUT_VARIABLE ASUS_QM_FILES)
qt_add_resources(asus_g615lw asus_g615lw_translations
    PREFIX "/ambient-led/drivers/asus-g615lw/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${ASUS_QM_FILES})
qt_add_lupdate(SOURCE_TARGETS ambient_asus_g615lw asus_g615lw TS_FILES ${ASUS_TS_FILES}
    OPTIONS -no-obsolete -locations relative)

install(FILES install/legacy-plugins.txt DESTINATION "${KDE_INSTALL_DATADIR}/klighting/drivers/asus-g615lw")
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
