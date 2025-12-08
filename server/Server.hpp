#pragma once
#include "../include/SharedTypes.hpp"
#include "../include/SharedMemory.hpp"
#include "Game.hpp"
#include <string>
#include <vector>
#include <unordered_map>

class Server {
public:
    Server();
    ~Server();
    void run();

private:
    SharedMemory shm;
    SharedMemoryRoot* root;
    bool setup_done;
    
    std::unordered_map<int, Game*> games_map;
    
    void init_shared_objects();
    void handle_message(const Message &m);
    void send_response_to(const char* login, const char* text);
    
    ClientSlot* find_or_create_client(const char* login);
    ClientSlot* find_client(const char* login);
    std::vector<std::string> list_clients();
    std::vector<std::string> list_available_games();
    
    int create_private_game(const std::string& creator, const std::string& target);
    int create_public_game(const std::string& game_name, const std::string& creator);
    Game* find_game_by_name(const std::string& game_name);
    Game* get_game(int game_id);
    void remove_game(int game_id);
    
    void handle_setup_complete(const Message &m);
    void handle_place_ship(const Message &m);
    void handle_get_board(const Message &m);
    void handle_get_opponent_board(const Message &m);
    void handle_surrender(const Message &m);
    void handle_game_status(const Message &m);
    
    bool parse_ship_placement(const std::string& payload, uint8_t& size, 
                             uint8_t& x, uint8_t& y, bool& horizontal);
    bool parse_shot(const std::string& payload, uint8_t& x, uint8_t& y);
};