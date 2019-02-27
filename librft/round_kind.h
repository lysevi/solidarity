#pragma once

#include <librft/exports.h>
#include <string>

namespace rft {

enum class ROUND_KIND { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2, ELECTION = 3 };

EXPORT std::string to_string(const rft::ROUND_KIND s);
} // namespace rft