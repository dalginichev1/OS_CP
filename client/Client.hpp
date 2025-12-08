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

    bool enqueue_message(const Message& m);
    bool wait_for_response(std::string &out, int timeout_ms = 0); // timeout_ms==0 => wait indefinite
    ClientSlot* my_slot();
};
