#include "Server.hpp"
#include <iostream>
#include <cstring>
#include <cassert>
#include <sstream>
#include <algorithm>

Server::Server() : shm(true), root(shm.root()), setup_done(false) {
    // initialize attributes if owner
    if (shm.is_owner()) {
        init_shared_objects();
    }
}

Server::~Server() {
    // dtor: shared memory cleanup is handled by SharedMemory
}

void Server::init_shared_objects() {
    // initialize mutex and cond with pshared attributes
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&root->mutex, &mattr);
    pthread_cond_init(&root->server_cond, &cattr);

    root->q_head = root->q_tail = 0;
    for (size_t i=0;i<QUEUE_SIZE;++i) root->queue[i].used = false;
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        root->clients[i].used = false;
        root->clients[i].has_response = false;
        // initialize cond per client for process-shared (use same attr)
        pthread_cond_init(&root->clients[i].cond, &cattr);
        std::memset(root->clients[i].login, 0, sizeof(root->clients[i].login));
        std::memset(root->clients[i].response, 0, sizeof(root->clients[i].response));
    }

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    setup_done = true;
    std::cout << "Server: shared memory initialized\n";
}

ClientSlot* Server::find_or_create_client(const char* login) {
    // assume mutex locked by caller
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login, LOGIN_MAX)==0) {
            return &root->clients[i];
        }
    }
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        if (!root->clients[i].used) {
            root->clients[i].used = true;
            std::strncpy(root->clients[i].login, login, LOGIN_MAX-1);
            root->clients[i].has_response = false;
            std::memset(root->clients[i].response, 0, RESP_MAX);
            return &root->clients[i];
        }
    }
    return nullptr;
}

ClientSlot* Server::find_client(const char* login) {
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login, LOGIN_MAX)==0) {
            return &root->clients[i];
        }
    }
    return nullptr;
}

std::vector<std::string> Server::list_clients() {
    std::vector<std::string> res;
    for (size_t i=0;i<MAX_CLIENTS;++i) {
        if (root->clients[i].used) res.emplace_back(root->clients[i].login);
    }
    return res;
}

void Server::send_response_to(const char* login, const char* text) {
    // assume mutex locked by caller
    ClientSlot* cl = find_client(login);
    if (!cl) return;
    std::strncpy(cl->response, text, RESP_MAX-1);
    cl->has_response = true;
    // signal client
    pthread_cond_signal(&cl->cond);
}

void Server::handle_message(const Message &m) {
    // m.from, m.to, m.type, m.payload
    std::string from(m.from);
    switch (m.type) {
        case MSG_REGISTER: {
            // create client slot and ack
            ClientSlot* c = find_or_create_client(m.from);
            if (c) {
                std::string ack = "REGISTERED:OK";
                send_response_to(m.from, ack.c_str());
                std::cout << "Registered client: " << m.from << '\n';
            } else {
                send_response_to(m.from, "REGISTERED:FAIL_FULL");
            }
            break;
        }
        case MSG_LIST: {
            auto cl = list_clients();
            std::ostringstream ss;
            ss << "LIST:";
            bool first = true;
            for (auto &n : cl) {
                if (!first) ss << ",";
                ss << n;
                first = false;
            }
            send_response_to(m.from, ss.str().c_str());
            break;
        }
        case MSG_INVITE: {
            // payload contains target login
            const char* target = m.payload;
            ClientSlot* tgt = find_client(target);
            if (!tgt) {
                send_response_to(m.from, "INVITE_FAIL:NOT_FOUND");
            } else {
                // forward invite to target as response (INVITE_FROM:<from>)
                char buf[RESP_MAX];
                std::snprintf(buf, RESP_MAX, "INVITE_FROM:%s", m.from);
                send_response_to(target, buf);
                send_response_to(m.from, "INVITE_SENT");
                std::cout << m.from << " invited " << target << '\n';
            }
            break;
        }
        case MSG_ACCEPT: {
            const char* from_who = m.payload; // who accepted the invitation? m.from is accepter, payload is inviter
            const char* inviter = m.payload;
            // tell inviter that acceptor accepted
            char buf[RESP_MAX];
            std::snprintf(buf, RESP_MAX, "ACCEPTED_BY:%s", m.from);
            send_response_to(inviter, buf);
            send_response_to(m.from, "ACCEPT_OK");
            // create a Game object
            std::string game_name = std::string(inviter) + "_" + std::string(m.from);
            games.emplace_back(game_name, inviter);
            games.back().join(m.from);
            std::cout << "Game started: " << game_name << '\n';
            // notify both players game started
            char buf1[RESP_MAX];
            std::snprintf(buf1, RESP_MAX, "GAME_START:OPPONENT:%s:TURN", m.from); // inviter's turn
            send_response_to(inviter, buf1);
            char buf2[RESP_MAX];
            std::snprintf(buf2, RESP_MAX, "GAME_START:OPPONENT:%s:WAIT", inviter);
            send_response_to(m.from, buf2);
            break;
        }
        case MSG_SHOT: {
            // payload "x,y:target"
            // For prototype, server simply forwards shot to opponent and computes hit randomly (or uses simple placeholder)
            // We'll parse payload as "x,y:target"
            int x=-1,y=-1;
            char target[LOGIN_MAX]{0};
            if (sscanf(m.payload, "%d,%d:%31s", &x, &y, target) >= 2) {
                // compute mock result (alternate HIT/MISS based on parity)
                const char* result = ((x+y)%2==0) ? "HIT" : "MISS";
                // inform shooter
                char bufShooter[RESP_MAX];
                std::snprintf(bufShooter, RESP_MAX, "SHOT_RESULT:%s:%d,%d", result, x, y);
                send_response_to(m.from, bufShooter);
                // inform target
                char bufTarget[RESP_MAX];
                std::snprintf(bufTarget, RESP_MAX, "INCOMING:%s:%d,%d", result, x, y);
                send_response_to(target, bufTarget);
                if (strcmp(result,"HIT")==0) {
                    // optional: end game when some condition — for prototype we don't track full boards
                }
            } else {
                send_response_to(m.from, "SHOT_FAIL:BAD_FORMAT");
            }
            break;
        }
        case MSG_QUIT: {
            // mark client slot unused
            ClientSlot* c = find_client(m.from);
            if (c) {
                c->used = false;
                c->has_response = false;
                std::memset(c->login, 0, LOGIN_MAX);
                std::memset(c->response, 0, RESP_MAX);
                std::cout << "Client quit: " << m.from << '\n';
            }
            break;
        }
        default:
            send_response_to(m.from, "UNKNOWN_CMD");
    }
}

void Server::run() {
    std::cout << "=== SERVER RUNNING ===\n";
    while (true) {
        // wait for messages
        pthread_mutex_lock(&root->mutex);
        // wait until queue not empty
        while (root->q_head == root->q_tail) {
            pthread_cond_wait(&root->server_cond, &root->mutex);
        }
        // pop one message
        Message m = root->queue[root->q_head];
        root->queue[root->q_head].used = false;
        root->q_head = (root->q_head + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&root->mutex);

        if (m.used) {
            handle_message(m);
        }
    }
}
