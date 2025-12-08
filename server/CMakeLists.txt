add_executable(server
    main.cpp
    Server.cpp
    Game.cpp
    ../include/SharedMemory.cpp
)

target_link_libraries(server pthread rt)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/include)
