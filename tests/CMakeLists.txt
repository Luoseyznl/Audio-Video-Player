set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
include(FetchContent)
fetchcontent_declare(
    googletest
    # URL file:///home/gew/Downloads/googletest-main.zip
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
fetchcontent_makeavailable(googletest)

set(TEST_SOURCES
    demuxer_test.cc
    decoder_test.cc
)

add_executable(av_player_tests ${TEST_SOURCES})

target_link_libraries(av_player_tests PRIVATE
    av_player_core
    GTest::gtest_main
)
# 注册到 CTest，替代 add_test
include(GoogleTest)
gtest_discover_tests(av_player_tests)
