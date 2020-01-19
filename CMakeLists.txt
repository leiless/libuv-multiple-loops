cmake_minimum_required(VERSION 3.15)
project(libuv-multiple-loops C)

set(CMAKE_C_STANDARD 99)

find_library(UV uv)

add_compile_options(-Wall -Wextra -Werror -Wno-unused-function)
add_compile_options(-DDEBUG -O0 -g)

add_executable(
        libuv-multiple-loops
        libuv-multiple-loops.c
)

target_link_libraries(${PROJECT_NAME} "${UV}")

