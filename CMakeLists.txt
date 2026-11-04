cmake_minimum_required(VERSION 3.16)
project(obmen CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8 /EHsc /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif ()

add_executable(obmen
    src/main.cpp
    src/app.cpp
    src/net.cpp
    src/store.cpp
    src/http.cpp
    src/discovery.cpp
    src/sync.cpp
    src/ftp.cpp
    src/gui_assets.cpp
    src/ui.cpp
)

if (WIN32)
    enable_language(RC)
    target_sources(obmen PRIVATE res/obmen.rc)
    target_include_directories(obmen PRIVATE third_party/webview2/include)
    if (MSVC)
        target_link_options(obmen PRIVATE /MANIFEST:EMBED)
    endif ()
    target_link_libraries(obmen PRIVATE
        ws2_32
        shell32
        ole32
        oleaut32
        user32
        gdi32
        advapi32
        windowsapp
        "${CMAKE_SOURCE_DIR}/third_party/webview2/x64/WebView2LoaderStatic.lib"
    )
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(obmen PRIVATE Threads::Threads)
endif ()

install(TARGETS obmen RUNTIME DESTINATION bin)
