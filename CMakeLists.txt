cmake_minimum_required(VERSION 3.7)

project("sfincludes")

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS program_options)
message("${Boost_LIBRARY_DIRS}")
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
add_executable(sfincludes main.cpp)
target_link_libraries(sfincludes ${Boost_LIBRARIES})

install(TARGETS sfincludes DESTINATION bin)
