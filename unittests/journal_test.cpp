#include "helpers.h"
#include <librft/journal.h>
#include <catch.hpp>

TEST_CASE("journal.memory") {
  auto jrn = rft::logdb::memory_journal::make_new();
  EXPECT_EQ(jrn->size(), size_t(0));

  rft::logdb::log_entry en;

  en.term = 0;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(1));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(0));
  EXPECT_EQ(jrn->first_uncommited_rec().lsn, jrn->prev_rec().lsn);
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);
  EXPECT_EQ(jrn->info(rft::logdb::index_t(0)).lsn, rft::logdb::index_t(0));
  EXPECT_EQ(jrn->info(rft::logdb::index_t(0)).term, en.term);
  EXPECT_EQ(jrn->info(rft::logdb::index_t(0)).kind, en.kind);

  auto first_rec = jrn->prev_rec();

  en.term = 1;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(2));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(1));
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);

  en.term = 2;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(3));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(2));
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::UNDEFINED_INDEX);

  jrn->commit(jrn->prev_rec().lsn);
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->commited_rec().lsn, rft::logdb::index_t(2));
  EXPECT_EQ(jrn->first_uncommited_rec().lsn, rft::logdb::UNDEFINED_INDEX);
  EXPECT_EQ(jrn->info(rft::logdb::index_t(2)).lsn, rft::logdb::index_t(2));
  EXPECT_EQ(jrn->info(rft::logdb::index_t(2)).term, en.term);
  EXPECT_EQ(jrn->info(rft::logdb::index_t(2)).kind, en.kind);

  SECTION("erase all after") {
    jrn->erase_all_after(first_rec);
    EXPECT_EQ(jrn->commited_rec().lsn, first_rec.lsn);
    EXPECT_EQ(jrn->prev_rec().lsn, first_rec.lsn);
  }

  SECTION("erase all to") {
    rft::logdb::reccord_info to_rm;
    to_rm.lsn = 2;
    jrn->erase_all_to(to_rm);
    EXPECT_EQ(jrn->prev_rec().lsn, rft::logdb::index_t(2));
    EXPECT_EQ(jrn->size(), size_t(1));

    EXPECT_EQ(jrn->info(rft::logdb::index_t(0)).lsn, rft::logdb::UNDEFINED_INDEX);
    EXPECT_EQ(jrn->info(rft::logdb::index_t(0)).term, rft::logdb::UNDEFINED_TERM);
  }
}