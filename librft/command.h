#pragma once
#include <memory>

namespace rft {

class abstract_command {
public:
};

using command_ptr = std::shared_ptr<abstract_command>;
} // namespace rft