#include "Server.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

Server::Server() : shm(true), root(shm.root()), setup_done(false) {
    if (shm.is_owner()) {
        init_shared_objects();
    }
}

Server::~Server() {
    for (auto& pair : games_map) {
        delete pair.second;
    }
}

void Server::init_shared_objects() {
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&root->mutex, &mattr);
    pthread_cond_init(&root->server_cond, &cattr);

    root->q_head = root->q_tail = 0;
    root->game_count = 0;

    for (size_t i = 0; i < QUEUE_SIZE; ++i)
        root->queue[i].used = false;
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        root->clients[i].used = false;
        root->clients[i].has_response = false;
        root->clients[i].current_game_id = -1;
        root->clients[i].setup_complete = false;
        pthread_cond_init(&root->clients[i].cond, &cattr);
        std::memset(root->clients[i].login, 0, sizeof(root->clients[i].login));
        std::memset(root->clients[i].response, 0, sizeof(root->clients[i].response));
    }

    for (size_t i = 0; i < 16; ++i) {
        root->games[i].used = false;
    }

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    setup_done = true;
    std::cout << "Server: shared memory initialized\n";
}

ClientSlot* Server::find_or_create_client(const char* login) {
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login, LOGIN_MAX) == 0) {
            return &root->clients[i];
        }
    }
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (!root->clients[i].used) {
            root->clients[i].used = true;
            std::strncpy(root->clients[i].login, login, LOGIN_MAX - 1);
            root->clients[i].has_response = false;
            root->clients[i].current_game_id = -1;
            root->clients[i].setup_complete = false;
            std::memset(root->clients[i].response, 0, RESP_MAX);
            return &root->clients[i];
        }
    }
    return nullptr;
}

ClientSlot* Server::find_client(const char* login) {
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login, LOGIN_MAX) == 0) {
            return &root->clients[i];
        }
    }
    return nullptr;
}

std::vector<std::string> Server::list_clients() {
    std::vector<std::string> res;
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (root->clients[i].used) {
            std::string info = root->clients[i].login;
            if (root->clients[i].current_game_id != -1) {
                info += " [в игре]";
            }
            res.emplace_back(info);
        }
    }
    return res;
}

std::vector<std::string> Server::list_available_games() {
    std::vector<std::string> res;
    for (int i = 0; i < 16; i++) {
        if (root->games[i].used && root->games[i].is_public &&
            root->games[i].state == GAME_WAITING) {
            std::string info = "🎮 " + std::string(root->games[i].game_name) +
                               " (ID: " + std::to_string(i) +
                               ") - создатель: " + std::string(root->games[i].player1);
            res.emplace_back(info);
        }
    }
    return res;
}

void Server::send_response_to(const char* login, const char* text) {
    pthread_mutex_lock(&root->mutex);
    ClientSlot* cl = find_client(login);
    if (cl) {
        std::strncpy(cl->response, text, RESP_MAX - 1);
        cl->has_response = true;
        pthread_cond_signal(&cl->cond);
    }
    pthread_mutex_unlock(&root->mutex);
}

int Server::create_private_game(const std::string& creator, const std::string& target) {
    if (root->game_count >= 16)
        return -1;

    std::string game_name = creator + "_vs_" + target;
    int game_id = -1;

    for (int i = 0; i < 16; i++) {
        if (!root->games[i].used) {
            game_id = i;
            break;
        }
    }

    if (game_id == -1)
        return -1;

    Game* game = new Game(game_name, creator, root, false);
    games_map[game_id] = game;
    root->game_count++;

    ClientSlot* client = find_client(creator.c_str());
    if (client) {
        client->current_game_id = game_id;
    }

    std::cout << "Private game '" << game_name << "' created by " << creator << " for " << target
              << std::endl;
    return game_id;
}

int Server::create_public_game(const std::string& game_name, const std::string& creator) {
    if (root->game_count >= 16) return -1;
    
    // Проверяем, не существует ли уже игры с таким именем
    for (int i = 0; i < 16; i++) {
        if (root->games[i].used && std::strcmp(root->games[i].game_name, game_name.c_str()) == 0) {
            return -2; // Игра с таким именем уже существует
        }
    }
    
    int game_id = -1;
    for (int i = 0; i < 16; i++) {
        if (!root->games[i].used) {
            game_id = i;
            break;
        }
    }
    
    if (game_id == -1) return -1;
    
    // Создаем игру
    Game* game = new Game(game_name, creator, root, true);
    games_map[game_id] = game;
    root->game_count++;
    
    // Обновляем состояние клиента
    ClientSlot* client = find_client(creator.c_str());
    if (client) {
        client->current_game_id = game_id;
        client->setup_complete = false;
    }
    
    std::cout << "Public game '" << game_name << "' created by " << creator << " (ID: " << game_id << ")" << std::endl;
    return game_id;
}

Game* Server::find_game_by_name(const std::string& game_name) {
    for (auto& pair : games_map) {
        if (pair.second->get_game_name() == game_name) {
            return pair.second;
        }
    }
    return nullptr;
}

Game* Server::get_game(int game_id) {
    auto it = games_map.find(game_id);
    if (it != games_map.end()) {
        return it->second;
    }
    return nullptr;
}

void Server::remove_game(int game_id) {
    auto it = games_map.find(game_id);
    if (it != games_map.end()) {
        Game* game = it->second;

        // Уведомляем всех игроков, что игра удалена
        std::string player1 = game->get_player1();
        std::string player2 = game->get_player2();

        if (!player1.empty()) {
            ClientSlot* client1 = find_client(player1.c_str());
            if (client1) {
                client1->current_game_id = -1;
                client1->setup_complete = false;
                send_response_to(player1.c_str(), "GAME_REMOVED:Игра удалена");
            }
        }

        if (!player2.empty()) {
            ClientSlot* client2 = find_client(player2.c_str());
            if (client2) {
                client2->current_game_id = -1;
                client2->setup_complete = false;
                send_response_to(player2.c_str(), "GAME_REMOVED:Игра удалена");
            }
        }

        // Удаляем игру
        delete game;
        games_map.erase(it);
        root->game_count--;

        if (game_id >= 0 && game_id < 16) {
            root->games[game_id].used = false;
        }
    }
}

bool Server::parse_ship_placement(const std::string& payload, uint8_t& size, uint8_t& x, uint8_t& y,
                                  bool& horizontal) {
    int s, x_pos, y_pos;
    char orientation;

    if (sscanf(payload.c_str(), "%d,%d,%d,%c", &s, &x_pos, &y_pos, &orientation) != 4) {
        return false;
    }

    if (s < 1 || s > 4)
        return false;
    if (x_pos < 0 || x_pos >= BOARD_SIZE)
        return false;
    if (y_pos < 0 || y_pos >= BOARD_SIZE)
        return false;
    if (orientation != 'H' && orientation != 'V')
        return false;

    size = static_cast<uint8_t>(s);
    x = static_cast<uint8_t>(x_pos);
    y = static_cast<uint8_t>(y_pos);
    horizontal = (orientation == 'H');

    return true;
}

bool Server::parse_shot(const std::string& payload, uint8_t& x, uint8_t& y) {
    int x_pos, y_pos;

    if (sscanf(payload.c_str(), "%d,%d", &x_pos, &y_pos) != 2) {
        return false;
    }

    if (x_pos < 0 || x_pos >= BOARD_SIZE)
        return false;
    if (y_pos < 0 || y_pos >= BOARD_SIZE)
        return false;

    x = static_cast<uint8_t>(x_pos);
    y = static_cast<uint8_t>(y_pos);

    return true;
}

void Server::handle_setup_complete(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client) {
        send_response_to(m.from, "ERROR:Not registered");
        return;
    }

    if (client->current_game_id == -1) {
        send_response_to(m.from, "ERROR:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    if (!game->is_setup_complete(m.from)) {
        send_response_to(m.from, "SETUP_INCOMPLETE:Place all ships first");
        return;
    }

    client->setup_complete = true;
    game->set_setup_complete(m.from);

    send_response_to(m.from, "SETUP_COMPLETE:Waiting for opponent...");
}

void Server::handle_place_ship(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client) {
        send_response_to(m.from, "ERROR:Not registered");
        return;
    }

    if (client->current_game_id == -1) {
        send_response_to(m.from, "ERROR:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    if (game->is_game_active() || game->is_game_finished()) {
        send_response_to(m.from, "ERROR:Game already started or finished");
        return;
    }

    uint8_t size, x, y;
    bool horizontal;

    if (!parse_ship_placement(m.payload, size, x, y, horizontal)) {
        send_response_to(m.from, "SHIP_ERROR:Invalid format. Use: size,x,y,orientation(H/V)");
        return;
    }

    if (game->place_ship(m.from, size, x, y, horizontal)) {
        send_response_to(m.from, "SHIP_PLACED:OK");

        std::string board = game->get_player_board(m.from, true);
        send_response_to(m.from, ("YOUR_BOARD:\n" + board).c_str());

        if (game->is_setup_complete(m.from)) {
            send_response_to(m.from, "ALL_SHIPS_PLACED:Use 'ready' when done");
        }
    } else {
        send_response_to(m.from, "SHIP_ERROR:Cannot place ship here");
    }
}

void Server::handle_get_board(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client || client->current_game_id == -1) {
        send_response_to(m.from, "ERROR:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    std::string board = game->get_player_board(m.from, true);
    send_response_to(m.from, ("YOUR_BOARD:\n" + board).c_str());
}

void Server::handle_get_opponent_board(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client || client->current_game_id == -1) {
        send_response_to(m.from, "ERROR:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    if (!game->is_game_active() && !game->is_game_finished()) {
        send_response_to(m.from, "ERROR:Game not started yet");
        return;
    }

    std::string board = game->get_opponent_view(m.from);
    send_response_to(m.from, ("OPPONENT_VIEW:\n" + board).c_str());
}

void Server::handle_surrender(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client || client->current_game_id == -1) {
        send_response_to(m.from, "ERROR:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    if (game->is_game_finished()) {
        send_response_to(m.from, "ERROR:Game already finished");
        return;
    }

    std::string opponent =
        (game->get_player1() == m.from) ? game->get_player2() : game->get_player1();

    send_response_to(m.from, "SURRENDER:You surrendered");
    send_response_to(opponent.c_str(), "OPPONENT_SURRENDERED:You win!");

    remove_game(client->current_game_id);
}

void Server::handle_game_status(const Message& m) {
    ClientSlot* client = find_client(m.from);
    if (!client || client->current_game_id == -1) {
        send_response_to(m.from, "STATUS:Not in a game");
        return;
    }

    Game* game = get_game(client->current_game_id);
    if (!game) {
        send_response_to(m.from, "ERROR:Game not found");
        return;
    }

    std::string status = game->get_status();
    std::string stats = game->get_statistics(m.from);

    send_response_to(m.from, ("GAME_STATUS:\n" + status + "\n" + stats).c_str());
}

void Server::handle_message(const Message& m) {
    std::string from(m.from);

    switch (m.type) {
    case MSG_REGISTER: {
        ClientSlot* c = find_or_create_client(m.from);
        if (c) {
            send_response_to(m.from, "REGISTERED:OK");
            std::cout << "Registered client: " << m.from << '\n';
        } else {
            send_response_to(m.from, "REGISTERED:FAIL_FULL");
        }
        break;
    }
    case MSG_LIST: {
        auto cl = list_clients();
        auto games = list_available_games();

        std::ostringstream ss;
        ss << "=== ИГРОКИ ОНЛАЙН (" << cl.size() << ") ===\n";
        for (auto& n : cl) {
            ss << "  " << n << "\n";
        }

        if (!games.empty()) {
            ss << "\n=== ДОСТУПНЫЕ ИГРЫ (" << games.size() << ") ===\n";
            for (auto& g : games) {
                ss << "  " << g << "\n";
            }
            ss << "\nПрисоединиться: join <имя_игры> или join <ID>\n";
        } else {
            ss << "\n=== НЕТ ДОСТУПНЫХ ИГР ===\n";
            ss << "  Создайте игру: create <имя_игры>\n";
            ss << "  Или пригласите: invite <логин>\n";
        }

        send_response_to(m.from, ss.str().c_str());
        break;
    }
    case MSG_INVITE: {
        // Приглашение конкретного игрока
        const char* target = m.payload;
        ClientSlot* tgt = find_client(target);

        if (!tgt) {
            send_response_to(m.from, "INVITE_FAIL:Игрок не найден");
        } else if (tgt->current_game_id != -1) {
            send_response_to(m.from, "INVITE_FAIL:Игрок уже в игре");
        } else if (find_client(m.from)->current_game_id != -1) {
            send_response_to(m.from, "INVITE_FAIL:Вы уже в игре");
        } else {
            int game_id = create_private_game(m.from, target);
            if (game_id == -1) {
                send_response_to(m.from, "INVITE_FAIL:Сервер переполнен");
            } else {
                // Отправляем приглашение
                Game* game = get_game(game_id);
                if (game) {
                    char buf[RESP_MAX];
                    std::snprintf(buf, RESP_MAX, "INVITE_FROM:%s:GAME:%s:ID:%d", m.from,
                                  game->get_game_name().c_str(), game_id);
                    send_response_to(target, buf);
                    send_response_to(m.from, "INVITE_SENT:Приглашение отправлено");
                    std::cout << m.from << " invited " << target << " to private game\n";
                }
            }
        }
        break;
    }
    case MSG_CREATE: {
        std::string game_name = m.payload;

        if (game_name.empty()) {
            send_response_to(m.from, "CREATE_FAIL:Имя игры не может быть пустым");
            break;
        }

        if (game_name.length() > LOGIN_MAX - 1) {
            send_response_to(m.from, "CREATE_FAIL:Имя игры слишком длинное");
            break;
        }

        ClientSlot* client = find_client(m.from);
        if (!client) {
            send_response_to(m.from, "CREATE_FAIL:Вы не зарегистрированы");
            break;
        }

        // Проверяем, не находится ли клиент уже в игре
        if (client->current_game_id != -1) {
            Game* existing_game = get_game(client->current_game_id);
            if (existing_game && existing_game->has_player(m.from)) {
                send_response_to(m.from, "CREATE_FAIL:Вы уже в игре");
                break;
            } else {
                // Если есть game_id, но игрока нет в игре - сбрасываем
                client->current_game_id = -1;
                client->setup_complete = false;
            }
        }

        // Проверяем, не существует ли уже игры с таким именем
        for (int i = 0; i < 16; i++) {
            if (root->games[i].used &&
                std::strcmp(root->games[i].game_name, game_name.c_str()) == 0) {
                send_response_to(m.from, "CREATE_FAIL:Игра с таким именем уже существует");
                break;
            }
        }

        int game_id = create_public_game(game_name, m.from);
        if (game_id == -1) {
            send_response_to(m.from, "CREATE_FAIL:Сервер переполнен");
        } else if (game_id == -2) {
            send_response_to(m.from, "CREATE_FAIL:Игра с таким именем уже существует");
        } else {
            char buf[RESP_MAX];
            std::snprintf(buf, RESP_MAX, "GAME_CREATED:Публичная игра '%s' создана (ID: %d)",
                          game_name.c_str(), game_id);
            send_response_to(m.from, buf);
        }
        break;
    }
    case MSG_JOIN: {
        // Присоединение к игре (публичной или по приглашению)
        std::string target = m.payload;

        ClientSlot* client = find_client(m.from);
        if (!client) {
            send_response_to(m.from, "JOIN_FAIL:Вы не зарегистрированы");
            break;
        }

        if (client->current_game_id != -1) {
            send_response_to(m.from, "JOIN_FAIL:Вы уже в игре");
            break;
        }

        Game* game = nullptr;
        int game_id = -1;

        // Пробуем найти по ID
        if (isdigit(target[0])) {
            game_id = std::stoi(target);
            game = get_game(game_id);
        }

        // Если не нашли по ID, ищем по имени
        if (!game) {
            for (auto& pair : games_map) {
                if (pair.second->get_game_name() == target) {
                    game = pair.second;
                    game_id = pair.first;
                    break;
                }
            }
        }

        if (!game) {
            send_response_to(m.from, "JOIN_FAIL:Игра не найдена");
            break;
        }

        // Проверяем, не пытается ли игрок присоединиться к своей же игре
        if (game->get_player1() == m.from || game->get_player2() == m.from) {
            send_response_to(m.from, "JOIN_FAIL:Вы уже в этой игре");
            break;
        }

        // Проверяем, есть ли свободное место в игре
        if (game->get_player1()[0] && game->get_player2()[0]) {
            send_response_to(m.from, "JOIN_FAIL:Игра уже заполнена");
            break;
        }

        // Присоединяем игрока
        if (game->join(m.from)) {
            client->current_game_id = game_id;

            // Определяем, кто является создателем
            std::string creator = game->get_player1();
            if (creator.empty())
                creator = game->get_player2();

            send_response_to(m.from, "JOIN_OK:Вы присоединились к игре");

            // Если создатель в игре, уведомляем его
            if (!creator.empty() && creator != m.from) {
                send_response_to(creator.c_str(), "OPPONENT_JOINED:Игрок присоединился");
            }

            // Инструкции по расстановке
            std::string instructions = "SHIP_PLACEMENT:\n"
                                       "Разместите корабли: place размер,x,y,ориентация(H/V)\n"
                                       "Корабли: 1x4, 2x3, 3x2, 4x1\n"
                                       "Пример: place 4,0,0,H\n"
                                       "Когда готовы: ready";

            send_response_to(m.from, instructions.c_str());
            send_response_to(creator.c_str(), instructions.c_str());
        } else {
            send_response_to(m.from, "JOIN_FAIL:Не удалось присоединиться");
        }
        break;
    }
    case MSG_ACCEPT: {
        // Старый формат принятия приглашения (для совместимости)
        int game_id = -1;
        if (sscanf(m.payload, "%d", &game_id) == 1) {
            Game* game = get_game(game_id);
            if (!game) {
                send_response_to(m.from, "ACCEPT_FAIL:Игра не найдена");
            } else if (game->join(m.from)) {
                ClientSlot* client = find_client(m.from);
                if (client) {
                    client->current_game_id = game_id;
                }

                send_response_to(m.from, "ACCEPT_OK:Вы присоединились");
                send_response_to(game->get_player1().c_str(),
                                 "OPPONENT_JOINED:Игрок принял приглашение");

                std::string instructions =
                    "SHIP_PLACEMENT:\n"
                    "Разместите корабли командой: place размер,x,y,ориентация(H/V)";

                send_response_to(m.from, instructions.c_str());
                send_response_to(game->get_player1().c_str(), instructions.c_str());
            } else {
                send_response_to(m.from, "ACCEPT_FAIL:Не удалось присоединиться");
            }
        } else {
            send_response_to(m.from, "ACCEPT_FAIL:Неверный формат. Используйте ID игры");
        }
        break;
    }
    case MSG_PLACE_SHIP: {
        std::cout << "DEBUG: Received PLACE_SHIP from " << m.from << " payload: " << m.payload
                  << std::endl;

        ClientSlot* client = find_client(m.from);
        if (!client) {
            send_response_to(m.from, "ERROR:Not registered");
            break;
        }

        if (client->current_game_id == -1) {
            send_response_to(m.from, "ERROR:Not in a game");
            break;
        }

        Game* game = get_game(client->current_game_id);
        if (!game) {
            send_response_to(m.from, "ERROR:Game not found");
            break;
        }

        uint8_t size, x, y;
        bool horizontal;

        // Парсим команду
        int s, x_pos, y_pos;
        char orientation;

        if (sscanf(m.payload, "%d,%d,%d,%c", &s, &x_pos, &y_pos, &orientation) != 4) {
            send_response_to(m.from, "SHIP_ERROR:Invalid format. Use: size,x,y,orientation(H/V)");
            break;
        }

        if (s < 1 || s > 4) {
            send_response_to(m.from, "SHIP_ERROR:Size must be 1-4");
            break;
        }

        if (x_pos < 0 || x_pos >= BOARD_SIZE || y_pos < 0 || y_pos >= BOARD_SIZE) {
            send_response_to(m.from, "SHIP_ERROR:Coordinates out of bounds");
            break;
        }

        if (orientation != 'H' && orientation != 'V') {
            send_response_to(m.from, "SHIP_ERROR:Orientation must be H or V");
            break;
        }

        size = static_cast<uint8_t>(s);
        x = static_cast<uint8_t>(x_pos);
        y = static_cast<uint8_t>(y_pos);
        horizontal = (orientation == 'H');

        // Пробуем разместить корабль
        if (game->place_ship(m.from, size, x, y, horizontal)) {
            send_response_to(m.from, "SHIP_PLACED:OK");
            std::cout << "DEBUG: Ship placed successfully" << std::endl;
        } else {
            send_response_to(m.from, "SHIP_ERROR:Cannot place ship here");
            std::cout << "DEBUG: Failed to place ship" << std::endl;
        }
        break;
    }
    case MSG_SETUP_COMPLETE: {
        handle_setup_complete(m);
        break;
    }
    case MSG_SHOT: {
        ClientSlot* client = find_client(m.from);
        if (!client || client->current_game_id == -1) {
            send_response_to(m.from, "ERROR:Not in a game");
            break;
        }

        Game* game = get_game(client->current_game_id);
        if (!game) {
            send_response_to(m.from, "ERROR:Game not found");
            break;
        }

        if (!game->is_game_active()) {
            send_response_to(m.from,
                             "ERROR:Game not active. Wait for both players to complete setup.");
            break;
        }

        uint8_t x, y;
        if (!parse_shot(m.payload, x, y)) {
            send_response_to(m.from, "SHOT_FAIL:Invalid format. Use: x,y");
            break;
        }

        if (!game->is_player_turn(m.from)) {
            send_response_to(m.from, "ERROR:Not your turn");
            break;
        }

        bool hit = game->make_shot(m.from, x, y);
        std::string shooter = m.from;
        std::string opponent =
            (game->get_player1() == shooter) ? game->get_player2() : game->get_player1();

        char buf[RESP_MAX];
        if (hit) {
            std::snprintf(buf, RESP_MAX, "SHOT_RESULT:HIT at %d,%d", x, y);
        } else {
            std::snprintf(buf, RESP_MAX, "SHOT_RESULT:MISS at %d,%d", x, y);
        }
        send_response_to(m.from, buf);

        std::snprintf(buf, RESP_MAX, "OPPONENT_SHOT:%s at %d,%d:%s", shooter.c_str(), x, y,
                      hit ? "HIT" : "MISS");
        send_response_to(opponent.c_str(), buf);

        std::string opponent_view = game->get_opponent_view(shooter);
        send_response_to(m.from, ("OPPONENT_VIEW_UPDATE:\n" + opponent_view).c_str());

        if (game->is_game_finished()) {
            std::string winner = game->get_winner();
            std::string loser =
                (winner == game->get_player1()) ? game->get_player2() : game->get_player1();

            send_response_to(winner.c_str(), "🎉 VICTORY:You won the game! 🎉");
            send_response_to(loser.c_str(), "💀 DEFEAT:You lost the game 💀");

            std::string winner_stats = game->get_statistics(winner);
            std::string loser_stats = game->get_statistics(loser);

            send_response_to(winner.c_str(), ("FINAL_STATS:\n" + winner_stats).c_str());
            send_response_to(loser.c_str(), ("FINAL_STATS:\n" + loser_stats).c_str());

            remove_game(client->current_game_id);
        } else {
            std::string current_turn = game->get_current_turn();
            if (current_turn == shooter && hit) {
                send_response_to(shooter.c_str(), "YOUR_TURN_AGAIN:You hit! Shoot again");
            } else if (current_turn == opponent) {
                send_response_to(opponent.c_str(), "YOUR_TURN:Make your move");
            }
        }
        break;
    }
    case MSG_GET_BOARD: {
        handle_get_board(m);
        break;
    }
    case MSG_GET_OPPONENT_BOARD: {
        handle_get_opponent_board(m);
        break;
    }
    case MSG_GAME_STATUS: {
        handle_game_status(m);
        break;
    }
    case MSG_SURRENDER: {
        handle_surrender(m);
        break;
    }
    case MSG_LEAVE_GAME: {
        ClientSlot* client = find_client(m.from);
        if (client && client->current_game_id != -1) {
            Game* game = get_game(client->current_game_id);
            if (game) {
                // Проверяем, является ли игрок участником этой игры
                if (!game->is_player_in_game(m.from)) {
                    send_response_to(m.from, "ERROR:You are not in this game");
                    break;
                }

                // Разрешаем выход в любом состоянии игры (кроме завершенной)
                if (game->is_game_finished()) {
                    send_response_to(m.from, "ERROR:Game already finished");
                    break;
                }

                // Определяем другого игрока для уведомления
                std::string other_player;
                if (game->get_player1() == m.from) {
                    other_player = game->get_player2();
                } else {
                    other_player = game->get_player1();
                }

                // Удаляем игрока из игры
                game->remove_player(m.from);

                // Если остался другой игрок - уведомляем его
                if (!other_player.empty()) {
                    std::string message =
                        "OPPONENT_LEFT:Игрок " + std::string(m.from) + " вышел из игры";
                    send_response_to(other_player.c_str(), message.c_str());

                    // Если игра перешла в состояние ожидания
                    if (game->is_waiting()) {
                        send_response_to(other_player.c_str(),
                                         "GAME_WAITING:Игра ожидает нового игрока");
                    }
                }

                // Сбрасываем клиентский слот
                client->current_game_id = -1;
                client->setup_complete = false;
                send_response_to(m.from, "LEFT_GAME:Вы вышли из игры");

                // Если игра полностью пустая, удаляем ее
                if (!game->get_player1()[0] && !game->get_player2()[0]) {
                    remove_game(client->current_game_id);
                } else {
                    // Обновляем слот другого игрока
                    if (!other_player.empty()) {
                        ClientSlot* other_client = find_client(other_player.c_str());
                        if (other_client) {
                            other_client->setup_complete = false;
                        }
                    }
                }
            }
        } else {
            send_response_to(m.from, "ERROR:You are not in any game");
        }
        break;
    }
    case MSG_QUIT: {
        ClientSlot* c = find_client(m.from);
        if (c) {
            if (c->current_game_id != -1) {
                Game* game = get_game(c->current_game_id);
                if (game && !game->is_game_finished()) {
                    std::string opponent =
                        (game->get_player1() == m.from) ? game->get_player2() : game->get_player1();
                    send_response_to(opponent.c_str(), "OPPONENT_DISCONNECTED:You win by forfeit");
                    remove_game(c->current_game_id);
                }
            }

            c->used = false;
            c->has_response = false;
            c->current_game_id = -1;
            c->setup_complete = false;
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
        pthread_mutex_lock(&root->mutex);
        while (root->q_head == root->q_tail) {
            pthread_cond_wait(&root->server_cond, &root->mutex);
        }

        Message m = root->queue[root->q_head];
        root->queue[root->q_head].used = false;
        root->q_head = (root->q_head + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&root->mutex);

        if (m.used) {
            handle_message(m);
        }
    }
}