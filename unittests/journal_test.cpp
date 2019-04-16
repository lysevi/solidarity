#include "helpers.h"
#include <catch.hpp>
#include <libsolidarity/journal.h>

TEST_CASE("journal.memory") {
  auto jrn = solidarity::logdb::memory_journal::make_new();
  EXPECT_EQ(jrn->size(), size_t(0));
  EXPECT_EQ(jrn->restore_start_point().lsn, solidarity::logdb::UNDEFINED_INDEX);

  solidarity::logdb::log_entry en;

  en.term = 0;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(1));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(0));
  EXPECT_EQ(jrn->first_uncommited_rec().lsn, jrn->prev_rec().lsn);
  EXPECT_EQ(jrn->commited_rec().lsn, solidarity::logdb::UNDEFINED_INDEX);
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(0)).lsn, solidarity::logdb::index_t(0));
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(0)).term, en.term);
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(0)).kind, en.kind);

  auto first_rec = jrn->prev_rec();

  en.term = 1;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(2));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(1));
  EXPECT_EQ(jrn->commited_rec().lsn, solidarity::logdb::UNDEFINED_INDEX);

  en.term = 2;
  jrn->put(en);
  EXPECT_EQ(jrn->size(), size_t(3));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(2));
  EXPECT_EQ(jrn->commited_rec().lsn, solidarity::logdb::UNDEFINED_INDEX);

  jrn->commit(jrn->prev_rec().lsn);
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->commited_rec().lsn, solidarity::logdb::index_t(2));
  EXPECT_EQ(jrn->first_uncommited_rec().lsn, solidarity::logdb::UNDEFINED_INDEX);
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(2)).lsn, solidarity::logdb::index_t(2));
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(2)).term, en.term);
  EXPECT_EQ(jrn->info(solidarity::logdb::index_t(2)).kind, en.kind);

  EXPECT_EQ(jrn->restore_start_point().lsn, solidarity::logdb::index_t(0));
  en.kind = solidarity::logdb::LOG_ENTRY_KIND::SNAPSHOT;
  auto snap_point = jrn->put(en);
  EXPECT_EQ(jrn->restore_start_point().lsn, snap_point.lsn);

  SECTION("erase all after") {
    jrn->erase_all_after(first_rec.lsn);
    EXPECT_EQ(jrn->commited_rec().lsn, first_rec.lsn);
    EXPECT_EQ(jrn->prev_rec().lsn, first_rec.lsn);
  }

  SECTION("erase all to") {
    solidarity::logdb::reccord_info to_rm;
    to_rm.lsn = 2;
    jrn->erase_all_to(to_rm.lsn);
    EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(3));
    EXPECT_EQ(jrn->reccords_count(), size_t(2));

    EXPECT_EQ(jrn->info(solidarity::logdb::index_t(0)).lsn,
              solidarity::logdb::UNDEFINED_INDEX);
    EXPECT_EQ(jrn->info(solidarity::logdb::index_t(0)).term,
              solidarity::logdb::UNDEFINED_TERM);
  }
}

TEST_CASE("journal.memory.put(idx)") {
  auto jrn = solidarity::logdb::memory_journal::make_new();
  EXPECT_EQ(jrn->size(), size_t(0));
  EXPECT_EQ(jrn->restore_start_point().lsn, solidarity::logdb::UNDEFINED_INDEX);

  solidarity::logdb::log_entry en;

  en.term = 0;
  jrn->put(solidarity::logdb::index_t(777), en);
  EXPECT_EQ(jrn->reccords_count(), size_t(1));
  EXPECT_EQ(jrn->size(), size_t(777 + 1));
  EXPECT_EQ(jrn->get(jrn->prev_rec().lsn).term, en.term);
  EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(777));
  jrn->put(en);
  EXPECT_EQ(jrn->prev_rec().lsn, solidarity::logdb::index_t(778));
}