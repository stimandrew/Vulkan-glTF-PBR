cmake_minimum_required(VERSION 3.10.0 FATAL_ERROR)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(NAME Vulkan-glTF-PBR)

project(${NAME})

include_directories(external)
include_directories(external/glm)
include_directories(external/gli)
include_directories(external/imgui)
include_directories(external/tinygltf)
include_directories(external/basisu/transcoder)
include_directories(external/basisu/zstd)
include_directories(base)

OPTION(USE_D2D_WSI "Build the project using Direct to Display swapchain" OFF)
OPTION(USE_WAYLAND_WSI "Build the project using Wayland swapchain" OFF)

set(RESOURCE_INSTALL_DIR "" CACHE PATH "Path to install resources to (leave empty for running uninstalled)")

# Enable Draco mesh compression if source and library are present
IF(EXISTS ${CMAKE_SOURCE_DIR}/external/draco/draco_features.h)
  message(STATUS "Draco source and library found at " ${CMAKE_SOURCE_DIR}/external/draco)
  add_definitions(-DTINYGLTF_ENABLE_DRACO)
  include_directories(external/draco)
  find_library(DRACO_DECODER_LIBRARY NAMES dracodec PATHS ${CMAKE_SOURCE_DIR}/libs/draco REQUIRED)
  IF (EXISTS ${DRACO_DECODER_LIBRARY})
        message(STATUS "Draco decoder library found at " ${DRACO_DECODER_LIBRARY})
        message(STATUS "Draco mesh compression enabled")
  ELSE()
    message(FATAL_ERROR "Draco decoder library not found!")
  ENDIF()
ENDIF()

message(STATUS "Using module to find Vulkan")
find_package(Vulkan)

IF(UNIX AND NOT APPLE)
    set(LINUX TRUE)
ENDIF()

IF(WIN32)
        IF (NOT Vulkan_FOUND)
                find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
                IF (Vulkan_LIBRARY)
                        set(Vulkan_FOUND ON)
                        MESSAGE("Using bundled Vulkan library version")
                ENDIF()
        ENDIF()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ELSEIF(LINUX)
        IF (NOT Vulkan_FOUND)
                find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
                IF (Vulkan_LIBRARY)
                        set(Vulkan_FOUND ON)
                        MESSAGE("Using bundled Vulkan library version")
                ENDIF()
        ENDIF()
        find_package(Threads REQUIRED)
        IF(USE_D2D_WSI)
                MESSAGE("Using direct to display extension...")
                add_definitions(-D_DIRECT2DISPLAY)
        ELSEIF(USE_WAYLAND_WSI)
                message("Using wayland extension...")
                find_package(Wayland REQUIRED)
                if (NOT WAYLAND_FOUND)
                        message(FATAL_ERROR "Wayland development package not found")
                endif ()
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
                include_directories(${WAYLAND_INCLUDE_DIR})
        ELSE(USE_D2D_WSI)
                message("Using XCB extension...")
                find_package(XCB REQUIRED)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
                include_directories(${XCB_INCLUDE_DIR})
        ENDIF(USE_D2D_WSI)
ELSEIF(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_MACOS_MVK -DVK_EXAMPLE_XCODE_GENERATED")
        # Todo : android?
ENDIF(WIN32)

IF (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
        message(STATUS ${Vulkan_LIBRARY})
ENDIF()

# Set preprocessor defines
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")

# Clang specific stuff
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif()

add_definitions(-D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== НАЧАЛО: Добавление компиляции шейдеров и копирования данных =====

# Поиск компилятора шейдеров
find_program(GLSLC_EXECUTABLE NAMES glslc glslangValidator)
if(NOT GLSLC_EXECUTABLE)
    message(WARNING "glslc or glslangValidator not found! Shaders will not be compiled automatically.")
    set(SKIP_SHADER_COMPILATION TRUE)
else()
    message(STATUS "Found shader compiler: ${GLSLC_EXECUTABLE}")
    set(SKIP_SHADER_COMPILATION FALSE)
endif()

# Настройка путей для шейдеров
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/data/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/compiled_shaders)

# Создание директорий для скомпилированных шейдеров
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# Функция для компиляции шейдера
function(compile_shader SHADER_PATH)
    get_filename_component(SHADER_FULL_NAME ${SHADER_PATH} NAME)
    set(OUTPUT_FILE ${SHADER_BINARY_DIR}/${SHADER_FULL_NAME}.spv)
    set(FINAL_OUTPUT ${SHADER_SOURCE_DIR}/${SHADER_FULL_NAME}.spv)

    if(EXISTS ${SHADER_PATH})
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${GLSLC_EXECUTABLE} -V -o ${OUTPUT_FILE} ${SHADER_PATH}
            DEPENDS ${SHADER_PATH}
            COMMENT "Compiling ${SHADER_FULL_NAME} -> ${SHADER_FULL_NAME}.spv"
            VERBATIM
        )

        # Копируем в директорию с исходниками
        add_custom_command(
            OUTPUT ${FINAL_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E copy ${OUTPUT_FILE} ${FINAL_OUTPUT}
            DEPENDS ${OUTPUT_FILE}
            COMMENT "Copying ${SHADER_FULL_NAME}.spv to data/shaders/"
            VERBATIM
        )

        list(APPEND COMPILED_SHADERS ${FINAL_OUTPUT})
        set(COMPILED_SHADERS ${COMPILED_SHADERS} PARENT_SCOPE)
    endif()
endfunction()

# Автоматическое обнаружение всех исходных шейдеров
if(NOT SKIP_SHADER_COMPILATION)
    file(GLOB_RECURSE SHADER_SOURCES
        "${SHADER_SOURCE_DIR}/*.vert"
        "${SHADER_SOURCE_DIR}/*.frag"
        "${SHADER_SOURCE_DIR}/*.comp"
        "${SHADER_SOURCE_DIR}/*.geom"
        "${SHADER_SOURCE_DIR}/*.tesc"
        "${SHADER_SOURCE_DIR}/*.tese"
        "${SHADER_SOURCE_DIR}/*.rgen"
        "${SHADER_SOURCE_DIR}/*.rchit"
        "${SHADER_SOURCE_DIR}/*.rmiss"
        "${SHADER_SOURCE_DIR}/ui_background.vert"
        "${SHADER_SOURCE_DIR}/ui_background.frag"
    )

    # Компилируем все найденные шейдеры
    foreach(SHADER_PATH ${SHADER_SOURCES})
        compile_shader(${SHADER_PATH})
    endforeach()

    # Создаем цель для компиляции всех шейдеров
    if(COMPILED_SHADERS)
        add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})
        message(STATUS "Added compile_shaders target with ${COMPILED_SHADERS} shaders")
    endif()
endif()

# Определяем путь для копирования данных
set(DATA_OUTPUT_DIR "${CMAKE_BINARY_DIR}")

# Создаем директорию для данных и копируем
add_custom_command(
    OUTPUT ${DATA_OUTPUT_DIR}/data_copied.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DATA_OUTPUT_DIR}/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${DATA_OUTPUT_DIR}/data"
    COMMAND ${CMAKE_COMMAND} -E touch "${DATA_OUTPUT_DIR}/data_copied.stamp"
    DEPENDS compile_shaders
    COMMENT "Copying data directory to ${DATA_OUTPUT_DIR}/data"
    VERBATIM
)

# Создаем цель для копирования данных
add_custom_target(copy_data ALL
    DEPENDS "${DATA_OUTPUT_DIR}/data_copied.stamp"
)

message(STATUS "Data will be copied to: ${DATA_OUTPUT_DIR}/data")

# ===== КОНЕЦ: Добавление компиляции шейдеров и копирования данных =====

# Не дублируем SOURCE здесь, так как будем использовать из поддиректорий
if(RESOURCE_INSTALL_DIR)
        add_definitions(-DVK_EXAMPLE_DATA_DIR=\"${RESOURCE_INSTALL_DIR}/\")
        install(DIRECTORY data/ DESTINATION ${RESOURCE_INSTALL_DIR}/)
else()
        add_definitions(-DVK_EXAMPLE_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data/\")
endif()

# Compiler specific stuff
IF(MSVC)
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
ELSEIF(APPLE)
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -ObjC++")
        ELSE()
                SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -xobjective-c++")
        ENDIF()
ENDIF(MSVC)

IF(WIN32)
        # Nothing here (yet)
ELSEIF(APPLE)
        link_libraries(${Vulkan_LIBRARY} "-framework AppKit" "-framework QuartzCore")
ELSE(WIN32)
        link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
ENDIF(WIN32)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

add_subdirectory(base)
add_subdirectory(src)

# Добавляем зависимости для основной цели
if(TARGET ${NAME})
    if(TARGET compile_shaders)
        add_dependencies(${NAME} compile_shaders)
    endif()
    if(TARGET copy_data)
        add_dependencies(${NAME} copy_data)
    endif()
endif()
