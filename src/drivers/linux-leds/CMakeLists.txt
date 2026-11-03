qt_add_library(ambient_linux_leds STATIC linux_leds.cpp)
target_include_directories(ambient_linux_leds PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ambient_linux_leds PUBLIC ambient_generic_driver)
target_compile_options(ambient_linux_leds PRIVATE -Wall -Wextra -Wpedantic)
qt_add_plugin(linux_leds SHARED CLASS_NAME LinuxLedsDriver)
target_sources(linux_leds PRIVATE linux_leds_driver.cpp)
target_link_libraries(linux_leds PRIVATE ambient_linux_leds)
set_target_properties(ambient_linux_leds linux_leds PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
set_target_properties(linux_leds PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/ambient-led/drivers/v4")
install(TARGETS linux_leds LIBRARY DESTINATION "${KDE_INSTALL_QTPLUGINDIR}/ambient-led/drivers/v4")
