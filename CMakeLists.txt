cmake_minimum_required(VERSION 3.21)
project(WorkDeskQt LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(WD_SRC
    src/main.cpp
    src/config.cpp
    src/session.cpp
    src/markdown.cpp
    src/syntaxhl.cpp
    src/messagebubble.cpp
    src/chatpanel.cpp
    src/notepad.cpp
    src/fontcombo.cpp
    src/settingsdialog.cpp
    src/mainwindow.cpp
    src/dockbar.cpp
    src/theme.cpp
    src/frost.cpp
    src/glass.cpp
    src/smoothscroll.cpp
    src/util.cpp
    src/http.cpp
)
set(WD_HDR
    src/config.h
    src/session.h
    src/markdown.h
    src/syntaxhl.h
    src/messagebubble.h
    src/chatpanel.h
    src/notepad.h
    src/fontcombo.h
    src/settingsdialog.h
    src/mainwindow.h
    src/dockbar.h
    src/theme.h
    src/frost.h
    src/glass.h
    src/smoothscroll.h
    src/util.h
    src/http.h
)
add_executable(WorkDeskQt WIN32 ${WD_SRC} ${WD_HDR} assets.qrc workdesk.rc)
target_include_directories(WorkDeskQt PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
# 可选的附加头文件目录（例如自备的第三方库路径）
if(DEFINED WD_EXTRA_INCLUDE AND NOT WD_EXTRA_INCLUDE STREQUAL "")
    target_include_directories(WorkDeskQt PRIVATE "${WD_EXTRA_INCLUDE}")
endif()
target_compile_options(WorkDeskQt PRIVATE /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MP)
target_compile_definitions(WorkDeskQt PRIVATE UNICODE _UNICODE)
target_link_libraries(WorkDeskQt PRIVATE
    Qt6::Widgets Qt6::Network
    winhttp comdlg32 shell32 ole32 oleaut32 user32 gdi32 windowscodecs uuid dwmapi
)
set_target_properties(WorkDeskQt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
