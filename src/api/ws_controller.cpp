#include "api/ws_controller.hpp"

namespace rosweb::api {

WsController::WsController(std::unique_ptr<ws::WsRouter> router)
    : router_(std::move(router)) {}

WsController::~WsController() = default;

void WsController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([this](crow::websocket::connection& conn) {
            router_->on_open(conn);
        })
        .onmessage([this](crow::websocket::connection& conn,
                          const std::string& message, bool is_binary) {
            router_->on_message(conn, message, is_binary);
        })
        .onclose([this](crow::websocket::connection& conn,
                        const std::string& reason, uint16_t status_code) {
            router_->on_close(conn, reason, status_code);
        });
}

}  // namespace rosweb::api
