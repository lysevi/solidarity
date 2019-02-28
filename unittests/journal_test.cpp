#include "helpers.h"
#include <librft/journal.h>
#include <catch.hpp>

TEST_CASE("journal.memory") {
  auto jrn = rft::logdb::memory_journal::make_new();
  EXPECT_EQ(jrn->size(), size_t(0));

  rft::logdb::log_entry en;

  en.round = 0;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(1));
  EXPECT_EQ(jrn->get(jrn->prev_rec()).round, en.round);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(0));
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);

  en.round = 1;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(2));
  EXPECT_EQ(jrn->get(jrn->prev_rec()).round, en.round);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(1));
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);

  en.round = 2;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(3));
  EXPECT_EQ(jrn->get(jrn->prev_rec()).round, en.round);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(2));
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);

  jrn->commit(jrn->prev_rec());
  EXPECT_EQ(jrn->get(jrn->prev_rec()).round, en.round);
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::index_t(2));
}