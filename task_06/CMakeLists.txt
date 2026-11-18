cmake_minimum_required(VERSION 3.10)

# Имя шестого задания в CMake.
project(Shilin_3d_task_6)

# Требуем C++11 для совместимости с MinGW 4.9.2.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Для интерфейса и рисования нужны Widgets и OpenGL; libtiff не подключается.
find_package(Qt5 COMPONENTS Widgets OpenGL REQUIRED)
# Автоматическая генерация метаобъектного кода для Q_OBJECT.
set(CMAKE_AUTOMOC ON)

# Перечисление cpp-файлов приложения.
set(SOURCES
    main.cpp
    MainWindow.cpp
    EarthWidget.cpp
    GeoTiff.cpp
    Projection.cpp
    GeoReference.cpp
    RasterContrast.cpp
)

# Заголовки с объявлениями классов и функций.
set(HEADERS
    MainWindow.h
    EarthWidget.h
    GeoPicking.h
    GeoTiff.h
    Projection.h
    GeoReference.h
    RasterContrast.h
)

# Создаём исполняемый файл проекта.
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Передаём приложению каталог с исходниками для поиска карты.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FOTON_TASK6_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Линкуем необходимые модули Qt.
target_link_libraries(${PROJECT_NAME}
    Qt5::Widgets
    Qt5::OpenGL
)
