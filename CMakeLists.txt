cmake_minimum_required(VERSION 3.15)
project(AICAD VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL PrintSupport)

# OpenCASCADE
find_package(OpenCASCADE REQUIRED)

# ECL
find_package(ECL)

# 包含目錄
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCASCADE_INCLUDE_DIR}
)

# 原始檔
set(CORE_SOURCES
    src/core/Application.cpp
    src/core/EventBus.cpp
    src/core/PluginManager.cpp
)

set(CORE_HEADERS
    src/core/Application.h
    src/core/EventBus.h
    src/core/PluginManager.h
)

# 執行檔
add_executable(AICAD
    src/main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    # 其他模組的檔案...
)

# 連結函式庫
target_link_libraries(AICAD
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::PrintSupport
    ${OpenCASCADE_LIBRARIES}
)

if(ECL_FOUND)
    target_link_libraries(AICAD ${ECL_LIBRARIES})
    target_compile_definitions(AICAD PRIVATE HAVE_ECL)
endif()

# 測試
enable_testing()

add_executable(test_application
    tests/unit/core/test_application.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_link_libraries(test_application
    Qt5::Test
    Qt5::Core
)

add_test(NAME ApplicationTest COMMAND test_application)
