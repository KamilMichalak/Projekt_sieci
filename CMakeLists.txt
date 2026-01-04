cmake_minimum_required(VERSION 3.10)
project(wisielec_na_wyscigi)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(THREADS REQUIRED)

include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})

# Client
add_executable(client
    client.cpp
)

target_compile_options(client PRIVATE ${GTK3_CFLAGS_OTHER})
target_include_directories(client PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_libraries(client ${GTK3_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

# Server
add_executable(server
    server.cpp
)

target_include_directories(server PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_libraries(server ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS client server RUNTIME DESTINATION bin)