#pragma once

#include <crow.h>
#include <memory>

#include "ws/ws_router.hpp"

namespace rosweb::api {

class WsController {
public:
    explicit WsController(std::unique_ptr<ws::WsRouter> router);
    ~WsController();

    void register_routes(crow::SimpleApp& app);

private:
    std::unique_ptr<ws::WsRouter> router_;
};

}  // namespace rosweb::api
