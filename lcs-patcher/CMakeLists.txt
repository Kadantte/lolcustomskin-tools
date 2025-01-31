cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(lcs-patcher)

add_library(lcs-patcher STATIC
    src/lcs/modoverlay.hpp
    src/lcs/modoverlay_common.cpp
    src/lcs/patcher_utility/ppp.hpp
    src/lcs/patcher_utility/process.hpp
    src/lcs/patcher_utility/lineconfig.hpp
    )
target_include_directories(lcs-patcher PUBLIC src)

if (WIN32)
    target_sources(lcs-patcher PUBLIC
        src/lcs/modoverlay_win32.cpp
        src/lcs/patcher_utility/process_win32.cpp
    )
    target_link_libraries(lcs-patcher PUBLIC psapi)
    target_include_directories(lcs-patcher PUBLIC src/)
elseif (APPLE)
    target_sources(lcs-patcher PUBLIC
        src/lcs/modoverlay_macos.cpp
        src/lcs/patcher_utility/macho.hpp
        src/lcs/patcher_utility/process_macos.cpp
    )
else()
    target_sources(lcs-patcher PUBLIC src/lcs/modoverlay_dummy.cpp)
endif()

add_executable(lolcustomskin src/main.cpp)
target_link_libraries(lolcustomskin PRIVATE lcs-patcher)
