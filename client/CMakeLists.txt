add_executable(client
    main.cpp
    Client.cpp
    ../include/SharedMemory.cpp
)

target_link_libraries(client pthread rt)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/include)
