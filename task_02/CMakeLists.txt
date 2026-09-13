cmake_minimum_required(VERSION 3.16)

project(Shilin_3d_task_2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Gui OpenGL)
find_package(TIFF REQUIRED)

add_executable(${PROJECT_NAME}
    main.cpp
    MainWindow.cpp
    MainWindow.h
    EarthWidget.cpp
    EarthWidget.h
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    FOTON_TASK2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    Qt5::Gui
    Qt5::OpenGL
)

if(TARGET TIFF::TIFF)
    target_link_libraries(${PROJECT_NAME} PRIVATE TIFF::TIFF)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${TIFF_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TIFF_LIBRARIES})
endif()
