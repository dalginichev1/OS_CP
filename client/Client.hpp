#pragma once
#include "../include/SharedTypes.hpp"
#include "../include/SharedMemory.hpp"
#include <string>
#include <random>

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

    std::mt19937 rng;
    
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
    void clear_response_buffer();

    void auto_place_ships();
    bool try_place_ship_auto(uint8_t size, std::vector<std::pair<uint8_t, uint8_t>>& placed_positions);
    bool is_valid_position(uint8_t x, uint8_t y, uint8_t size, bool horizontal, const std::vector<std::pair<uint8_t, uint8_t>>& placed_positions);
    void force_clear_response();
    bool has_only_one_player() const;
    bool is_player_in_game(const std::string& player) const;
    void remove_player(const std::string& player);
    void force_check_state();
};