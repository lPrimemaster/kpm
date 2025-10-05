cmake_minimum_required(VERSION 3.20)

# Enable vcpkg
if(DEFINED ENV{VCPKG_ROOT})
	message(STATUS "Using vcpkg root.")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(kpm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

option(LTRACE "Enable trace logging." OFF)

if(LTRACE)
	add_definitions(-DLTRACE)
endif()

find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

add_executable(kpm
	main.cpp
	src/kpm_install.cpp
)

target_link_libraries(kpm PRIVATE
	CURL::libcurl
	yaml-cpp::yaml-cpp
	LibArchive::LibArchive	
	CLI11::CLI11
)

add_subdirectory(test)
