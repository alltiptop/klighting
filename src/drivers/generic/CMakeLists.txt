qt_add_library(ambient_generic_driver STATIC driver_support.cpp)
target_include_directories(ambient_generic_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ambient_generic_driver PUBLIC ambient_driver_api)
target_compile_options(ambient_generic_driver PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(ambient_generic_driver PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
