#include "api/fs_controller.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "api/rest_helpers.hpp"
#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"
#include "models/fs_models.hpp"

namespace rosweb::api {

FsController::FsController(std::shared_ptr<fs::IFileSystem> filesystem,
                           std::shared_ptr<fs::PathValidator> validator)
    : fs_(std::move(filesystem)), validator_(std::move(validator)) {}

void FsController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/fs/tree")
    ([this](const crow::request& req) {
        return try_handle("GET /fs/tree", [&] { return handle_get_tree(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/file").methods("GET"_method)
    ([this](const crow::request& req) {
        return try_handle("GET /fs/file", [&] { return handle_get_file(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/file").methods("PUT"_method)
    ([this](const crow::request& req) {
        return try_handle("PUT /fs/file", [&] { return handle_put_file(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/file").methods("DELETE"_method)
    ([this](const crow::request& req) {
        return try_handle("DELETE /fs/file", [&] { return handle_delete_file(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/rename").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /fs/rename", [&] { return handle_rename(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/mkdir").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /fs/mkdir", [&] { return handle_mkdir(req); });
    });

    CROW_ROUTE(app, "/api/v1/fs/search")
    ([this](const crow::request& req) {
        return try_handle("GET /fs/search", [&] { return handle_search(req); });
    });
}

auto FsController::try_handle(std::string_view endpoint_name,
                              std::function<std::string()> handler) -> std::string {
    try {
        return handler();
    } catch (const errors::ApiException& e) {
        auto [status, body] = make_error(e.code(), e.what());
        return body;
    } catch (const std::exception& e) {
        auto [status, body] = make_error(errors::ErrorCode::INTERNAL_ERROR, e.what());
        return body;
    }
}

auto FsController::handle_get_tree(const crow::request& req) -> std::string {
    std::string path = req.url_params.get("path") ? req.url_params.get("path") : "";
    std::string depth_str = req.url_params.get("depth") ? req.url_params.get("depth") : "1";
    int depth = std::stoi(depth_str);

    auto result = unwrap(fs_->get_tree(path, depth), path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_get_file(const crow::request& req) -> std::string {
    const char* path = req.url_params.get("path");
    if (!path || std::strlen(path) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::FS_PATH_NOT_FOUND,
                                         "Missing required parameter: path");
        return body;
    }

    auto result = unwrap(fs_->read_file(path), path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_put_file(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);

    std::string path = body.at("path").get<std::string>();
    std::string content = body.at("content").get<std::string>();
    bool create_parents = body.value("createParents", false);

    auto result = unwrap(fs_->write_file(path, content, create_parents), path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_delete_file(const crow::request& req) -> std::string {
    const char* path = req.url_params.get("path");
    if (!path || std::strlen(path) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::FS_PATH_NOT_FOUND,
                                         "Missing required parameter: path");
        return body;
    }

    bool recursive = false;
    const char* rec_str = req.url_params.get("recursive");
    if (rec_str) {
        std::string val(rec_str);
        recursive = (val == "true" || val == "1");
    }

    auto result = unwrap(fs_->delete_path(path, recursive), path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_rename(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);

    std::string old_path = body.at("oldPath").get<std::string>();
    std::string new_path = body.at("newPath").get<std::string>();

    auto result = unwrap(fs_->rename(old_path, new_path), old_path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_mkdir(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);

    std::string path = body.at("path").get<std::string>();
    bool create_parents = body.value("createParents", false);

    auto result = unwrap(fs_->mkdir(path, create_parents), path);
    nlohmann::json j = result;
    return make_success(j);
}

auto FsController::handle_search(const crow::request& req) -> std::string {
    const char* query = req.url_params.get("query");
    if (!query || std::strlen(query) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::FS_PATH_NOT_FOUND,
                                         "Missing required parameter: query");
        return body;
    }

    models::SearchQuery sq;
    sq.query = query;
    sq.path = req.url_params.get("path") ? req.url_params.get("path") : "";

    const char* type_str = req.url_params.get("type");
    if (type_str) {
        std::string t(type_str);
        if (t == "filename") sq.type = models::SearchType::filename;
        else if (t == "content") sq.type = models::SearchType::content;
        else sq.type = models::SearchType::all;
    }

    const char* max_str = req.url_params.get("maxResults");
    if (max_str) {
        sq.max_results = std::stoi(max_str);
    }

    auto result = unwrap(fs_->search(sq), sq.path);
    nlohmann::json j = result;
    return make_success(j);
}

}  // namespace rosweb::api
