#pragma once
#include "../include/SharedTypes.hpp"
#include "../include/SharedMemory.hpp"
#include <string>

class Client {
public:
    Client();
    ~Client();

    void run();

private:
    SharedMemory shm;
    SharedMemoryRoot* root;
    std::string login;
    int current_game_id;
    bool in_game;
    bool in_setup;

    bool setup_show_menu;
    
    std::string pending_invite_game_name;
    std::string pending_invite_from;
    int pending_invite_id;

    bool enqueue_message(const Message& m);
    bool wait_for_response(std::string &out, int timeout_ms = 1000);
    ClientSlot* my_slot();
    bool check_for_async_messages();
    void handle_game_response(const std::string& response);
    
    void show_main_menu();
    void show_game_menu();
    void place_ships_interactive();
    void show_game_status();
};