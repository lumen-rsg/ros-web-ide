#include "server/server.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string workspace = ".";
    if (argc > 1) {
        workspace = argv[1];
    }

    uint16_t port = 8080;
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::cerr << "[INFO] Workspace: " << workspace << " Port: " << port << std::endl;
    rosweb::server::Server server(workspace);
    server.start(port);

    return 0;
}
