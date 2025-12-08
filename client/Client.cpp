#include "Client.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>

Client::Client() : shm(false), root(shm.root()), current_game_id(-1), 
                   in_game(false), in_setup(false), pending_invite_id(-1), setup_show_menu(false) {
    if (!root) throw std::runtime_error("Cannot open shared memory; run server first");
}

Client::~Client() {}

ClientSlot* Client::my_slot() {
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (root->clients[i].used && std::strncmp(root->clients[i].login, login.c_str(), LOGIN_MAX) == 0) {
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

bool Client::wait_for_response(std::string &out, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        
        pthread_mutex_lock(&root->mutex);
        ClientSlot* slot = my_slot();
        
        if (!slot) {
            pthread_mutex_unlock(&root->mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    return false;
}

bool Client::check_for_async_messages() {
    std::string resp;
    if (wait_for_response(resp, 0)) {
        handle_game_response(resp);
        return true;
    }
    return false;
}

void Client::handle_game_response(const std::string& response) {
    if (response.find("INVITE_FROM:") == 0) {
        std::string invite_str = response.substr(12);
        
        size_t game_pos = invite_str.find("GAME:");
        size_t id_pos = invite_str.find("ID:");
        
        if (game_pos != std::string::npos && id_pos != std::string::npos) {
            std::string inviter = invite_str.substr(0, game_pos - 1);
            std::string game_name = invite_str.substr(game_pos + 5, id_pos - game_pos - 6);
            std::string game_id_str = invite_str.substr(id_pos + 3);
            
            std::cout << "\n" << std::string(50, '=') << "\n";
            std::cout << "  🎮 ПРИГЛАШЕНИЕ В ИГРУ!\n";
            std::cout << std::string(50, '=') << "\n";
            std::cout << "  Игра: " << game_name << "\n";
            std::cout << "  ID: " << game_id_str << "\n";
            std::cout << "  Приглашает: " << inviter << "\n\n";
            std::cout << "  Принять: join " << game_id_str << "\n";
            std::cout << "  Игнорировать: ignore\n";
            std::cout << std::string(50, '=') << "\n";
            
            pending_invite_game_name = game_name;
            pending_invite_from = inviter;
            pending_invite_id = std::stoi(game_id_str);
        }
    } 
    else if (response.find("OPPONENT_JOINED:") == 0) {
        std::cout << "\n🎯 Противник присоединился! Начинайте расставлять корабли.\n";
    } 
    else if (response.find("YOUR_BOARD:") == 0) {
        std::cout << "\n" << response.substr(11) << "\n";
    } 
    else if (response.find("OPPONENT_VIEW:") == 0) {
        std::cout << "\n" << response.substr(14) << "\n";
    }
    else if (response.find("YOUR_TURN:") == 0) {
        std::cout << "\n🎯 ВАШ ХОД! " << response.substr(10) << "\n";
    }
    else if (response.find("OPPONENT_SHOT:") == 0) {
        std::cout << "\n💥 ПРОТИВНИК СТРЕЛЯЕТ: " << response.substr(14) << "\n";
    } 
    else if (response.find("SHOT_RESULT:") == 0) {
        std::cout << "\n📊 РЕЗУЛЬТАТ: " << response.substr(12) << "\n";
    } 
    else if (response.find("VICTORY:") == 0) {
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "  🎉 ПОБЕДА! 🎉\n";
        std::cout << "  " << response.substr(8) << "\n";
        std::cout << std::string(50, '=') << "\n\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } 
    else if (response.find("DEFEAT:") == 0) {
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "  💀 ПОРАЖЕНИЕ 💀\n";
        std::cout << "  " << response.substr(7) << "\n";
        std::cout << std::string(50, '=') << "\n\n";
        in_game = false;
        in_setup = false;
        current_game_id = -1;
    } 
    else if (response.find("ACCEPT_OK") == 0 || response.find("JOIN_OK") == 0) {
        std::cout << "\n✅ Вы присоединились к игре!\n";
        in_game = true;
        in_setup = true;
        pending_invite_game_name.clear();
        pending_invite_from.clear();
        pending_invite_id = -1;
        // НЕ вызываем place_ships_interactive() здесь - это будет сделано в run()
    }
    else if (response.find("SHIP_PLACEMENT:") == 0) {
        std::cout << "\n" << response << "\n";
        // Если получили инструкции по расстановке, переключаемся в режим игры
        if (!in_game) {
            in_game = true;
            in_setup = true;
        }
        // Показываем инструкции по расстановке
    }
    // В методе handle_game_response добавьте:
    else if (response.find("GAME_CREATED") == 0) {
        std::cout << "\n✅ " << response.substr(13) << "\n";
        
        // После создания игры автоматически переходим в режим расстановки
        in_game = true;
        in_setup = true;
        
        // Ищем ID игры в ответе
        size_t id_pos = response.find("ID:");
        if (id_pos != std::string::npos) {
            std::string id_str = response.substr(id_pos + 3);
            // Убираем все нецифры
            id_str.erase(std::remove_if(id_str.begin(), id_str.end(), 
                        [](char c) { return !std::isdigit(c); }), id_str.end());
            if (!id_str.empty()) {
                current_game_id = std::stoi(id_str);
            }
        }
        
        // Показываем инструкции по расстановке
        std::cout << "\n" << std::string(50, '=') << "\n";
        std::cout << "  РАССТАНОВКА КОРАБЛЕЙ\n";
        std::cout << std::string(50, '=') << "\n";
        std::cout << "  Формат: размер,x,y,ориентация(H/V)\n";
        std::cout << "  Пример: 5,0,0,H\n\n";
        std::cout << "  Корабли для размещения:\n";
        std::cout << "    1 авианосец (5 клеток)\n";
        std::cout << "    2 линкора (4 клетки)\n";
        std::cout << "    3 крейсера (3 клетки)\n";
        std::cout << "    4 эсминца (2 клетки)\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  Команды:\n";
        std::cout << "    ready - готов к игре\n";
        std::cout << "    board - посмотреть поле\n";
        std::cout << "    menu - выйти в меню\n";
        std::cout << std::string(50, '-') << "\n";
    }
    else if (response.find("INVITE_SENT") == 0) {
        std::cout << "\n✅ " << response.substr(12) << "\n";
    }
    else if (response.find("SETUP_COMPLETE") == 0) {
        std::cout << "\n✅ " << response.substr(15) << "\n";
        in_setup = false;
    }
    else if (response.find("ERROR:") == 0 || 
             response.find("FAIL:") == 0 || 
             response.find("INVALID") == 0) {
        std::cout << "\n❌ " << response << "\n";
    }
    else if (response.find("REGISTERED:") == 0) {
        std::cout << "\n✅ " << response.substr(11) << "\n";
    }
    else if (!response.empty() && response.find("===") != 0) {
        std::cout << "\n" << response << "\n";
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
    std::cout << "  5 - Выйти\n";
    
    if (!pending_invite_game_name.empty()) {
        std::cout << std::string(50, '-') << "\n";
        std::cout << "  📨 Приглашение: " << pending_invite_game_name << "\n";
        std::cout << "  От: " << pending_invite_from << "\n";
        std::cout << "  Принять: join " << pending_invite_id << "\n";
        std::cout << "  Игнорировать: ignore\n";
    }
    
    std::cout << std::string(50, '-') << "\n";
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
    std::cout << "    ready - готов к игре\n";
    std::cout << "    board - посмотреть поле\n";
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

void Client::run() {
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  ДОБРО ПОЖАЛОВАТЬ В МОРСКОЙ БОЙ!\n";
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
        // Проверяем асинхронные сообщения
        check_for_async_messages();
        
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
            }
            else if (line == "ignore" && !pending_invite_game_name.empty()) {
                std::cout << "\n❌ Приглашение проигнорировано\n";
                pending_invite_game_name.clear();
                pending_invite_from.clear();
                pending_invite_id = -1;
            }
            else if (line == "1") {
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
            }
            else if (line == "2") {
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
            }
            else if (line == "3") {
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
            }
            else if (line == "4") {
                std::cout << "\n👥 Введите логин игрока: ";
                std::string target;
                std::getline(std::cin, target);
                
                if (target.empty()) {
                    std::cout << "\n❌ Логин не может быть пустым\n";
                    continue;
                }
                
                if (target == login) {
                    std::cout << "\n❌ Нельзя пригласить самого себя\n";
                    continue;
                }
                
                Message m;
                std::memset(&m, 0, sizeof(m));
                std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                m.type = MSG_INVITE;
                std::strncpy(m.payload, target.c_str(), CMD_MAX - 1);
                
                if (!enqueue_message(m)) {
                    std::cout << "\n❌ Очередь переполнена\n";
                } else {
                    if (wait_for_response(resp, 2000)) {
                        handle_game_response(resp);
                    }
                }
            }
            else if (line == "5") {
                std::cout << "\n🚪 Вы уверены? (да/нет): ";
                std::string confirm;
                std::getline(std::cin, confirm);
                
                std::string confirm_lower = confirm;
                std::transform(confirm_lower.begin(), confirm_lower.end(), confirm_lower.begin(), ::tolower);
                
                if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" || confirm_lower == "д") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_QUIT;
                    enqueue_message(m);
                    running = false;
                    std::cout << "\n👋 Выход...\n";
                }
            }
            else {
                std::cout << "\n❌ Неверная команда\n";
            }
        } 
        else {
            // В режиме игры
            if (in_setup) {
                // Проверяем, не получили ли мы асинхронно инструкции по расстановке
                if (!setup_show_menu) {
                    // Если нет асинхронных сообщений, показываем меню расстановки
                    place_ships_interactive();
                    setup_show_menu = true;
                }
                
                std::cout << "\n⚓ Команда: ";
                std::string command;
                std::getline(std::cin, command);
                
                std::string cmd_lower = command;
                std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);
                
                if (cmd_lower == "ready" || cmd_lower == "готово") {
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_SETUP_COMPLETE;
                    
                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (m.type == MSG_PLACE_SHIP) {
                            // Ждем ответ
                            std::string resp;
                            if (wait_for_response(resp, 2000)) {
                                handle_game_response(resp);
                            }
                            
                            // Автоматически показываем поле
                            Message board_msg;
                            std::memset(&board_msg, 0, sizeof(board_msg));
                            std::strncpy(board_msg.from, login.c_str(), LOGIN_MAX - 1);
                            board_msg.type = MSG_GET_BOARD;
                            enqueue_message(board_msg);
                            
                            // Ждем и показываем поле
                            if (wait_for_response(resp, 2000)) {
                                handle_game_response(resp);
                            }
                        }
                    }
                }
                else if (cmd_lower == "board") {
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
                }
                else {
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
            } 
            else {
                // Игра в процессе (стрельба)
                check_for_async_messages();
                show_game_menu();
                
                std::string line;
                std::getline(std::cin, line);
                
                if (line == "1") {
                    std::cout << "\n🎯 Координаты (x,y): ";
                    std::string shot;
                    std::getline(std::cin, shot);
                    
                    Message m;
                    std::memset(&m, 0, sizeof(m));
                    std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                    m.type = MSG_SHOT;
                    std::strncpy(m.payload, shot.c_str(), CMD_MAX - 1);
                    
                    if (!enqueue_message(m)) {
                        std::cout << "\n❌ Очередь переполнена\n";
                    } else {
                        if (wait_for_response(resp, 2000)) {
                            handle_game_response(resp);
                        }
                    }
                } 
                else if (line == "2") {
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
                else if (line == "3") {
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
                } 
                else if (line == "4") {
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
                } 
                else if (line == "5") {
                    std::cout << "\n🏳️ Вы уверены? (да/нет): ";
                    std::string confirm;
                    std::getline(std::cin, confirm);
                    
                    std::string confirm_lower = confirm;
                    std::transform(confirm_lower.begin(), confirm_lower.end(), confirm_lower.begin(), ::tolower);
                    
                    if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" || confirm_lower == "д") {
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
                } 
                else if (line == "6") {
                    std::cout << "\n⚠️ Выход приравнивается к сдаче!\n";
                    std::cout << "Вы уверены? (да/нет): ";
                    std::string confirm;
                    std::getline(std::cin, confirm);
                    
                    std::string confirm_lower = confirm;
                    std::transform(confirm_lower.begin(), confirm_lower.end(), confirm_lower.begin(), ::tolower);
                    
                    if (confirm_lower == "да" || confirm_lower == "y" || confirm_lower == "yes" || confirm_lower == "д") {
                        Message m;
                        std::memset(&m, 0, sizeof(m));
                        std::strncpy(m.from, login.c_str(), LOGIN_MAX - 1);
                        m.type = MSG_SURRENDER;
                        enqueue_message(m);
                        
                        in_game = false;
                        in_setup = false;
                        current_game_id = -1;
                        std::cout << "\n🏳️ Вы вышли из игры\n";
                    }
                } 
                else {
                    std::cout << "\n❌ Неверная команда\n";
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  ИГРА ЗАВЕРШЕНА\n";
    std::cout << "  Спасибо за игру!\n";
    std::cout << std::string(50, '=') << "\n";
}