#include "Client.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

Client::Client()
    : shm(false), root(shm.root()), current_game_id(-1), in_game(false), in_setup(false),
      pending_invite_id(-1), rng(std::random_device{}()) {
    if (!root)
        throw std::runtime_error("Cannot open shared memory; run server first");
}

void Client::force_check_state() {
    if (current_game_id != -1) {
        std::cout << "🔄 Проверяем состояние игры...\n";

        Message m;
        std::memset(&m, 0, sizeof(m));
        std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
        m.type = MSG_GAME_STATUS;

        clear_response_buffer();

        if (enqueue_message(m)) {
            std::string resp;
            if (wait_for_response(resp, 2000)) {
                if (resp.find("ERROR") != std::string::npos ||
                    resp.find("GAME_REMOVED") != std::string::npos ||
                    resp.find("Not in a game") != std::string::npos) {
                    std::cout << "⚠️ Игра не найдена, сбрасываем состояние\n";
                    in_game = false;
                    in_setup = false;
                    current_game_id = -1;

                    ClientSlot* slot = my_slot();
                    if (slot) {
                        slot->current_game_id = -1;
                        slot->setup_complete = false;
                    }
                } else {
                    std::cout << "✅ Игра существует: " << resp.substr(0, 50) << "...\n";
                }
            } else {
                std::cout << "❌ Нет ответа от сервера, сбрасываем состояние\n";
                in_game = false;
                in_setup = false;
                current_game_id = -1;
            }
        }
    }
}

bool Client::is_valid_position(uint8_t x, uint8_t y, uint8_t size, bool horizontal, const std::vector<std::pair<uint8_t, uint8_t>>& placed_positions) {

    if (horizontal) {
        if (x + size > BOARD_SIZE)
            return false;
    } else {
        if (y + size > BOARD_SIZE)
            return false;
    }

    for (int i = 0; i < size; i++) {
        int cx = horizontal ? x + i : x;
        int cy = horizontal ? y : y + i;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = cx + dx;
                int ny = cy + dy;

                if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    for (const auto& pos : placed_positions) {
                        if (pos.first == nx && pos.second == ny) {
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}

void Client::auto_place_ships() {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  АВТОМАТИЧЕСКАЯ РАССТАНОВКА КОРАБЛЕЙ\n";
    std::cout << std::string(50, '=') << "\n";

    std::vector<std::string> ships = {"4,0,0,H", "3,0,5,H", "3,0,8,H", "2,8,8,H", "2,5,5,V",
                                      "2,8,4,H", "1,6,0,H", "1,6,2,H", "1,3,2,H", "1,9,0,H"};

    int placed_ships = 0;
    int total_ships = ships.size();

    clear_response_buffer();

    for (const auto& ship_cmd : ships) {
        std::cout << "Размещаем корабль: " << ship_cmd << "... ";

        Message m;
        std::memset(&m, 0, sizeof(m));
        std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
        m.type = MSG_PLACE_SHIP;
        std::strncpy(m.payload, ship_cmd.c_str(), CMD_MAX - 1);

        if (!enqueue_message(m)) {
            std::cout << "❌ Очередь переполнена\n";
            continue;
        }

        std::string resp;
        if (wait_for_response(resp, 2000)) {
            if (resp.find("SHIP_PLACED") != std::string::npos ||
                resp.find("OK") != std::string::npos ||
                resp.find("YOUR_BOARD") != std::string::npos) {
                placed_ships++;
                std::cout << "✅ Успешно\n";
            } else {
                std::cout << "❌ Ошибка: " << resp.substr(0, 50) << "\n";
            }
        } else {
            std::cout << "❌ Нет ответа от сервера\n";
        }

        usleep(100 * 1000);
    }

    std::cout << "\n" << std::string(50, '-') << "\n";
    std::cout << "  РАССТАНОВКА ЗАВЕРШЕНА\n";
    std::cout << "  Размещено кораблей: " << placed_ships << "/" << total_ships << "\n";

    if (placed_ships == total_ships) {
        std::cout << "  ✅ Все корабли успешно размещены!\n";

        std::cout << "  Показываем поле...\n";
        Message board_msg;
        std::memset(&board_msg, 0, sizeof(board_msg));
        std::strncpy(board_msg.from, login.c_str(), LOGIN_MAX - 1);
        board_msg.type = MSG_GET_BOARD;

        clear_response_buffer();

        if (enqueue_message(board_msg)) {
            std::string resp;
            if (wait_for_response(resp, 2000)) {
                std::cout << "\n" << resp << "\n";
            }
        }

        std::cout << "  Для завершения расстановки введите 'ready'\n";
    } else {
        std::cout << "  ⚠️  Не все корабли удалось разместить\n";
        std::cout << "  Завершите расстановку вручную\n";
    }

    std::cout << std::string(50, '=') << "\n\n";
}

Client::~Client() {
}

ClientSlot* Client::my_slot() {
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (root->clients[i].used &&
            std::strncmp(root->clients[i].login, login.c_str(), LOGIN_MAX) == 0) {
            return &root->clients[i];
        }
    }
    return nullptr;
}

bool Client::enqueue_message(const Message& m) {
    pthread_mutex_lock(&root->mutex);

    size_t next_tail = (root->q_tail + 1) % QUEUE_SIZE;
    if (next_tail == root->q_head) {
        pthread_mutex_unlock(&root->mutex);
        return false;
    }

    root->queue[root->q_tail] = m;
    root->queue[root->q_tail].used = true;
    root->q_tail = next_tail;

    pthread_cond_signal(&root->server_cond);
    pthread_mutex_unlock(&root->mutex);
    return true;
}

bool Client::wait_for_response(std::string& out, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start)
               .count() < timeout_ms) {

        pthread_mutex_lock(&root->mutex);
        ClientSlot* slot = my_slot();

        if (!slot) {
            pthread_mutex_unlock(&root->mutex);
            usleep(100 * 1000);
            continue;
        }

        if (slot->has_response) {
            out = slot->response;
            slot->has_response = false;
            std::memset(slot->response, 0, RESP_MAX);
            pthread_mutex_unlock(&root->mutex);
            return true;
        }

        pthread_mutex_unlock(&root->mutex);
        usleep(100 * 1000);
    }

    return false;
}

bool Client::check_for_async_messages() {
    std::string resp;
    if (wait_for_response(resp, 20)) {
        std::cout << "[DEBUG] check_for_async_messages got: "
                  << (resp.length() > 50 ? resp.substr(0, 50) + "..." : resp) << std::endl;
        handle_game_response(resp);
        return true;
    }
    return false;
}

void Client::handle_game_response(const std::string& response) {
    if (response.find("GAME_REMOVED:") == 0) {
        std::cout << "\n🗑️ " << response.substr(13) << "\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
        pending_invite_game_name.clear();
        pending_invite_from.clear();
        pending_invite_id = -1;

        ClientSlot* slot = my_slot();
        if (slot) {
            slot->current_game_id = -1;
            slot->setup_complete = false;
        }
    } else if (response.find("GAME_CREATED:") == 0) {
        std::cout << "\n✅ " << response.substr(13) << "\n";
        in_game = true;
        in_setup = true;

        size_t id_pos = response.find("ID:");
        if (id_pos != std::string::npos) {
            std::string id_str = response.substr(id_pos + 3);
            id_str.erase(std::remove_if(id_str.begin(), id_str.end(),
                                        [](char c) { return !std::isdigit(c); }),
                         id_str.end());
            if (!id_str.empty()) {
                current_game_id = std::stoi(id_str);
            }
        }
    }

    if (response.find("INVITE:") == 0) {
        std::cout << "🎯 DEBUG: Processing invitation: " << response << std::endl;

        size_t first_colon = response.find(':');
        size_t second_colon = response.find(':', first_colon + 1);
        size_t third_colon = response.find(':', second_colon + 1);

        if (first_colon != std::string::npos && second_colon != std::string::npos &&
            third_colon != std::string::npos) {

            std::string inviter = response.substr(first_colon + 1, second_colon - first_colon - 1);
            std::string game_name =
                response.substr(second_colon + 1, third_colon - second_colon - 1);
            std::string game_id_str = response.substr(third_colon + 1);

            game_id_str.erase(std::remove_if(game_id_str.begin(), game_id_str.end(),
                                             [](char c) { return !std::isdigit(c); }),
                              game_id_str.end());

            std::cout << "\n" << std::string(50, '=') << "\n";
            std::cout << "  🎮 ПРИГЛАШЕНИЕ В ИГРУ!\n";
            std::cout << std::string(50, '=') << "\n";
            std::cout << "  Игра: " << game_name << "\n";
            std::cout << "  Приглашает: " << inviter << "\n";
            std::cout << "  ID: " << game_id_str << "\n\n";
            std::cout << "  Принять: join " << game_id_str << "\n";
            std::cout << "  Отклонить: ignore\n";
            std::cout << std::string(50, '=') << "\n";

            pending_invite_from = inviter;
            pending_invite_game_name = game_name;

            try {
                pending_invite_id = std::stoi(game_id_str);
            } catch (...) {
                pending_invite_id = -1;
            }

            show_main_menu();
        }
    } else if (response.find("OPPONENT_JOINED:") == 0) {
        std::cout << "\n🎯 Противник присоединился! Начинайте расставлять корабли.\n";
    } else if (response.find("YOUR_BOARD:") == 0) {
        std::cout << "\n" << response.substr(11) << "\n";
    } else if (response.find("OPPONENT_VIEW:") == 0) {
        std::cout << "\n" << response.substr(14) << "\n";
    } else if (response.find("YOUR_TURN:") == 0) {
        std::cout << "\n🎯 ВАШ ХОД! " << response.substr(10) << "\n";
    } else if (response.find("YOUR_TURN_AGAIN:") == 0) {
        std::cout << "\n🎯 ВАШ ХОД СНОВА! " << response.substr(16) << "\n";
    } else if (response.find("OPPONENT_SHOT:") == 0) {
        std::cout << "\n💥 ПРОТИВНИК СТРЕЛЯЕТ: " << response.substr(14) << "\n";
    } else if (response.find("SHOT_RESULT:") == 0) {
        std::cout << "\n📊 РЕЗУЛЬТАТ ВЫСТРЕЛА: " << response.substr(12) << "\n";
    } else if (response.find("VICTORY:") == 0) {
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "  🎉 ПОБЕДА! 🎉\n";
        std::cout << "  " << response.substr(8) << "\n";
        std::cout << std::string(50, '=') << "\n\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } else if (response.find("DEFEAT:") == 0) {
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "  💀 ПОРАЖЕНИЕ 💀\n";
        std::cout << "  " << response.substr(7) << "\n";
        std::cout << std::string(50, '=') << "\n\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } else if (response.find("ACCEPT_OK") == 0 || response.find("JOIN_OK") == 0) {
        std::cout << "\n✅ Вы присоединились к игре!\n";
        in_game = true;
        in_setup = true;
        pending_invite_game_name.clear();
        pending_invite_from.clear();
        pending_invite_id = -1;
    } else if (response.find("SHIP_PLACEMENT:") == 0) {
        std::cout << "\n" << response << "\n";
        if (!in_game) {
            in_game = true;
            in_setup = true;
        }
    } else if (response.find("LEFT_GAME:") == 0) {
        std::cout << "\n🚪 " << response.substr(10) << "\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } else if (response.find("GAME_CREATED") == 0) {
        std::cout << "\n✅ " << response.substr(13) << "\n";
        in_game = true;
        in_setup = true;

        size_t id_pos = response.find("ID:");
        if (id_pos != std::string::npos) {
            std::string id_str = response.substr(id_pos + 3);
            id_str.erase(std::remove_if(id_str.begin(), id_str.end(),
                                        [](char c) { return !std::isdigit(c); }),
                         id_str.end());
            if (!id_str.empty()) {
                current_game_id = std::stoi(id_str);
            }
        }
    }
    if (response.find("INVITE_SENT") == 0) {
        std::cout << "\n✅ " << response.substr(12) << "\n";
        in_game = true;
        in_setup = true;
        std::cout << "🎮 Вы вошли в игру. Начинайте расстановку кораблей!\n";
    } else if (response.find("SETUP_COMPLETE") == 0) {
        std::cout << "\n✅ " << response.substr(15) << "\n";
        in_setup = false;
    } else if (response.find("OPPONENT_VIEW_UPDATE:") == 0) {
        std::cout << "\n" << response.substr(21) << "\n";
    } else if (response.find("GAME_STATUS:") == 0) {
        std::cout << "\n" << response.substr(12) << "\n";
    } else if (response.find("FINAL_STATS:") == 0) {
        std::cout << "\n📊 " << response.substr(12) << "\n";
    } else if (response.find("ERROR:") == 0 || response.find("FAIL:") == 0 ||
               response.find("INVALID") == 0 || response.find("SHIP_ERROR") == 0) {
        std::cout << "\n❌ " << response << "\n";
    } else if (response.find("REGISTERED:") == 0) {
        std::cout << "\n✅ " << response.substr(11) << "\n";
    } else if (response.find("LEFT_GAME:") == 0) {
        std::cout << "\n🚪 " << response.substr(10) << "\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } else if (!response.empty() && response.find("===") != 0) {
        if (response != "\n" && response.length() > 2) {
            std::cout << "\n" << response << "\n";
        }
    }
}
void Client::show_main_menu() {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  МОРСКОЙ БОЙ\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  1 - Список игроков и игр\n";
    std::cout << "  2 - Создать публичную игру\n";
    std::cout << "  3 - Присоединиться к игре\n";
    std::cout << "  4 - Пригласить игрока\n";
    std::cout << "  5 - Проверить приглашения\n";
    std::cout << "  6 - Выйти\n";

    if (pending_invite_id != -1) {
        std::cout << std::string(50, '=') << "\n";
        std::cout << "  📨 АКТИВНОЕ ПРИГЛАШЕНИЕ:\n";
        std::cout << "  Игра: " << pending_invite_game_name << "\n";
        std::cout << "  От: " << pending_invite_from << "\n";
        std::cout << "  ID: " << pending_invite_id << "\n";
        std::cout << "  Принять: join " << pending_invite_id << "\n";
        std::cout << std::string(50, '=') << "\n";
    }

    std::cout << "  Выберите действие: ";
}

void Client::place_ships_interactive() {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  РАССТАНОВКА КОРАБЛЕЙ\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  Формат: размер,x,y,ориентация(H/V)\n";
    std::cout << "  Пример: 4,0,0,H\n\n";
    std::cout << "  Корабли для размещения:\n";
    std::cout << "    1 авианосец (4 клеток)\n";
    std::cout << "    2 линкора (3 клетки)\n";
    std::cout << "    3 крейсера (2 клетки)\n";
    std::cout << "    4 эсминца (1 клетки)\n";
    std::cout << std::string(50, '-') << "\n";
    std::cout << "  Команды:\n";
    std::cout << "    auto - автоматическая расстановка\n";
    std::cout << "    ready - готов к игре\n";
    std::cout << "    board - посмотреть поле\n";
    std::cout << "    invite <логин> - пригласить игрока в эту игру\n";
    std::cout << "    menu - выйти в меню\n";
    std::cout << std::string(50, '-') << "\n";
}

void Client::show_game_menu() {
    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "  ИГРА В ПРОЦЕССЕ\n";
    std::cout << std::string(40, '=') << "\n";
    std::cout << "  1 - Сделать выстрел\n";
    std::cout << "  2 - Посмотреть свое поле\n";
    std::cout << "  3 - Посмотреть поле противника\n";
    std::cout << "  4 - Статус игры\n";
    std::cout << "  5 - Сдаться\n";
    std::cout << "  6 - Выйти в меню\n";
    std::cout << std::string(40, '-') << "\n";
    std::cout << "  Выберите действие: ";
}

void Client::clear_response_buffer() {
    pthread_mutex_lock(&root->mutex);
    ClientSlot* slot = my_slot();
    if (slot && slot->has_response) {
        std::string resp = slot->response;
        std::cout << "[DEBUG] Buffer has: " << resp << std::endl;

        if (resp.find("INVITE:") == 0) {
            std::cout << "[DEBUG] Keeping invitation in buffer" << std::endl;
        } else {
            std::cout << "[DEBUG] Clearing buffer" << std::endl;
            slot->has_response = false;
            std::memset(slot->response, 0, RESP_MAX);
        }
    }
    pthread_mutex_unlock(&root->mutex);
}

void Client::run() {
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  ДОБРО ПОЖАЛОВАТЬ В МОРСКОЙ ГОЙ!\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  Введите ваш логин: ";
    std::getline(std::cin, login);

    if (login.empty()) {
        std::cerr << "\n❌ Логин не может быть пустым\n";
        return;
    }

    Message reg;
    std::memset(&reg, 0, sizeof(reg));
    std::strncpy(reg.from, login.c_str(), LOGIN_MAX - 1);
    reg.type = MSG_REGISTER;

    std::cout << "\n🔗 Регистрация...\n";
    if (!enqueue_message(reg)) {
        std::cerr << "❌ Не удалось отправить запрос\n";
        return;
    }

    std::string resp;
    if (wait_for_response(resp, 2000)) {
        handle_game_response(resp);
    }

    bool running = true;

    while (running) {
        static int check_counter = 0;
        check_counter++;
        if (check_counter >= 10 && current_game_id != -1) {
            force_check_state();
            check_counter = 0;
        }

        for (int i = 0; i < 3; i++) {
            if (check_for_async_messages()) {
                break;
            }
            usleep(50 * 1000);
        }

        if (!in_game) {
            show_main_menu();
            std::string line;
            std::getline(std::cin, line);

            if (line.find("join ") == 0 && !pending_invite_game_name.empty()) {
                std::string game_id_str = line.substr(5);

                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_JOIN;
                std::strncpy(m.payload, game_id_str.c_str(), CMD_MAX - 1);

                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        handle_game_response(resp);
                    }
                }
            } else if (line == "ignore" && !pending_invite_game_name.empty()) {
                std::cout << "\n❌ Приглашение проигнорировано\n";
                pending_invite_game_name.clear();
                pending_invite_from.clear();
                pending_invite_id = -1;
            } else if (line == "1") {
                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_LIST;

                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        std::cout << resp << "\n";
                    }
                }
            } else if (line == "2") {
                std::cout << "\n🎮 Введите имя для новой игры: ";
                std::string game_name;
                std::getline(std::cin, game_name);

                if (game_name.empty()) {
                    std::cout << "\n❌ Имя игры не может быть пустым\n";
                    continue;
                }

                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_CREATE;
                std::strncpy(m.payload, game_name.c_str(), CMD_MAX - 1);

                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        handle_game_response(resp);
                    }
                }
            } else if (line == "3") {
                std::cout << "\n🎮 Введите имя или ID игры: ";
                std::string game_target;
                std::getline(std::cin, game_target);

                if (game_target.empty()) {
                    std::cout << "\n❌ Имя/ID не может быть пустым\n";
                    continue;
                }

                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_JOIN;
                std::strncpy(m.payload, game_target.c_str(), CMD_MAX - 1);

                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        handle_game_response(resp);
                    }
                }
            } else if (line == "4") {
                std::cout << "\n👥 Введите логин игрока для приглашения: ";
                std::string target;
                std::getline(std::cin, target);

                std::string game_name = login + "_vs_" + target + "_private";

                Message create_msg;
                std::memset(&create_msg, 0, sizeof(create_msg));
                std::strncpy(create_msg.from, login.c_str(), LOGIN_MAX - 1);
                create_msg.type = MSG_CREATE;
                std::strncpy(create_msg.payload, game_name.c_str(), CMD_MAX - 1);

                if (!enqueue_message(create_msg)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                    continue;
                }

                std::string resp;
                if (wait_for_response(resp, 2000)) {
                    if (resp.find("GAME_CREATED") != std::string::npos) {
                        Message invite_msg;
                        std::memset(&invite_msg, 0, sizeof(invite_msg));
                        std::strncpy(invite_msg.from, login.c_str(), LOGIN_MAX - 1);
                        invite_msg.type = MSG_INVITE_TO_GAME;
                        std::strncpy(invite_msg.payload, target.c_str(), CMD_MAX - 1);

                        if (!enqueue_message(invite_msg)) {
                            std::cout << "\n❌ Очередь переполнена\n";
                        } else {
                            std::string invite_resp;
                            if (wait_for_response(invite_resp, 2000)) {
                                handle_game_response(invite_resp);
                                if (in_game && in_setup) {
                                    place_ships_interactive();
                                }
                            }
                        }
                    } else {
                        handle_game_response(resp);
                    }
                }

            } else if (line == "5") {
                std::cout << "\n🔄 Проверяем приглашения...\n";

                bool found_invitation = false;
                for (int i = 0; i < 3; i++) {
                    if (check_for_async_messages()) {
                        found_invitation = true;
                    }
                    usleep(100 * 1000);
                }

                if (!found_invitation && pending_invite_id == -1) {
                    std::cout << "📭 Приглашений нет\n";
                } else if (pending_invite_id != -1) {
                    std::cout << "✅ Есть активное приглашение (ID: " << pending_invite_id << ")\n";
                    std::cout << "  Принять: join " << pending_invite_id << "\n";
                }
            } else if (line == "6") {
                force_check_state();

                std::cout << "\n🚪 Вы уверены? (да/нет): ";
                std::string confirm;
                std::getline(std::cin, confirm);

                std::string confirm_lower = confirm;
                std::transform(confirm_lower.begin(), confirm_lower.end(), confirm_lower.begin(), ::tolower);

                if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" ||
                    confirm_lower == "д") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_QUIT;
                    enqueue_message(m);
                    running = false;
                    std::cout << "\n👋 Выход...\n";
                }
            } else if (line.find("join ") == 0) {
                std::string game_id_str = line.substr(5);

                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_JOIN;
                std::strncpy(m.payload, game_id_str.c_str(), CMD_MAX - 1);

                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        handle_game_response(resp);
                        pending_invite_game_name.clear();
                        pending_invite_from.clear();
                        pending_invite_id = -1;
                    }
                }
            } else if (line == "ignore") {
                std::cout << "\n❌ Приглашение отклонено\n";
                pending_invite_game_name.clear();
                pending_invite_from.clear();
                pending_invite_id = -1;
            } else {
                std::cout << "\n❌ Неверная команда\n";
            }
        } else {
            if (in_setup) {
                if (!check_for_async_messages()) {
                    place_ships_interactive();
                }

                std::cout << "\n⚓ Команда: ";
                std::string command;
                std::getline(std::cin, command);

                clear_response_buffer();

                std::string cmd_lower = command;
                std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

                if (cmd_lower == "ready" || cmd_lower == "готово") {
                    clear_response_buffer();

                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_SETUP_COMPLETE;

                    std::cout << "🔄 Отправляем 'ready' на сервер...\n";

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        std::string resp;
                        if (wait_for_response(resp, 3000)) {
                            std::cout << "📥 Получен ответ от сервера\n";
                            handle_game_response(resp);
                        } else {
                            std::cout << "❌ Нет ответа от сервера\n";
                        }
                    }
                } else if (cmd_lower == "auto") {
                    std::cout << "\n🔄 Запускаем автоматическую расстановку...\n";
                    auto_place_ships();
                } else if (cmd_lower == "board") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_GET_BOARD;

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                }

                else if (cmd_lower.find("invite ") == 0) {
                    std::string target = command.substr(7);

                    if (target.empty() || target == login) {
                        std::cout << "\n❌ Неверный логин\n";
                        continue;
                    }

                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_INVITE_TO_GAME;
                    std::strncpy(m.payload, target.c_str(), CMD_MAX - 1);

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        std::string resp;
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                }

                else if (cmd_lower == "menu") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_LEAVE_GAME;
                    enqueue_message(m);

                    in_game = false;
                    in_setup = false;
                    current_game_id = -1;
                    std::cout << "\n🏳️ Вы вышли из игры\n";
                } else {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_PLACE_SHIP;
                    std::strncpy(m.payload, command.c_str(), CMD_MAX - 1);

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                }
            } else {
                bool has_async = check_for_async_messages();

                if (has_async) {
                    std::cout << "\nНажмите Enter для продолжения...";
                    std::string dummy;
                    std::getline(std::cin, dummy);
                }

                show_game_menu();

                std::string line;
                std::getline(std::cin, line);

                if (line == "1") {
                    std::cout << "\n🎯 Координаты выстрела (x,y): ";
                    std::string shot;
                    std::getline(std::cin, shot);

                    clear_response_buffer();

                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_SHOT;
                    std::strncpy(m.payload, shot.c_str(), CMD_MAX - 1);

                    std::cout << "🔄 Отправляем выстрел...\n";

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        std::string resp;
                        if (wait_for_response(resp, 3000)) {
                            std::cout << "📥 Ответ сервера получен\n";
                            handle_game_response(resp);
                        } else {
                            std::cout << "❌ Нет ответа от сервера\n";
                        }
                    }
                } else if (line == "2") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_GET_BOARD;

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                } else if (line == "3") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_GET_OPPONENT_BOARD;

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                } else if (line == "4") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_GAME_STATUS;

                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                } else if (line == "5") {

                    force_check_state();

                    std::cout << "\n🏳️ Вы уверены? (да/нет): ";
                    std::string confirm;
                    std::getline(std::cin, confirm);

                    std::string confirm_lower = confirm;
                    std::transform(confirm_lower.begin(), confirm_lower.end(),
                                   confirm_lower.begin(), ::tolower);

                    if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" ||
                        confirm_lower == "д") {
                        Message m;
                        std::memset(&m, 0, sizeof(m));
                        std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                        m.type = MSG_SURRENDER;

                        if (!enqueue_message(m)) {
                            std::cout << "\n❌ Очередь переполнена\n";
                        } else {
                            if (wait_for_response(resp, 2000)) {
                                handle_game_response(resp);
                            }
                        }
                    }
                } else if (line == "6") {
                    std::cout << "\n⚠️ Выход приравнивается к сдаче!\n";
                    std::cout << "Вы уверены? (да/нет): ";
                    std::string confirm;
                    std::getline(std::cin, confirm);

                    std::string confirm_lower = confirm;
                    std::transform(confirm_lower.begin(), confirm_lower.end(),
                                   confirm_lower.begin(), ::tolower);

                    if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" ||
                        confirm_lower == "д") {
                        Message m;
                        std::memset(&m, 0, sizeof(m));
                        std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                        m.type = MSG_LEAVE_GAME;

                        clear_response_buffer();

                        if (!enqueue_message(m)) {
                            std::cout << "\n❌ Очередь переполнена\n";
                        } else {
                            std::string resp;
                            if (wait_for_response(resp, 3000)) {
                                handle_game_response(resp);
                            } else {
                                std::cout
                                    << "❌ Нет ответа от сервера, сбрасываем состояние локально\n";
                                in_game = false;
                                in_setup = false;
                                current_game_id = -1;
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  ИГРА ЗАВЕРШЕНА\n";
    std::cout << "  Спасибо за игру!\n";
    std::cout << std::string(50, '=') << "\n";
}