qt_add_library(ambient_hid_lamparray STATIC lamp_protocol.cpp usb_lamps.cpp)
target_include_directories(ambient_hid_lamparray PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ambient_hid_lamparray PUBLIC ambient_generic_driver PRIVATE PkgConfig::LIBUSB)
target_compile_options(ambient_hid_lamparray PRIVATE -Wall -Wextra -Wpedantic)
qt_add_plugin(hid_lamparray SHARED CLASS_NAME HidLampArrayDriver)
target_sources(hid_lamparray PRIVATE hid_lamparray_driver.cpp)
target_link_libraries(hid_lamparray PRIVATE ambient_hid_lamparray)
set_target_properties(ambient_hid_lamparray hid_lamparray PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
set_target_properties(hid_lamparray PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/ambient-led/drivers/v4")
install(TARGETS hid_lamparray LIBRARY DESTINATION "${KDE_INSTALL_QTPLUGINDIR}/ambient-led/drivers/v4")
