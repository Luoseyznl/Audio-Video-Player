set(CORE_SOURCES
    audio_renderer.cc
    packet_producer.cc
    decoder.cc
    demuxer.cc
    gl_renderer.cc
    player.cc
    utils/logger.cc
    mediadefs.h # 不参与编译，但方便 IDE 管理
)

add_library(av_player_core STATIC ${CORE_SOURCES})

target_include_directories(av_player_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

target_link_libraries(av_player_core PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::SDL2
    OpenGL::GL
    glfw
    GLEW::GLEW
)
