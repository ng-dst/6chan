add_compile_definitions(-DSERVER)

if (USE_PIPES)
    add_executable(server main.c src/pipe.c src/service.c src/model.c ../utils/src/recvbuf.c)
else()
    add_executable(server main.c src/controller.c src/service.c src/model.c ../utils/src/recvbuf.c)
endif()
target_link_libraries(server list ws2_32 pthread -static)
