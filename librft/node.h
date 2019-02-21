#pragma once

#include <librft/exports.h>
#include <librft/settings.h>

namespace rft {

class node {
public:
  EXPORT node(const node_settings &ns);

private:
  node_settings _ns;
};

}; // namespace rft