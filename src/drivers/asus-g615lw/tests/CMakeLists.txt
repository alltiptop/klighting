qt_add_executable(asus_g615lw_transport_tests test_transport.cpp)
target_link_libraries(asus_g615lw_transport_tests PRIVATE ambient_core ambient_asus_g615lw Qt6::Test)
target_compile_definitions(asus_g615lw_transport_tests PRIVATE
    LAMP_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/g615lw-lamps.json")
add_test(NAME asus_g615lw_transport_tests COMMAND ${DBUS_RUN_SESSION}
    --config-file=${PROJECT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:asus_g615lw_transport_tests>)
set_tests_properties(asus_g615lw_transport_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "LANGUAGE=en_US")

qt_add_executable(asus_g615lw_output_tests test_output.cpp)
target_link_libraries(asus_g615lw_output_tests PRIVATE ambient_asus_g615lw Qt6::Test)
add_test(NAME asus_g615lw_output_tests COMMAND asus_g615lw_output_tests)

qt_add_executable(asus_g615lw_output_benchmark benchmark_output.cpp)
target_link_libraries(asus_g615lw_output_benchmark PRIVATE ambient_asus_g615lw component_info)
target_compile_definitions(asus_g615lw_output_benchmark PRIVATE BUILD_TYPE="$<CONFIG>")

qt_add_executable(asus_g615lw_layout_tests test_layout.cpp)
target_link_libraries(asus_g615lw_layout_tests PRIVATE ambient_core Qt6::Test)
target_compile_definitions(asus_g615lw_layout_tests PRIVATE ASUS_DRIVER_DIR="${CMAKE_CURRENT_BINARY_DIR}/driver")
add_dependencies(asus_g615lw_layout_tests asus_g615lw)
add_custom_command(TARGET asus_g615lw_layout_tests PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/driver"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:asus_g615lw>"
        "${CMAKE_CURRENT_BINARY_DIR}/driver/asus_g615lw.so")
add_test(NAME asus_g615lw_layout_tests COMMAND asus_g615lw_layout_tests)
set_tests_properties(asus_g615lw_layout_tests PROPERTIES ENVIRONMENT "LANGUAGE=en_US")

if(BUILD_KWIN_EFFECT)
    qt_add_plugin(asus_g615lw_config_fixture SHARED CLASS_NAME ConfigDriver config_driver.cpp)
    set_target_properties(asus_g615lw_config_fixture PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
    target_link_libraries(asus_g615lw_config_fixture PRIVATE ambient_asus_g615lw)
    qt_add_executable(asus_g615lw_controls_tests test_controls.cpp)
    target_link_libraries(asus_g615lw_controls_tests PRIVATE ambient_settings ambient_asus_g615lw
        settings_test_support Qt6::Widgets Qt6::Test)
    add_dependencies(asus_g615lw_controls_tests asus_g615lw_config_fixture)
    add_test(NAME asus_g615lw_controls_tests COMMAND ${DBUS_RUN_SESSION}
        --config-file=${PROJECT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:asus_g615lw_controls_tests>)
    set_tests_properties(asus_g615lw_controls_tests PROPERTIES TIMEOUT 30
        ENVIRONMENT "AMBIENT_LED_DRIVER_PATH=$<TARGET_FILE_DIR:asus_g615lw_config_fixture>;LANGUAGE=en_US")
endif()

qt_add_executable(asus_g615lw_translations_tests test_translations.cpp)
target_link_libraries(asus_g615lw_translations_tests PRIVATE ambient_driver_api Qt6::Test)
target_compile_definitions(asus_g615lw_translations_tests PRIVATE
    DRIVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    ASUS_PLUGIN_PATH="$<TARGET_FILE:asus_g615lw>")
add_dependencies(asus_g615lw_translations_tests asus_g615lw)
add_test(NAME asus_g615lw_translations_tests COMMAND asus_g615lw_translations_tests)
set_tests_properties(asus_g615lw_translations_tests PROPERTIES TIMEOUT 30)
if(BUILD_KWIN_EFFECT)
    target_compile_definitions(asus_g615lw_translations_tests PRIVATE
        HOST_TRANSLATIONS_DIR="${PROJECT_BINARY_DIR}" HOST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_dependencies(asus_g615lw_translations_tests ambient_settings)
endif()
