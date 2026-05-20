#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "errors/error_codes.hpp"
#include "models/tf_models.hpp"
#include "tf/i_tf_listener.hpp"

namespace rosweb::tf {

class ITfManager {
public:
    virtual ~ITfManager() = default;

    virtual auto subscribe_tf(
        const std::string& subscription_id,
        const std::optional<std::vector<std::string>>& frames,
        const std::optional<int>& throttle_rate)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto unsubscribe_tf(const std::string& subscription_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto get_tf_tree()
        -> std::expected<models::TfTreePayload, errors::ErrorCode> = 0;

    virtual auto add_listener(std::shared_ptr<ITfListener> listener) -> void = 0;
    virtual auto remove_listener(std::shared_ptr<ITfListener> listener) -> void = 0;

    virtual auto shutdown() -> void = 0;
};

}  // namespace rosweb::tf
