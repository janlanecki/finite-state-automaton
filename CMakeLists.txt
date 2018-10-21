cmake_minimum_required (VERSION 3.1)
project (AUTOMATON)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
  # invoke built-in add_executable
  _add_executable(${ARGV})
  if (TARGET ${_name})
    target_link_libraries(${_name} rt)
  endif()
endmacro()

add_executable(tester tester.c helper.c)
add_executable(validator validator.c helper.c)
add_executable(run run.c helper.c)

install(TARGETS DESTINATION .)
