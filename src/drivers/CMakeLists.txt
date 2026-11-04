# Each module is independently discoverable; adding a driver needs no central list.
set(KLIGHTING_DISABLED_DRIVERS "" CACHE STRING "Driver directory names to exclude (semicolon-separated)")
add_custom_target(klighting_drivers)

function(klighting_register_driver target)
    set_target_properties(${target} PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/ambient-led/drivers/v4")
    install(TARGETS ${target} LIBRARY DESTINATION "${KDE_INSTALL_QTPLUGINDIR}/ambient-led/drivers/v4")
    add_dependencies(klighting_drivers ${target})
endfunction()

# Shared standard-protocol utilities are available before the modules consuming them.
add_subdirectory(generic)
file(GLOB modules CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*/CMakeLists.txt")
list(SORT modules)
foreach(module IN LISTS modules)
    get_filename_component(driver "${module}" DIRECTORY)
    if(NOT driver STREQUAL "generic" AND NOT driver IN_LIST KLIGHTING_DISABLED_DRIVERS)
        add_subdirectory("${driver}")
    endif()
endforeach()
