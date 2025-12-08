#pragma once
#include "../include/SharedTypes.hpp"
#include "../include/SharedMemory.hpp"
#include "Game.hpp"
#include <string>
#include <vector>
#include <optional>

class Server {
public:
    Server();
    ~Server();
    void run();

private:
    SharedMemory shm;
    SharedMemoryRoot* root;
    bool setup_done;

    // helpers
    void init_shared_objects();
    void handle_message(const Message &m);
    void send_response_to(const char* login, const char* text);
    ClientSlot* find_or_create_client(const char* login);
    ClientSlot* find_client(const char* login);
    std::vector<std::string> list_clients();
    std::vector<Game> games;
};
