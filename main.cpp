#include "engine.hpp"
#include <iostream>

int main() {
    try {
        VimEngine engine;
        engine.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
