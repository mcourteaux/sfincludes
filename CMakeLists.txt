cmake_minimum_required(VERSION 3.7)

project("sfincludes")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

find_package(Boost REQUIRED COMPONENTS filesystem program_options)
#find_package(Boost REQUIRED COMPONENTS filesystem)
message("${Boost_LIBRARY_DIRS}")
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
add_executable(sfincludes main.cpp)
target_link_libraries(sfincludes ${Boost_LIBRARIES})

install(TARGETS sfincludes DESTINATION bin)
