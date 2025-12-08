#pragma once
#include <pthread.h>
#include <cstdint>

constexpr const char* SHM_NAME = "/battleship_shm_v1";
constexpr size_t MAX_CLIENTS = 32;
constexpr size_t QUEUE_SIZE = 64;
constexpr size_t LOGIN_MAX = 32;
constexpr size_t CMD_MAX = 256;
constexpr size_t RESP_MAX = 256;

enum MsgType : uint8_t {
    MSG_REGISTER = 1,
    MSG_LIST = 2,
    MSG_INVITE = 3,
    MSG_ACCEPT = 4,
    MSG_SHOT = 5,
    MSG_QUIT = 6
};

struct Message {
    bool used;
    char from[LOGIN_MAX];
    char to[LOGIN_MAX];   // empty = server-directed or broadcast
    uint8_t type;
    char payload[CMD_MAX];
};

struct ClientSlot {
    bool used;
    char login[LOGIN_MAX];
    pthread_cond_t cond;   // signaled when server has response for this client
    char response[RESP_MAX];
    bool has_response;
};

struct SharedMemoryRoot {
    // global sync
    pthread_mutex_t mutex;
    pthread_cond_t server_cond; // signaled when queue has entries
    // circular queue
    Message queue[QUEUE_SIZE];
    size_t q_head;
    size_t q_tail;
    // clients
    ClientSlot clients[MAX_CLIENTS];
};
