#pragma once

#include <solidarity/exports.h>
#include <string>

namespace solidarity {

enum class NODE_KIND { LEADER = 0, FOLLOWER = 1, CANDIDATE = 2, ELECTION = 3 };

EXPORT std::string to_string(const solidarity::NODE_KIND s);
} // namespace solidarity
