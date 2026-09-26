#include "engine.hpp"
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else {
            files.push_back(argv[i]);
        }
    }

    try {
        VimEngine engine(verbose, files);
        engine.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
