# Library install test to package

add_executable(rprint rprint.cpp)
add_library(librp STATIC rprint_lib.cpp)

install(TARGETS rprint DESTINATION bin)
install(TARGETS librp DESTINATION lib)
install(FILES rprint.h DESTINATION include)
