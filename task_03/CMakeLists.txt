cmake_minimum_required(VERSION 3.16)
project(Shilin_3d_task_3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets OpenGL)

add_executable(${PROJECT_NAME}
    main.cpp
    MainWindow.cpp
    MainWindow.h
    EarthWidget.cpp
    EarthWidget.h
    GeoPicking.h
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FOTON_TASK3_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt5::Widgets Qt5::OpenGL)

include(CTest)
if(BUILD_TESTING)
    add_executable(GeoPickingTest tests/GeoPickingTest.cpp GeoPicking.h)
    target_include_directories(GeoPickingTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(GeoPickingTest PRIVATE Qt5::Gui)
    add_test(NAME GeoPicking COMMAND GeoPickingTest)
endif()
