qt_add_executable(asus_tuf_fa706ihrb_backend_tests test_tuf.cpp)
target_link_libraries(asus_tuf_fa706ihrb_backend_tests PRIVATE ambient_asus_tuf_fa706ihrb Qt6::Test)
add_test(NAME asus_tuf_fa706ihrb_backend_tests COMMAND ${DBUS_RUN_SESSION}
    --config-file=${PROJECT_SOURCE_DIR}/tests/session-bus.conf -- $<TARGET_FILE:asus_tuf_fa706ihrb_backend_tests>)
set_tests_properties(asus_tuf_fa706ihrb_backend_tests PROPERTIES TIMEOUT 60)

qt_add_executable(asus_tuf_fa706ihrb_layout_tests test_layout.cpp)
target_link_libraries(asus_tuf_fa706ihrb_layout_tests PRIVATE ambient_core Qt6::Test)
target_compile_definitions(asus_tuf_fa706ihrb_layout_tests PRIVATE TUF_DRIVER_DIR="${CMAKE_CURRENT_BINARY_DIR}/driver")
add_dependencies(asus_tuf_fa706ihrb_layout_tests asus_tuf_fa706ihrb)
add_custom_command(TARGET asus_tuf_fa706ihrb_layout_tests PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/driver"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:asus_tuf_fa706ihrb>"
        "${CMAKE_CURRENT_BINARY_DIR}/driver/asus_tuf_fa706ihrb.so")
add_test(NAME asus_tuf_fa706ihrb_layout_tests COMMAND asus_tuf_fa706ihrb_layout_tests)
