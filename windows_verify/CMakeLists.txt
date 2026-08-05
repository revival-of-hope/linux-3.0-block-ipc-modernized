cmake_minimum_required(VERSION 3.16)
project(linux30_modernization_verify C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(checked_math_test checked_math_test.c)
target_include_directories(checked_math_test PRIVATE ../include)

if(MSVC)
  target_compile_options(checked_math_test PRIVATE /W4 /WX)
else()
  target_compile_options(checked_math_test PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME checked_math COMMAND checked_math_test)
