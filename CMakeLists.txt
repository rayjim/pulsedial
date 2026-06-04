cmake_minimum_required(VERSION 3.21)

project(PulseDial LANGUAGES CXX)

add_executable(PulseDial WIN32
    src/main.cpp
)

target_compile_features(PulseDial PRIVATE cxx_std_20)

target_compile_definitions(PulseDial PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(PulseDial PRIVATE
    d2d1
    dwrite
    dwmapi
    user32
    gdi32
    shell32
    ole32
)
