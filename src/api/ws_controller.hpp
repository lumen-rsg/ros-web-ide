#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <memory>

#include "ws/ws_router.hpp"

namespace rosweb::api {

class WsController {
public:
    explicit WsController(std::unique_ptr<ws::WsRouter> router);
    ~WsController();

    void register_routes(crow::App<crow::CORSHandler>& app);

private:
    std::unique_ptr<ws::WsRouter> router_;
};

}  // namespace rosweb::api
