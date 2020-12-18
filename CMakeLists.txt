cmake_minimum_required(VERSION 3.5)
project(ppstep)

find_package(Boost COMPONENTS system filesystem program_options thread wave REQUIRED)

file(GLOB_RECURSE sources src/*.cpp src/*.hpp)
file(GLOB_RECURSE external_sources external/*.cpp external/*.hpp external/*.c external/*.h)

add_executable(ppstep ${sources} ${external_sources})

target_include_directories(ppstep PUBLIC src external)

target_compile_options(ppstep PUBLIC -std=c++17)

target_link_libraries(ppstep PUBLIC ${Boost_LIBRARIES})

install(TARGETS ppstep DESTINATION bin)
