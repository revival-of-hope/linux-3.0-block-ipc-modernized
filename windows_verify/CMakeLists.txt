cmake_minimum_required(VERSION 3.16)
project(linux30_modernization_verify C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

function(add_policy_test target source)
  add_executable(${target} ${source})
  target_include_directories(${target} PRIVATE ../include ../block)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_policy_test(checked_math_test checked_math_test.c)
add_policy_test(allocation_policy_test allocation_policy_test.c)
add_policy_test(look_policy_test look_policy_test.c)
