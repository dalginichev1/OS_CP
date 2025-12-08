#pragma once
#include "../include/SharedTypes.hpp"
#include <string>
#include <vector>

class Game {
public:
    Game(const std::string& name, const std::string& creator, SharedMemoryRoot* root, bool is_public = false);
    ~Game();
    
    bool join(const std::string& player2);
    bool place_ship(const std::string& player, uint8_t size, uint8_t x, uint8_t y, bool horizontal);
    bool make_shot(const std::string& shooter, uint8_t x, uint8_t y);
    bool is_setup_complete(const std::string& player) const;
    void set_setup_complete(const std::string& player);
    bool is_game_active() const;
    bool is_game_finished() const;
    std::string get_winner() const;
    std::string get_current_turn() const;
    std::string get_status() const;
    
    std::string get_player_board(const std::string& player, bool show_ships = true) const;
    std::string get_opponent_view(const std::string& player) const;
    std::string get_statistics(const std::string& player) const;
    
    int get_id() const;
    std::string get_game_name() const { return std::string(game_data->game_name); }
    std::string get_player1() const { return std::string(game_data->player1); }
    std::string get_player2() const { return std::string(game_data->player2); }
    bool is_public() const { return game_data->is_public; }
    bool is_waiting() const { return game_data->state == GAME_WAITING; }
    
    bool has_player(const std::string& player) const;
    bool is_player_turn(const std::string& player) const;
    
private:
    int game_id;
    SharedMemoryRoot* root;
    GameData* game_data;
    
    bool can_place_ship(uint8_t size, uint8_t x, uint8_t y, bool horizontal, 
                       CellState board[BOARD_SIZE][BOARD_SIZE]) const;
    void place_ship_on_board(uint8_t size, uint8_t x, uint8_t y, bool horizontal,
                            CellState board[BOARD_SIZE][BOARD_SIZE], Ship* ship_array, uint8_t& ship_count);
    bool check_hit(uint8_t x, uint8_t y, CellState board[BOARD_SIZE][BOARD_SIZE], 
                  Ship* ships, uint8_t ship_count, bool& sunk, uint8_t& sunk_ship_index);
    bool check_game_over() const;
    void switch_turn();
    
    std::string board_to_string(CellState board[BOARD_SIZE][BOARD_SIZE], bool show_ships) const;
};