SET(EXAMPLE_NAME "Vulkan-glTF-PBR")
file(GLOB SOURCE *.cpp)
file(GLOB SHADERS "../data/shaders/*.vert" "../data/shaders/*.frag" "../data/shaders/*.comp" "../data/shaders/*.geom" "../data/shaders/*.tesc" "../data/shaders/*.tese")
file(GLOB SHADER_INCLUDES "../data/shaders/includes/*.glsl")

source_group("Shaders" FILES ${SHADERS})
source_group("Shaders\\Includes" FILES ${SHADER_INCLUDES})

# Добавляем заголовочные файлы
file(GLOB HEADERS "*.h")
source_group("Header Files" FILES ${HEADERS})

if(WIN32)
    add_executable(${EXAMPLE_NAME} WIN32 ${SOURCE} ${HEADERS} ${SHADERS} ${SHADER_INCLUDES})
    target_link_libraries(${EXAMPLE_NAME} base ${Vulkan_LIBRARY} ${WINLIBS})
else(WIN32)
    add_executable(${EXAMPLE_NAME} ${SOURCE} ${HEADERS} ${SHADERS} ${SHADER_INCLUDES})
    target_link_libraries(${EXAMPLE_NAME} base)
endif(WIN32)

if(RESOURCE_INSTALL_DIR)
    install(TARGETS ${EXAMPLE_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
