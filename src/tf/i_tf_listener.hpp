#pragma once

#include <string>
#include <vector>

#include "models/tf_models.hpp"

namespace rosweb::tf {

class ITfListener {
public:
    virtual ~ITfListener() = default;

    virtual void on_tf_update(
        const std::string& subscription_id,
        const std::vector<models::TfTransform>& transforms) = 0;
};

}  // namespace rosweb::tf
