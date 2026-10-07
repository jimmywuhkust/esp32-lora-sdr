idf_build_get_property(component_targets __COMPONENT_TARGETS)
foreach(component_target ${component_targets})
    __component_get_property(component_name ${component_target} COMPONENT_NAME)
    if(component_name MATCHES "^framework-arduinoespressif32")
        set(arduino_component ${component_name})
    endif()
endforeach()
if(NOT arduino_component)
    message(FATAL_ERROR "Build this sketch with framework = arduino, espidf")
endif()
idf_component_register(SRCS main.cpp INCLUDE_DIRS "."
    REQUIRES idf ${arduino_component})
# Arduino's PUBLIC architecture definitions must also reach the radio
# component: this is an Arduino sketch using the native acquisition backend.
idf_component_get_property(radio_lib idf COMPONENT_LIB)
cmake_policy(SET CMP0079 NEW)
target_link_libraries(${radio_lib} PUBLIC idf::${arduino_component})
