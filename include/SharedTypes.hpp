#pragma once
#include <pthread.h>
#include <cstdint>
#include <cstring>

constexpr const char* SHM_NAME = "/battleship_shm_v3";
constexpr size_t MAX_CLIENTS = 32;
constexpr size_t QUEUE_SIZE = 128;
constexpr size_t LOGIN_MAX = 32;
constexpr size_t CMD_MAX = 256;
constexpr size_t RESP_MAX = 512;

constexpr int BOARD_SIZE = 10;
constexpr int MAX_SHIPS = 10;

enum CellState : uint8_t {
    CELL_EMPTY = 0,
    CELL_SHIP = 1,
    CELL_HIT = 2,
    CELL_MISS = 3,
    CELL_SUNK = 4
};

enum ShipType : uint8_t {
    SHIP_CARRIER = 4,
    SHIP_BATTLESHIP = 3,
    SHIP_CRUISER = 2,
    SHIP_DESTROYER = 1
};

enum GameState : uint8_t {
    GAME_WAITING = 0,
    GAME_SETUP = 1,
    GAME_ACTIVE = 2,
    GAME_FINISHED = 3
};

struct Ship {
    uint8_t size;
    uint8_t health;
    bool horizontal;
    uint8_t start_x;
    uint8_t start_y;
    bool sunk;
};

struct GameData {
    bool used;
    char game_name[LOGIN_MAX];
    char player1[LOGIN_MAX];
    char player2[LOGIN_MAX];
    GameState state;
    char current_turn[LOGIN_MAX];
    bool is_public;
    
    CellState board1[BOARD_SIZE][BOARD_SIZE];
    CellState board2[BOARD_SIZE][BOARD_SIZE];
    
    Ship ships1[MAX_SHIPS];
    Ship ships2[MAX_SHIPS];
    uint8_t ship_count1;
    uint8_t ship_count2;
    
    uint8_t hits1;
    uint8_t hits2;
    uint8_t misses1;
    uint8_t misses2;
    uint8_t sunk1;
    uint8_t sunk2;
    
    time_t start_time;
    time_t end_time;
};

enum MsgType : uint8_t {
    MSG_REGISTER = 1,
    MSG_LIST = 2,
    MSG_INVITE = 3,
    MSG_INVITE_TO_GAME = 16,
    MSG_ACCEPT = 4,
    MSG_SHOT = 5,
    MSG_QUIT = 6,
    MSG_SETUP_COMPLETE = 7,
    MSG_PLACE_SHIP = 8,
    MSG_GET_BOARD = 9,
    MSG_GET_OPPONENT_BOARD = 10,
    MSG_SURRENDER = 11,
    MSG_GAME_STATUS = 12,
    MSG_CREATE = 13,
    MSG_JOIN = 14,
    MSG_LEAVE_GAME = 15
};

struct Message {
    bool used;
    char from[LOGIN_MAX];
    char to[LOGIN_MAX];
    uint8_t type;
    char payload[CMD_MAX];
};

struct ClientSlot {
    bool used;
    char login[LOGIN_MAX];
    pthread_cond_t cond;
    char response[RESP_MAX];
    bool has_response;
    int current_game_id;
    bool setup_complete;
};

struct SharedMemoryRoot {
    pthread_mutex_t mutex;
    pthread_cond_t server_cond;
    
    Message queue[QUEUE_SIZE];
    size_t q_head;
    size_t q_tail;
    
    ClientSlot clients[MAX_CLIENTS];
    
    GameData games[16];
    size_t game_count;
};