cmake_minimum_required(VERSION 3.25)
project(klighting VERSION 0.27.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CTest)
option(BUILD_KWIN_EFFECT "Build the native KWin effect" ON)

find_package(ECM 6.26 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui DBus LinguistTools)
qt_standard_project_setup(REQUIRES 6.10)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_library(ambient_driver_api INTERFACE)
target_include_directories(ambient_driver_api INTERFACE src)
target_link_libraries(ambient_driver_api INTERFACE Qt6::Core Qt6::Gui)

add_library(ambient_core STATIC
    src/device_manager.cpp src/color.cpp src/notification_effect.cpp src/effect_palette.cpp src/keyboard_profile.cpp src/presets.cpp src/preset_runtime.cpp src/driver_loader.cpp)
target_include_directories(ambient_core PUBLIC src)
target_link_libraries(ambient_core PUBLIC ambient_driver_api KF6::ConfigCore)
target_compile_options(ambient_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(ambient_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

# Each driver owns its build, hardware data and translations.
add_subdirectory(src/drivers/asus-g615lw)
add_subdirectory(src/drivers/generic)
add_subdirectory(src/drivers/linux-leds)
add_subdirectory(src/drivers/hid-lamparray)
install(FILES src/device_driver.h DESTINATION "${KDE_INSTALL_INCLUDEDIR}/ambient-led")

add_executable(ambient_probe tools/probe.cpp)
target_link_libraries(ambient_probe PRIVATE ambient_core Qt6::DBus)
add_dependencies(ambient_probe asus_g615lw)
add_dependencies(ambient_probe linux_leds hid_lamparray)

add_library(lighting_runtime STATIC src/lighting_runtime.cpp)
target_link_libraries(lighting_runtime PUBLIC ambient_core)
add_library(lighting_service STATIC
    src/lighting_service.cpp src/desktop_source.h src/kwin_desktop_source.cpp src/kwin_capture_source.cpp
    src/notification_source.cpp src/power_source.cpp)
target_link_libraries(lighting_service PUBLIC lighting_runtime Qt6::DBus)
set_target_properties(lighting_runtime lighting_service PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_options(lighting_runtime PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(lighting_service PRIVATE -Wall -Wextra -Wpedantic)
qt_add_executable(klightingd src/daemon_main.cpp)
target_link_libraries(klightingd PRIVATE lighting_service)
install(TARGETS klightingd RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
configure_file(services/org.klighting.Service.service.in org.klighting.Service.service @ONLY)
configure_file(services/klightingd.service.in klightingd.service @ONLY)
configure_file(services/klightingd.desktop.in klightingd.desktop @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.klighting.Service.service" DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/klightingd.service" DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/klightingd.desktop" DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})

if(BUILD_KWIN_EFFECT)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Quick QuickControls2)
    find_package(KF6Kirigami REQUIRED)
    find_package(KWin 6.7 REQUIRED)
    find_package(KF6KCMUtils REQUIRED)
    find_package(KF6GuiAddons REQUIRED)
    find_package(KF6Service REQUIRED)
    find_package(KF6I18n REQUIRED)
    set(BUILD_SHARED_LIBS ON)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    add_library(daemon_peer STATIC src/daemon_peer.cpp)
    target_link_libraries(daemon_peer PUBLIC Qt6::DBus)
    set_target_properties(daemon_peer PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    kcoreaddons_add_plugin(klighting_integration SOURCES src/kwin_integration.cpp
        INSTALL_NAMESPACE "kwin/plugins")
    target_link_libraries(klighting_integration PRIVATE daemon_peer KWin::kwin)
    set_target_properties(klighting_integration PROPERTIES PREFIX ""
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    kcoreaddons_add_plugin(ambientled
        SOURCES src/main.cpp src/ambient_effect.cpp src/frame_sampler.cpp src/capture_channel.cpp
        INSTALL_NAMESPACE "kwin/effects/plugins")
    set_target_properties(ambientled PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    set_property(TARGET ambientled APPEND PROPERTY AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY_SUPPORTED)
    target_link_libraries(ambientled PRIVATE daemon_peer KWin::kwin)
    target_compile_options(ambientled PRIVATE -Wall -Wextra -Wpedantic)
    add_library(ambient_settings STATIC src/translations.cpp src/settings_controller.cpp src/settings_effects.cpp src/settings_devices.cpp src/effect_control.cpp src/service_control.cpp src/condition_recorder.cpp src/layer_key_tracker.cpp)
    target_link_libraries(ambient_settings PUBLIC ambient_core KF6::I18n KF6::GuiAddons KF6::Service Qt6::Quick Qt6::DBus)
    set_target_properties(ambient_settings PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    target_compile_options(ambient_settings PRIVATE -Wall -Wextra -Wpedantic)
    target_sources(ambient_settings PRIVATE src/ambient_config.cpp)
    target_link_libraries(ambient_settings PUBLIC KF6::KCMUtilsQuick)
    set(AMBIENT_TS_FILES)
    foreach(language IN ITEMS ru fr it es uk pl de zh_CN ar he)
        list(APPEND AMBIENT_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/ambient-led_${language}.ts")
    endforeach()
    qt_add_lrelease(TS_FILES ${AMBIENT_TS_FILES} QM_FILES_OUTPUT_VARIABLE AMBIENT_QM_FILES)
    qt_add_resources(ambient_settings ambient_translations
        PREFIX "/ambient-led/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${AMBIENT_QM_FILES})
    file(GLOB AMBIENT_TRANSLATABLE_UI CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.qml")
    qt_add_lupdate(SOURCE_TARGETS ambient_settings ambient_core
        SOURCES ${AMBIENT_TRANSLATABLE_UI} TS_FILES ${AMBIENT_TS_FILES}
        OPTIONS -no-obsolete -locations relative)
    set(AMBIENT_PLUGIN kcm_ambientled)
    set(AMBIENT_METADATA "${CMAKE_CURRENT_SOURCE_DIR}/kcm_ambientled.json")
    configure_file(src/config_plugin.cpp.in kcm_ambientled.cpp @ONLY)
    kcmutils_add_qml_kcm(kcm_ambientled SOURCES "${CMAKE_CURRENT_BINARY_DIR}/kcm_ambientled.cpp")
    set_target_properties(kcm_ambientled PROPERTIES PREFIX "")
    target_link_libraries(kcm_ambientled PRIVATE ambient_settings)

    # Native subcategories put the section navigation outside the module footer.
    # The editor and controller sources are shared with the standalone host.
    file(GLOB AMBIENT_UI CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.qml")
    list(REMOVE_ITEM AMBIENT_UI "${CMAKE_CURRENT_SOURCE_DIR}/ui/main.qml")
    foreach(section IN ITEMS devices presets effects applications)
        set(AMBIENT_PLUGIN "kcm_ambientled_${section}")
        set(AMBIENT_METADATA "${CMAKE_CURRENT_SOURCE_DIR}/${AMBIENT_PLUGIN}.json")
        string(SUBSTRING "${section}" 0 1 initial)
        string(TOUPPER "${initial}" initial)
        string(SUBSTRING "${section}" 1 -1 remaining)
        set(AMBIENT_PAGE "${initial}${remaining}Page")
        configure_file(src/config_plugin.cpp.in "${AMBIENT_PLUGIN}.cpp" @ONLY)
        configure_file(src/config_section.qml.in "sections/${section}/main.qml" @ONLY)
        kcoreaddons_add_plugin(${AMBIENT_PLUGIN}
            SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${AMBIENT_PLUGIN}.cpp"
            INSTALL_NAMESPACE "plasma/kcms/systemsettings")
        set_target_properties(${AMBIENT_PLUGIN} PROPERTIES PREFIX "")
        target_link_libraries(${AMBIENT_PLUGIN} PRIVATE ambient_settings)
        kcmutils_generate_desktop_file(${AMBIENT_PLUGIN})
        qt_add_resources(${AMBIENT_PLUGIN} "${section}_ui"
            PREFIX "/kcm/${AMBIENT_PLUGIN}" BASE "${CMAKE_CURRENT_SOURCE_DIR}/ui" FILES ${AMBIENT_UI})
        qt_add_resources(${AMBIENT_PLUGIN} "${section}_main"
            PREFIX "/kcm/${AMBIENT_PLUGIN}" BASE "${CMAKE_CURRENT_BINARY_DIR}/sections/${section}"
            FILES "${CMAKE_CURRENT_BINARY_DIR}/sections/${section}/main.qml")
    endforeach()
    install(FILES settings-ambientled.desktop DESTINATION "${KDE_INSTALL_DATADIR}/systemsettings/categories")
    install(FILES org.ambientled.Lighting.desktop DESTINATION ${KDE_INSTALL_APPDIR})
    message(STATUS "Building for KWin ${KWin_VERSION}; rebuild after KWin updates")
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(generic_drivers_tests tests/test_generic_drivers.cpp)
    target_link_libraries(generic_drivers_tests PRIVATE ambient_core ambient_linux_leds ambient_hid_lamparray Qt6::Test)
    add_test(NAME generic_drivers_tests COMMAND generic_drivers_tests)
    qt_add_plugin(alternative_driver_fixture SHARED CLASS_NAME AlternativeDriver tests/fixtures/alternative_driver.cpp)
    target_link_libraries(alternative_driver_fixture PRIVATE ambient_driver_api)
    set_target_properties(alternative_driver_fixture PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-drivers/alternative")
    add_library(config_driver_fixture MODULE tests/fixtures/config_driver.cpp)
    target_link_libraries(config_driver_fixture PRIVATE ambient_asus_g615lw)
    set_target_properties(config_driver_fixture PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-drivers/config")
    add_library(incompatible_driver_fixture MODULE tests/fixtures/incompatible_driver.cpp)
    target_link_libraries(incompatible_driver_fixture PRIVATE Qt6::Core)
    set_target_properties(incompatible_driver_fixture PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-drivers/incompatible")
    add_library(multi_driver_fixture MODULE tests/fixtures/multi_driver.cpp)
    target_link_libraries(multi_driver_fixture PRIVATE ambient_driver_api)
    set_target_properties(multi_driver_fixture PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-drivers/multi")
    add_executable(drivers_tests tests/test_drivers.cpp)
    target_link_libraries(drivers_tests PRIVATE ambient_core Qt6::Test)
    target_compile_definitions(drivers_tests PRIVATE
        ASUS_DRIVER_DIR="${CMAKE_BINARY_DIR}/test-drivers/asus"
        ALTERNATIVE_DRIVER_DIR="$<TARGET_FILE_DIR:alternative_driver_fixture>"
        MULTI_DRIVER_DIR="$<TARGET_FILE_DIR:multi_driver_fixture>"
        CONFIG_DRIVER_DIR="$<TARGET_FILE_DIR:config_driver_fixture>"
        INCOMPATIBLE_DRIVER_DIR="$<TARGET_FILE_DIR:incompatible_driver_fixture>")
    add_dependencies(drivers_tests multi_driver_fixture asus_g615lw config_driver_fixture incompatible_driver_fixture)
    add_dependencies(drivers_tests alternative_driver_fixture)
    add_custom_command(TARGET drivers_tests PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/test-drivers/asus"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:asus_g615lw>"
            "${CMAKE_BINARY_DIR}/test-drivers/asus/asus_g615lw.so")
    add_test(NAME drivers_tests COMMAND drivers_tests)
    add_executable(presets_tests tests/test_presets.cpp)
    target_link_libraries(presets_tests PRIVATE ambient_core Qt6::Test)
    add_test(NAME presets_tests COMMAND presets_tests)
    add_executable(ambient_tests tests/test_ambient.cpp)
    target_link_libraries(ambient_tests PRIVATE ambient_core ambient_asus_g615lw Qt6::Test)
    target_compile_definitions(ambient_tests PRIVATE
        LAMP_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/g615lw-lamps.json")
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    qt_add_executable(lighting_service_tests tests/test_lighting_service.cpp)
    target_link_libraries(lighting_service_tests PRIVATE lighting_service Qt6::Test)
    target_compile_definitions(lighting_service_tests PRIVATE
        MULTI_DRIVER_DIR="$<TARGET_FILE_DIR:multi_driver_fixture>" DAEMON_PATH="$<TARGET_FILE:klightingd>")
    add_dependencies(lighting_service_tests multi_driver_fixture klightingd)
    add_test(NAME lighting_service_tests COMMAND ${DBUS_RUN_SESSION}
        --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:lighting_service_tests>)
    set_tests_properties(lighting_service_tests PROPERTIES TIMEOUT 45)
    add_test(NAME ambient_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:ambient_tests>)
    set_tests_properties(ambient_tests PROPERTIES TIMEOUT 30)
    add_executable(notifications_tests tests/test_notifications.cpp src/notification_source.cpp)
    target_link_libraries(notifications_tests PRIVATE ambient_core Qt6::DBus Qt6::Test)
    add_test(NAME notifications_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:notifications_tests>)
    set_tests_properties(notifications_tests PROPERTIES TIMEOUT 30)
    add_executable(power_tests tests/test_power.cpp src/power_source.cpp)
    target_link_libraries(power_tests PRIVATE ambient_core Qt6::DBus Qt6::Test)
    add_test(NAME power_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:power_tests>)
    set_tests_properties(power_tests PROPERTIES TIMEOUT 30)
    if(BUILD_KWIN_EFFECT)
        find_package(Qt6 REQUIRED COMPONENTS QuickWidgets)
        qt_add_executable(capture_ipc_tests tests/test_capture_ipc.cpp src/capture_channel.cpp)
        target_include_directories(capture_ipc_tests PRIVATE src)
        target_link_libraries(capture_ipc_tests PRIVATE daemon_peer ambient_driver_api Qt6::Test)
        add_test(NAME capture_ipc_tests COMMAND ${DBUS_RUN_SESSION}
            --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:capture_ipc_tests>)
        set_tests_properties(capture_ipc_tests PROPERTIES TIMEOUT 15)
        add_executable(keyboard_editor_tests tests/test_keyboard_editor.cpp)
        target_link_libraries(keyboard_editor_tests PRIVATE Qt6::Quick Qt6::Widgets Qt6::Test)
        target_compile_definitions(keyboard_editor_tests PRIVATE UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui")
        add_test(NAME keyboard_editor_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:keyboard_editor_tests>)
        add_executable(layer_conditions_tests tests/test_layer_conditions.cpp)
        target_link_libraries(layer_conditions_tests PRIVATE ambient_settings Qt6::QuickWidgets Qt6::Widgets Qt6::Test)
        add_test(NAME layer_conditions_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:layer_conditions_tests>)
        add_executable(capture_geometry_tests tests/test_capture_geometry.cpp)
        target_link_libraries(capture_geometry_tests PRIVATE ambient_core KWin::kwin Qt6::Test)
        add_test(NAME capture_geometry_tests COMMAND capture_geometry_tests)
        add_executable(quick_tests tests/test_quick.cpp)
        target_link_libraries(quick_tests PRIVATE ambient_settings KF6::KCMUtils KF6::KCMUtilsQuick Qt6::QuickWidgets Qt6::Widgets Qt6::Test)
        target_compile_definitions(quick_tests PRIVATE CONFIG_PLUGIN_PATH="$<TARGET_FILE:kcm_ambientled>"
            CONFIG_SECTION_DIR="$<TARGET_FILE_DIR:kcm_ambientled_devices>")
        add_dependencies(quick_tests kcm_ambientled config_driver_fixture
            kcm_ambientled_devices kcm_ambientled_presets kcm_ambientled_effects kcm_ambientled_applications)
        add_test(NAME quick_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:quick_tests>)
        set_tests_properties(quick_tests PROPERTIES TIMEOUT 60
            ENVIRONMENT "AMBIENT_LED_DRIVER_PATH=$<TARGET_FILE_DIR:config_driver_fixture>")
        add_executable(translations_tests tests/test_translations.cpp)
        target_link_libraries(translations_tests PRIVATE
            ambient_settings KF6::KCMUtils Qt6::QuickWidgets Qt6::Widgets Qt6::Test)
        target_compile_definitions(translations_tests PRIVATE
            SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            CONFIG_PLUGIN_PATH="$<TARGET_FILE:kcm_ambientled_devices>"
            ASUS_PLUGIN_PATH="$<TARGET_FILE:asus_g615lw>")
        add_dependencies(translations_tests kcm_ambientled_devices asus_g615lw config_driver_fixture)
        add_test(NAME translations_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:translations_tests>)
        set_tests_properties(translations_tests PROPERTIES TIMEOUT 60
            ENVIRONMENT "AMBIENT_LED_DRIVER_PATH=$<TARGET_FILE_DIR:config_driver_fixture>")
        add_executable(config_tests tests/test_config.cpp)
        target_link_libraries(config_tests PRIVATE
            ambient_settings ambient_asus_g615lw KF6::KCMUtilsQuick Qt6::Widgets Qt6::Test)
        target_compile_definitions(config_tests PRIVATE
            MULTI_DRIVER_DIR="$<TARGET_FILE_DIR:multi_driver_fixture>"
            ALTERNATIVE_DRIVER_DIR="$<TARGET_FILE_DIR:alternative_driver_fixture>"
            CONFIG_PLUGIN_PATH="$<TARGET_FILE:kcm_ambientled>"
            EFFECT_PLUGIN_PATH="$<TARGET_FILE:ambientled>")
        add_dependencies(config_tests multi_driver_fixture kcm_ambientled ambientled config_driver_fixture)
        add_dependencies(config_tests alternative_driver_fixture)
        add_test(NAME config_tests COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:config_tests>)
        set_tests_properties(config_tests PROPERTIES TIMEOUT 30
            ENVIRONMENT "AMBIENT_LED_DRIVER_PATH=$<TARGET_FILE_DIR:config_driver_fixture>")
    endif()
endif()

# Keep existing English UI assertions independent of the developer's desktop locale.
if(BUILD_TESTING)
    get_property(AMBIENT_TESTS DIRECTORY PROPERTY TESTS)
    set_property(TEST ${AMBIENT_TESTS} APPEND PROPERTY ENVIRONMENT "LANGUAGE=en_US")
endif()

# Optional developer tools; normal builds do not require Python or formatters.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(lint
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
endif()
