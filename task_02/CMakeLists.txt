cmake_minimum_required(VERSION 3.10)

project(Shilin_3d_task_2)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)

set(SOURCES
    main.cpp
    MainWindow.cpp
    EarthWidget.cpp
)

set(HEADERS
    MainWindow.h
    EarthWidget.h
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    FOTON_TASK2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME}
    Qt5::Widgets
    Qt5::OpenGL
)
