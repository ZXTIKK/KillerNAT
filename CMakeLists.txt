cmake_minimum_required(VERSION 3.15)
project(KillerNAT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

find_package(Boost QUIET)

if(Boost_FOUND)
    set(Boost_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
else()
    include(FetchContent)
    FetchContent_Declare(
      boost
      URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz
    )
    FetchContent_MakeAvailable(boost)
    set(Boost_INCLUDE_DIRS ${boost_SOURCE_DIR})
endif()

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock iphlpapi)
endif()