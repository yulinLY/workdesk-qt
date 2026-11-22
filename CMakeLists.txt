cmake_minimum_required(VERSION 3.21)
project(WorkDeskQt LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

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
    src/util.cpp
    src/smoothscroll.cpp
    src/markdowntoolbar.cpp
    src/http.cpp
    src/welcomedialog.cpp
    src/birthdaydialog.cpp
    src/musicplayer.cpp
src/musicprovider.cpp
    src/musicpanel.cpp
    src/overflowbar.cpp
src/tooltip.cpp
    src/musiclibrary.cpp
    src/shellint.cpp
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
    src/util.h
    src/smoothscroll.h
    src/markdowntoolbar.h
    src/http.h
    src/welcomedialog.h
    src/birthdaydialog.h
    src/musicplayer.h
    src/musicpanel.h
src/musicprovider.h
    src/overflowbar.h
src/tooltip.h
    src/musiclibrary.h
    src/shellint.h
)

add_executable(WorkDeskQt WIN32 ${WD_SRC} ${WD_HDR} assets.qrc workdesk.rc)

target_include_directories(WorkDeskQt PRIVATE src "D:/libs/include")
target_compile_options(WorkDeskQt PRIVATE /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MP)
target_compile_definitions(WorkDeskQt PRIVATE UNICODE _UNICODE)

target_link_libraries(WorkDeskQt PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    winhttp comdlg32 shell32 ole32 oleaut32 user32 gdi32 windowscodecs uuid dwmapi
)

set_target_properties(WorkDeskQt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---- 卸载程序：跟主程序放同一个目录，用户哪天不想用了双击它就能卸干净 ----
add_executable(Uninstall WIN32 src/uninstallmain.cpp src/theme.cpp src/shellint.cpp)

target_include_directories(Uninstall PRIVATE src "D:/libs/include")
target_compile_options(Uninstall PRIVATE /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MP)
target_compile_definitions(Uninstall PRIVATE UNICODE _UNICODE)

target_link_libraries(Uninstall PRIVATE
    Qt6::Widgets
    shell32 ole32 user32 gdi32
)

set_target_properties(Uninstall PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_NAME "Uninstall"
)
