add_compile_definitions(-DUSE_COLOR)

if (USE_PIPES)
    add_executable(client main.c src/pipe.c src/fileshare.c ../utils/src/recvbuf.c)
else()
    add_executable(client main.c src/client.c src/fileshare.c ../utils/src/recvbuf.c)
endif()

target_link_libraries(client ws2_32 pthread -static)