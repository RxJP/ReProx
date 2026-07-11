cmake_minimum_required(VERSION 4.0)
project(ReProx)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_executable(ReProx main.cpp config.cpp)
target_link_libraries(ReProx PRIVATE tomlplusplus::tomlplusplus)
