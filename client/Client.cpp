#include "Client.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

Client::Client() : shm(false), root(shm.root()) {
    if (!root) throw std::runtime_error("Cannot open shared memory; run server first");
}

Client::~Client() {}

ClientSlot* Client::my_slot() {
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login.c_str(), LOGIN_MAX)==0) {
            return &root->clients[i];
        }
    }
    return nullptr;
}

bool Client::enqueue_message(const Message& m) {
    pthread_mutex_lock(&root->mutex);
    // check space
    size_t next_tail = (root->q_tail + 1) % QUEUE_SIZE;
    if (next_tail == root->q_head) {
        // queue full
        pthread_mutex_unlock(&root->mutex);
        return false;
    }
    // place message at tail
    root->queue[root->q_tail] = m;
    root->queue[root->q_tail].used = true;
    root->q_tail = next_tail;
    // signal server
    pthread_cond_signal(&root->server_cond);
    pthread_mutex_unlock(&root->mutex);
    return true;
}

bool Client::wait_for_response(std::string &out, int /*timeout_ms*/) {
    // find our client slot
    ClientSlot* slot = nullptr;
    // it's possible that server hasn't created our slot yet (REGISTER path)
    // wait until our slot exists
    while (true) {
        pthread_mutex_lock(&root->mutex);
        slot = my_slot();
        if (!slot) {
            // no slot yet -> unlock and sleep shortly
            pthread_mutex_unlock(&root->mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        // wait for has_response
        while (!slot->has_response) {
            pthread_cond_wait(&slot->cond, &root->mutex);
        }
        // copy response
        out = slot->response;
        slot->has_response = false;
        std::memset(slot->response, 0, RESP_MAX);
        pthread_mutex_unlock(&root->mutex);
        return true;
    }
}

void Client::run() {
    std::cout << "Введите логин: ";
    std::getline(std::cin, login);
    if (login.empty()) {
        std::cerr << "Empty login\n";
        return;
    }

    // send REGISTER
    Message reg;
    std::memset(&reg, 0, sizeof(reg));
    std::strncpy(reg.from, login.c_str(), LOGIN_MAX-1);
    reg.type = MSG_REGISTER;
    if (!enqueue_message(reg)) {
        std::cerr << "Failed to send register\n";
        return;
    }
    // wait for registration response
    std::string resp;
    if (wait_for_response(resp)) {
        std::cout << "Server: " << resp << "\n";
    }

    bool running = true;
    while (running) {
        std::cout << "\nМеню:\n 1 - Список игроков\n 2 - Пригласить по логину\n 3 - Принять приглашение (вручную введя LOGIN кто отправил)\n 4 - Выстрел (x,y:target)\n 5 - Выйти\n> ";
        std::string line;
        std::getline(std::cin, line);
        if (line=="1") {
            Message m; std::memset(&m,0,sizeof(m));
            std::strncpy(m.from, login.c_str(), LOGIN_MAX-1);
            m.type = MSG_LIST;
            if (!enqueue_message(m)) std::cout << "Очередь переполнена, повторите позже\n";
            else {
                if (wait_for_response(resp)) std::cout << "Server: " << resp << "\n";
            }
        } else if (line=="2") {
            std::cout << "Введите логин цели: ";
            std::string tgt; std::getline(std::cin, tgt);
            Message m; std::memset(&m,0,sizeof(m));
            std::strncpy(m.from, login.c_str(), LOGIN_MAX-1);
            m.type = MSG_INVITE;
            std::strncpy(m.payload, tgt.c_str(), CMD_MAX-1);
            if (!enqueue_message(m)) std::cout << "Очередь переполнена\n";
            else if (wait_for_response(resp)) std::cout << "Server: " << resp << "\n";
        } else if (line=="3") {
            std::cout << "Введите логин, кого принимаете: ";
            std::string who; std::getline(std::cin, who);
            Message m; std::memset(&m,0,sizeof(m));
            std::strncpy(m.from, login.c_str(), LOGIN_MAX-1);
            m.type = MSG_ACCEPT;
            std::strncpy(m.payload, who.c_str(), CMD_MAX-1);
            if (!enqueue_message(m)) std::cout << "Очередь переполнена\n";
            else if (wait_for_response(resp)) std::cout << "Server: " << resp << "\n";
        } else if (line=="4") {
            std::cout << "Введите выстрел в формате x,y:target (пример 3,5:opponent): ";
            std::string pl; std::getline(std::cin, pl);
            Message m; std::memset(&m,0,sizeof(m));
            std::strncpy(m.from, login.c_str(), LOGIN_MAX-1);
            m.type = MSG_SHOT;
            std::strncpy(m.payload, pl.c_str(), CMD_MAX-1);
            if (!enqueue_message(m)) std::cout << "Очередь переполнена\n";
            else if (wait_for_response(resp)) std::cout << "Server: " << resp << "\n";
        } else if (line=="5") {
            Message m; std::memset(&m,0,sizeof(m));
            std::strncpy(m.from, login.c_str(), LOGIN_MAX-1);
            m.type = MSG_QUIT;
            enqueue_message(m);
            running = false;
            std::cout << "Выход...\n";
        } else {
            std::cout << "Неверная команда\n";
        }
    }
}
