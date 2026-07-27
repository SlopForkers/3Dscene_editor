#include "app.h"
#include <vector>
#include <string>

int main(int argc, char** argv) {
    App app;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return app.run(args);
}
