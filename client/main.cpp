#include "Client.hpp"
#include <iostream>

int main() {
    try {
        Client c;
        c.run();
    } catch (const std::exception &ex) {
        std::cerr << "Client error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
