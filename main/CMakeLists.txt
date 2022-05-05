set(COMPONENT_SRCS "weather.cpp" )
set(COMPONENT_ADD_INCLUDEDIRS "")

idf_component_register(SRCS "weather.cpp" "io_data.cpp" "io_data.h" "secrets.h", "Arduino.h")
# register_component()