#include "Game.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

Game::Game(const std::string& name, const std::string& creator, SharedMemoryRoot* root, bool is_public)
    : root(root) {
    
    game_id = -1;
    for (int i = 0; i < 16; i++) {
        if (!root->games[i].used) {
            game_id = i;
            game_data = &root->games[i];
            break;
        }
    }
    
    if (game_id == -1) {
        throw std::runtime_error("No free game slots");
    }
    
    std::memset(game_data, 0, sizeof(GameData));
    game_data->used = true;
    std::strncpy(game_data->game_name, name.c_str(), LOGIN_MAX - 1);
    std::strncpy(game_data->player1, creator.c_str(), LOGIN_MAX - 1);
    game_data->state = GAME_WAITING;
    game_data->is_public = is_public;
    game_data->ship_count1 = 0;
    game_data->ship_count2 = 0;
    game_data->hits1 = game_data->hits2 = 0;
    game_data->misses1 = game_data->misses2 = 0;
    game_data->sunk1 = game_data->sunk2 = 0;
    
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            game_data->board1[i][j] = CELL_EMPTY;
            game_data->board2[i][j] = CELL_EMPTY;
        }
    }
}

bool Game::has_only_one_player() const {
    return (game_data->player1[0] != '\0' && game_data->player2[0] == '\0') ||
           (game_data->player1[0] == '\0' && game_data->player2[0] != '\0');
}

bool Game::is_player_in_game(const std::string& player) const {
    return player == std::string(game_data->player1) || 
           player == std::string(game_data->player2);
}

void Game::remove_player(const std::string& player) {
    if (player == std::string(game_data->player1)) {
        // Удаляем первого игрока
        game_data->player1[0] = '\0';
        game_data->ship_count1 = 0;
        game_data->hits1 = game_data->misses1 = game_data->sunk1 = 0;
        
        // Очищаем поле первого игрока
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                game_data->board1[i][j] = CELL_EMPTY;
            }
        }
        
        // Очищаем корабли первого игрока
        for (int i = 0; i < 10; i++) {
            game_data->ships1[i] = Ship();
        }
        
        // Если игра была активной, переводим в ожидание
        if (game_data->state == GAME_ACTIVE || game_data->state == GAME_SETUP) {
            game_data->state = GAME_WAITING;
        }
    } 
    else if (player == std::string(game_data->player2)) {
        // Удаляем второго игрока
        game_data->player2[0] = '\0';
        game_data->ship_count2 = 0;
        game_data->hits2 = game_data->misses2 = game_data->sunk2 = 0;
        
        // Очищаем поле второго игрока
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                game_data->board2[i][j] = CELL_EMPTY;
            }
        }
        
        // Очищаем корабли второго игрока
        for (int i = 0; i < 10; i++) {
            game_data->ships2[i] = Ship();
        }
        
        // Если игра была активной, переводим в ожидание
        if (game_data->state == GAME_ACTIVE || game_data->state == GAME_SETUP) {
            game_data->state = GAME_WAITING;
        }
    }
    
    // Если остался только один игрок, сбрасываем состояние
    if (has_only_one_player()) {
        game_data->current_turn[0] = '\0';
    }
    
    // Если не осталось игроков, помечаем игру как неиспользуемую
    if (game_data->player1[0] == '\0' && game_data->player2[0] == '\0') {
        game_data->used = false;
    }
}

bool Game::is_empty() const { 
    return game_data->player1[0] == '\0' && game_data->player2[0] == '\0'; 
}

bool Game::is_full() const { 
    return game_data->player1[0] != '\0' && game_data->player2[0] != '\0'; 
}

int Game::get_player_count() const {
    int count = 0;
    if (game_data->player1[0] != '\0') count++;
    if (game_data->player2[0] != '\0') count++;
    return count;
}

Game::~Game() {
    if (game_data) {
        game_data->used = false;
    }
}

int Game::get_id() const {
    return game_id;
}

bool Game::join(const std::string& player2) {
    // Проверяем, не является ли игрок уже участником
    if (std::string(game_data->player1) == player2 || 
        std::string(game_data->player2) == player2) {
        return false;
    }
    
    // Определяем, в какое место поставить игрока
    if (game_data->player1[0] == '\0') {
        std::strncpy(game_data->player1, player2.c_str(), LOGIN_MAX - 1);
    } else if (game_data->player2[0] == '\0') {
        std::strncpy(game_data->player2, player2.c_str(), LOGIN_MAX - 1);
    } else {
        return false; // Оба места заняты
    }
    
    // Если теперь в игре 2 игрока, переводим в режим расстановки
    if (game_data->player1[0] != '\0' && game_data->player2[0] != '\0') {
        game_data->state = GAME_SETUP;
    }
    
    return true;
}

bool Game::can_place_ship(uint8_t size, uint8_t x, uint8_t y, bool horizontal, 
                         CellState board[BOARD_SIZE][BOARD_SIZE]) const {
    
    std::cout << "DEBUG: Checking if can place ship size " << (int)size 
              << " at " << (int)x << "," << (int)y 
              << " " << (horizontal ? "H" : "V") << std::endl;
    
    // Проверяем границы
    if (x >= BOARD_SIZE || y >= BOARD_SIZE) {
        std::cout << "DEBUG: Coordinates out of bounds" << std::endl;
        return false;
    }
    
    // Для однопалубного корабля (size == 1)
    if (size == 1) {
        // Просто проверяем клетку и вокруг
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    if (board[ny][nx] == CELL_SHIP) {
                        std::cout << "DEBUG: Cell " << nx << "," << ny << " already has ship" << std::endl;
                        return false;
                    }
                }
            }
        }
        return true;
    }
    
    // Для многопалубных кораблей (size >= 2)
    if (horizontal) {
        if (x + size > BOARD_SIZE) {
            std::cout << "DEBUG: Ship goes beyond right border" << std::endl;
            return false;
        }
        // Проверяем клетки и соседние
        for (int i = 0; i < size; i++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + i + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                        if (board[ny][nx] == CELL_SHIP) {
                            std::cout << "DEBUG: Cell " << nx << "," << ny << " already has ship" << std::endl;
                            return false;
                        }
                    }
                }
            }
        }
    } else {
        if (y + size > BOARD_SIZE) {
            std::cout << "DEBUG: Ship goes beyond bottom border" << std::endl;
            return false;
        }
        for (int i = 0; i < size; i++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + i + dy;
                    if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                        if (board[ny][nx] == CELL_SHIP) {
                            std::cout << "DEBUG: Cell " << nx << "," << ny << " already has ship" << std::endl;
                            return false;
                        }
                    }
                }
            }
        }
    }
    
    std::cout << "DEBUG: Ship can be placed here" << std::endl;
    return true;
}

void Game::place_ship_on_board(uint8_t size, uint8_t x, uint8_t y, bool horizontal,
                              CellState board[BOARD_SIZE][BOARD_SIZE], Ship* ship_array, uint8_t& ship_count) {
    Ship ship;
    ship.size = size;
    ship.health = size;
    ship.horizontal = horizontal;
    ship.start_x = x;
    ship.start_y = y;
    ship.sunk = false;
    
    // Размещение на поле
    if (size == 1) {
        // Однопалубный корабль
        board[y][x] = CELL_SHIP;
    } else if (horizontal) {
        for (int i = 0; i < size; i++) {
            board[y][x + i] = CELL_SHIP;
        }
    } else {
        for (int i = 0; i < size; i++) {
            board[y + i][x] = CELL_SHIP;
        }
    }
    
    ship_array[ship_count] = ship;
    ship_count++;
}

bool Game::place_ship(const std::string& player, uint8_t size, uint8_t x, uint8_t y, bool horizontal) {
    // Разрешаем размещение в состояниях WAITING и SETUP
    if (game_data->state != GAME_WAITING && game_data->state != GAME_SETUP) {
        std::cout << "DEBUG: Wrong game state: " << (int)game_data->state << std::endl;
        return false;
    }
    
    // Проверяем допустимость размера корабля
    bool valid_size = false;
    int required_count = 0;
    
    switch(size) {
        case 4: valid_size = true; required_count = 1; break;
        case 3: valid_size = true; required_count = 2; break;
        case 2: valid_size = true; required_count = 3; break;
        case 1: valid_size = true; required_count = 4; break;
        default: 
            std::cout << "DEBUG: Invalid ship size: " << (int)size << std::endl;
            return false;
    }
    
    if (!valid_size) return false;

    if (size == 1) {
        horizontal = true; 
    }
    
    // Определяем, какой игрок размещает корабль
    uint8_t current_count = 0;
    Ship* ships = nullptr;
    uint8_t* ship_count = nullptr;
    CellState (*board)[BOARD_SIZE] = nullptr;
    
    if (player == std::string(game_data->player1)) {
        ships = game_data->ships1;
        ship_count = &game_data->ship_count1;
        board = game_data->board1;
        std::cout << "DEBUG: Player 1 placing ship" << std::endl;
    } else if (player == std::string(game_data->player2)) {
        ships = game_data->ships2;
        ship_count = &game_data->ship_count2;
        board = game_data->board2;
        std::cout << "DEBUG: Player 2 placing ship" << std::endl;
    } else {
        std::cout << "DEBUG: Unknown player: " << player << std::endl;
        return false;
    }
    
    // Проверяем, не превышен ли лимит кораблей данного типа
    for (int i = 0; i < *ship_count; i++) {
        if (ships[i].size == size) current_count++;
    }
    
    if (current_count >= required_count) {
        std::cout << "DEBUG: Too many ships of size " << (int)size 
                  << " (have " << current_count << ", need " << required_count << ")" << std::endl;
        return false;
    }
    
    // Проверяем возможность размещения
    if (!can_place_ship(size, x, y, horizontal, board)) {
        std::cout << "DEBUG: Cannot place ship at " << (int)x << "," << (int)y 
                  << " size " << (int)size << (horizontal ? "H" : "V") << std::endl;
        return false;
    }
    
    // Размещаем корабль
    place_ship_on_board(size, x, y, horizontal, board, ships, *ship_count);
    
    std::cout << "DEBUG: Ship placed successfully. Player " << player 
              << " now has " << (int)*ship_count << " ships" << std::endl;
    
    // Если это первый корабль и игра в состоянии WAITING, меняем на SETUP
    if (game_data->state == GAME_WAITING) {
        game_data->state = GAME_SETUP;
        std::cout << "DEBUG: Game state changed to SETUP" << std::endl;
    }
    
    return true;
}

bool Game::check_hit(uint8_t x, uint8_t y, CellState board[BOARD_SIZE][BOARD_SIZE],
                    Ship* ships, uint8_t ship_count, bool& sunk, uint8_t& sunk_ship_index) {
    if (board[y][x] == CELL_SHIP) {
        for (int i = 0; i < ship_count; i++) {
            Ship& ship = ships[i];
            if (ship.sunk) continue;
            
            if (ship.horizontal) {
                if (y == ship.start_y && x >= ship.start_x && x < ship.start_x + ship.size) {
                    ship.health--;
                    std::cout << "DEBUG: Hit ship " << i << " at " << (int)x << "," << (int)y 
                              << ". Health now: " << (int)ship.health << std::endl;
                    
                    if (ship.health == 0) {
                        ship.sunk = true;
                        sunk = true;
                        sunk_ship_index = i;
                        std::cout << "DEBUG: Ship " << i << " SUNK! Marking cells..." << std::endl;
                        
                        // Помечаем все клетки корабля как потопленные
                        for (int j = 0; j < ship.size; j++) {
                            board[ship.start_y][ship.start_x + j] = CELL_SUNK;
                        }
                    } else {
                        board[y][x] = CELL_HIT;
                    }
                    return true;
                }
            } else {
                if (x == ship.start_x && y >= ship.start_y && y < ship.start_y + ship.size) {
                    ship.health--;
                    std::cout << "DEBUG: Hit ship " << i << " at " << (int)x << "," << (int)y 
                              << ". Health now: " << (int)ship.health << std::endl;
                    
                    if (ship.health == 0) {
                        ship.sunk = true;
                        sunk = true;
                        sunk_ship_index = i;
                        std::cout << "DEBUG: Ship " << i << " SUNK! Marking cells..." << std::endl;
                        
                        for (int j = 0; j < ship.size; j++) {
                            board[ship.start_y + j][ship.start_x] = CELL_SUNK;
                        }
                    } else {
                        board[y][x] = CELL_HIT;
                    }
                    return true;
                }
            }
        }
    } else if (board[y][x] == CELL_EMPTY) {
        board[y][x] = CELL_MISS;
        return false;
    }
    
    return false;
}

bool Game::make_shot(const std::string& shooter, uint8_t x, uint8_t y) {
    if (game_data->state != GAME_ACTIVE) return false;
    if (!is_player_turn(shooter)) return false;
    if (x >= BOARD_SIZE || y >= BOARD_SIZE) return false;
    
    bool is_player1 = (shooter == std::string(game_data->player1));
    bool hit = false;
    bool sunk = false;
    uint8_t sunk_ship_index = 0;
    
    CellState (*target_board)[BOARD_SIZE] = nullptr;
    Ship* target_ships = nullptr;
    uint8_t target_ship_count = 0;
    
    if (is_player1) {
        target_board = game_data->board2;
        target_ships = game_data->ships2;
        target_ship_count = game_data->ship_count2;
    } else {
        target_board = game_data->board1;
        target_ships = game_data->ships1;
        target_ship_count = game_data->ship_count1;
    }
    
    hit = check_hit(x, y, target_board, target_ships, target_ship_count, sunk, sunk_ship_index);
    
    // УБЕДИТЕСЬ, ЧТО ЭТА ЧАСТЬ КОДА ВЫПОЛНЯЕТСЯ:
    if (is_player1) {
        if (hit) {
            game_data->hits1++;
            if (sunk) {
                game_data->sunk1++;
                std::cout << "DEBUG: Player 1 sunk a ship! Total sunk: " << (int)game_data->sunk1 << std::endl;
            }
        } else {
            game_data->misses1++;
        }
    } else {
        if (hit) {
            game_data->hits2++;
            if (sunk) {
                game_data->sunk2++;
                std::cout << "DEBUG: Player 2 sunk a ship! Total sunk: " << (int)game_data->sunk2 << std::endl;
            }
        } else {
            game_data->misses2++;
        }
    }
    
    if (check_game_over()) {
        game_data->state = GAME_FINISHED;
        game_data->end_time = time(nullptr);
        std::strcpy(game_data->current_turn, "");
    } else if (!hit) {
        switch_turn();
    } else {
        // Если попали, но не потопили корабль, ход остается у того же игрока
        std::cout << "DEBUG: Hit but not sunk, shooter gets another turn" << std::endl;
    }
    
    return hit;
}

void Game::switch_turn() {
    if (std::string(game_data->current_turn) == std::string(game_data->player1)) {
        std::strcpy(game_data->current_turn, game_data->player2);
    } else {
        std::strcpy(game_data->current_turn, game_data->player1);
    }
}

bool Game::check_game_over() const {
    bool all_sunk2 = true;
    for (int i = 0; i < game_data->ship_count2; i++) {
        if (!game_data->ships2[i].sunk) {
            all_sunk2 = false;
            break;
        }
    }
    
    bool all_sunk1 = true;
    for (int i = 0; i < game_data->ship_count1; i++) {
        if (!game_data->ships1[i].sunk) {
            all_sunk1 = false;
            break;
        }
    }
    
    return all_sunk1 || all_sunk2;
}

bool Game::is_setup_complete(const std::string& player) const {
    if (player == std::string(game_data->player1)) {
        return game_data->ship_count1 == 10;
    } else if (player == std::string(game_data->player2)) {
        return game_data->ship_count2 == 10;
    }
    return false;
}

void Game::set_setup_complete(const std::string& player) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (root->clients[i].used && std::strcmp(root->clients[i].login, player.c_str()) == 0) {
            root->clients[i].setup_complete = true;
            break;
        }
    }
    
    bool player1_ready = false;
    bool player2_ready = false;
    
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (root->clients[i].used) {
            if (std::strcmp(root->clients[i].login, game_data->player1) == 0) {
                player1_ready = root->clients[i].setup_complete;
            } else if (std::strcmp(root->clients[i].login, game_data->player2) == 0) {
                player2_ready = root->clients[i].setup_complete;
            }
        }
    }
    
    if (player1_ready && player2_ready) {
        game_data->state = GAME_ACTIVE;
        game_data->start_time = time(nullptr);
        std::strcpy(game_data->current_turn, game_data->player1);
    }
}

bool Game::is_game_active() const {
    return game_data->state == GAME_ACTIVE;
}

bool Game::is_game_finished() const {
    return game_data->state == GAME_FINISHED;
}

std::string Game::get_winner() const {
    if (!is_game_finished()) return "";
    
    bool all_sunk1 = true;
    for (int i = 0; i < game_data->ship_count1; i++) {
        if (!game_data->ships1[i].sunk) {
            all_sunk1 = false;
            break;
        }
    }
    
    bool all_sunk2 = true;
    for (int i = 0; i < game_data->ship_count2; i++) {
        if (!game_data->ships2[i].sunk) {
            all_sunk2 = false;
            break;
        }
    }
    
    if (all_sunk1) return std::string(game_data->player2);
    if (all_sunk2) return std::string(game_data->player1);
    return "";
}

std::string Game::get_current_turn() const {
    return std::string(game_data->current_turn);
}

std::string Game::board_to_string(CellState board[BOARD_SIZE][BOARD_SIZE], bool show_ships) const {
    std::stringstream ss;
    
    ss << "   ";
    for (int i = 0; i < BOARD_SIZE; i++) {
        ss << std::setw(2) << i << " ";
    }
    ss << "\n";
    
    for (int y = 0; y < BOARD_SIZE; y++) {
        ss << std::setw(2) << y << " ";
        for (int x = 0; x < BOARD_SIZE; x++) {
            char symbol = '.';
            switch(board[y][x]) {
                case CELL_EMPTY: symbol = '.'; break;
                case CELL_SHIP: symbol = show_ships ? 'S' : '.'; break;
                case CELL_HIT: symbol = 'X'; break;
                case CELL_MISS: symbol = 'O'; break;
                case CELL_SUNK: symbol = '#'; break;
            }
            ss << " " << symbol << " ";
        }
        ss << "\n";
    }
    
    return ss.str();
}

std::string Game::get_player_board(const std::string& player, bool show_ships) const {
    if (player == std::string(game_data->player1)) {
        return board_to_string(game_data->board1, show_ships);
    } else if (player == std::string(game_data->player2)) {
        return board_to_string(game_data->board2, show_ships);
    }
    return "";
}

std::string Game::get_opponent_view(const std::string& player) const {
    CellState temp_board[BOARD_SIZE][BOARD_SIZE];
    
    if (player == std::string(game_data->player1)) {
        for (int y = 0; y < BOARD_SIZE; y++) {
            for (int x = 0; x < BOARD_SIZE; x++) {
                if (game_data->board2[y][x] == CELL_SHIP) {
                    temp_board[y][x] = CELL_EMPTY;
                } else {
                    temp_board[y][x] = game_data->board2[y][x];
                }
            }
        }
    } else {
        for (int y = 0; y < BOARD_SIZE; y++) {
            for (int x = 0; x < BOARD_SIZE; x++) {
                if (game_data->board1[y][x] == CELL_SHIP) {
                    temp_board[y][x] = CELL_EMPTY;
                } else {
                    temp_board[y][x] = game_data->board1[y][x];
                }
            }
        }
    }
    
    return board_to_string(temp_board, false);
}

std::string Game::get_statistics(const std::string& player) const {
    std::stringstream ss;
    
    bool is_player1 = (player == std::string(game_data->player1));
    
    ss << "Статистика:\n";
    ss << "Сбито кораблей: " << (is_player1 ? (int)game_data->sunk1 : (int)game_data->sunk2) << "\n";
    ss << "Попаданий: " << (is_player1 ? (int)game_data->hits1 : (int)game_data->hits2) << "\n";
    ss << "Промахов: " << (is_player1 ? (int)game_data->misses1 : (int)game_data->misses2) << "\n";
    
    if (is_game_finished()) {
        ss << "Игра завершена!\n";
        std::string winner = get_winner();
        if (winner == player) {
            ss << "Вы победили!\n";
        } else {
            ss << "Победил: " << winner << "\n";
        }
    } else if (is_game_active()) {
        ss << "Текущий ход: " << get_current_turn() << "\n";
        if (get_current_turn() == player) {
            ss << "Ваш ход!\n";
        } else {
            ss << "Ход противника\n";
        }
    }
    
    return ss.str();
}

std::string Game::get_status() const {
    std::stringstream ss;
    
    ss << "Игра: " << game_data->game_name << " (ID: " << game_id << ")\n";
    ss << "Игрок 1: " << game_data->player1 << "\n";
    ss << "Игрок 2: " << (game_data->player2[0] ? game_data->player2 : "ожидает...") << "\n";
    ss << "Тип: " << (game_data->is_public ? "публичная" : "приватная") << "\n";
    ss << "Статус: ";
    
    switch(game_data->state) {
        case GAME_WAITING: ss << "Ожидание второго игрока"; break;
        case GAME_SETUP: ss << "Расстановка кораблей"; break;
        case GAME_ACTIVE: ss << "Идет игра (ход: " << game_data->current_turn << ")"; break;
        case GAME_FINISHED: 
            ss << "Завершена. Победитель: " << get_winner();
            break;
    }
    
    return ss.str();
}

bool Game::has_player(const std::string& player) const {
    return (player == std::string(game_data->player1) || 
            player == std::string(game_data->player2));
}

bool Game::is_player_turn(const std::string& player) const {
    return (game_data->state == GAME_ACTIVE && 
            std::string(game_data->current_turn) == player);
}