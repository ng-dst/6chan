add_compile_definitions("-DUSE_COLOR")

add_executable(client main.c src/client.c src/fileshare.c ../utils/src/recvbuf.c)

target_link_libraries(client ws2_32 pthread -static)