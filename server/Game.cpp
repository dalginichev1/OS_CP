#include "Game.hpp"

Game::Game(const std::string& name_, const std::string& creator)
    : name(name_), p1(creator), p2() {}

bool Game::join(const std::string& player) {
    if (p2.empty()) {
        p2 = player;
        return true;
    }
    return false;
}

std::string Game::info() const {
    if (p2.empty()) return name + ":" + p1 + ":waiting";
    return name + ":" + p1 + ":" + p2;
}
