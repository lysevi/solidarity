#include "helpers.h"
#include <librft/journal.h>
#include <catch.hpp>

TEST_CASE("journal.memory") {
  auto jrn = rft::logdb::memory_journal::make_new();
  EXPECT_EQ(jrn->size(), size_t(0));

  rft::logdb::log_entry en;

  en.round = 0;
  jrn->put(rft::logdb::index_t(0), en);
  EXPECT_EQ(jrn->size(), size_t(1));

  en.round = 1;
  jrn->put(rft::logdb::index_t(1), en);
  EXPECT_EQ(jrn->size(), size_t(2));

  en.round = 2;
  jrn->put(rft::logdb::index_t(2), en);
  EXPECT_EQ(jrn->size(), size_t(3));
}