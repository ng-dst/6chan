add_compile_definitions("-DSERVER")

add_executable(server main.c src/controller.c src/service.c src/model.c ../utils/src/recvbuf.c)
target_link_libraries(server list ws2_32 pthread -static)
