cmake_minimum_required(VERSION 3.10)
project(PrimeCounter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(prime_counter prime_counter.c)
target_compile_options(prime_counter PRIVATE -Wall -Wextra)
target_link_libraries(prime_counter PRIVATE Threads::Threads)

# Copy numbers.txt next to the executable so ./prime_counter finds it
configure_file(${CMAKE_SOURCE_DIR}/numbers.txt
               ${CMAKE_BINARY_DIR}/numbers.txt COPYONLY)
