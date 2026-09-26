#include "engine.hpp"
#include <iostream>
#include <cstring>

static void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options] [files...]\n\n"
              << "Arguments:\n"
              << "  file1 file2 ...          Open one or more files in buffers\n"
              << "  +<line>[:<col>] file     Open file at line (and optional column)\n"
              << "  file:<line>[:<col>]      Open file at line (and optional column)\n\n"
              << "Options:\n"
              << "  -v, --verbose            Enable verbose debug logging (fe_debug.log)\n"
              << "  -h, --help               Display this help message and exit\n\n";
}

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
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
