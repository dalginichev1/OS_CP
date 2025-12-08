#pragma once
#include <string>

class Game {
public:
    Game(const std::string& name, const std::string& creator);
    bool join(const std::string& player);
    std::string info() const;

private:
    std::string name;
    std::string p1;
    std::string p2;
};
