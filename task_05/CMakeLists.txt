cmake_minimum_required(VERSION 3.10)

project(Shilin_geo_task_5)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(TIFF REQUIRED)

include_directories(${TIFF_INCLUDE_DIR})

set(SOURCES
    main.cpp
    GeoTiff.cpp
    Projection.cpp
    GeoReference.cpp
    PointFile.cpp
    RasterContrast.cpp
)

set(HEADERS
    GeoTiff.h
    Projection.h
    GeoReference.h
    PointFile.h
    RasterContrast.h
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    ${TIFF_LIBRARIES}
)
