#pragma once

#include <crow.h>
#include <memory>
#include <string>

#include "fs/i_filesystem.hpp"
#include "fs/path_validator.hpp"

namespace rosweb::api {

class FsController {
public:
    FsController(std::shared_ptr<fs::IFileSystem> filesystem,
                 std::shared_ptr<fs::PathValidator> validator);

    void register_routes(crow::SimpleApp& app);

private:
    std::shared_ptr<fs::IFileSystem> fs_;
    std::shared_ptr<fs::PathValidator> validator_;

    auto handle_get_tree(const crow::request& req) -> std::string;
    auto handle_get_file(const crow::request& req) -> std::string;
    auto handle_put_file(const crow::request& req) -> std::string;
    auto handle_delete_file(const crow::request& req) -> std::string;
    auto handle_rename(const crow::request& req) -> std::string;
    auto handle_mkdir(const crow::request& req) -> std::string;
    auto handle_search(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;
};

}  // namespace rosweb::api
