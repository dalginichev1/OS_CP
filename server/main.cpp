#include "Server.hpp"
#include <iostream>

int main() {
    try {
        Server s;
        s.run();
    } catch (const std::exception &ex) {
        std::cerr << "Server error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
