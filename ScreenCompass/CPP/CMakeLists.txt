cmake_minimum_required(VERSION 3.20)
project(ScreenCompass VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ScreenCompass WIN32
    main.cpp
    resource.rc
)

target_link_libraries(ScreenCompass PRIVATE
    user32
    shell32
    gdi32
    RuntimeObject
)

# Enable WinRT
target_compile_options(ScreenCompass PRIVATE /await)
set_target_properties(ScreenCompass PROPERTIES
    VS_GLOBAL_CppWinRTOptimized true
    VS_GLOBAL_CppWinRTRootNamespaceAutoMerge true
)
