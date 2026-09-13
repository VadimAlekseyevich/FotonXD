cmake_minimum_required(VERSION 3.10)

project(Shilin_3d_task_1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)

set(SOURCES
    main.cpp
    MainWindow.cpp
    CubeWidget.cpp
)

set(HEADERS
    MainWindow.h
    CubeWidget.h
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(${PROJECT_NAME}
    Qt5::Widgets
    Qt5::OpenGL
)
