#include "helpers.h"
#include <solidarity/node.h>
#include <solidarity/special/licenseservice.h>
#include <solidarity/special/lockservice.h>
#include <solidarity/utils/logger.h>

#include "test_description_t.h"

#include <algorithm>
#include <catch.hpp>
#include <condition_variable>
#include <iostream>
#include <iterator>
#include <numeric>

class lock_service_test_description_t : public test_description_t {
public:
  lock_service_test_description_t()
      : test_description_t() {}

  std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
  get_state_machines() override {
    std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
        result;
    result[uint32_t(0)] = std::make_shared<solidarity::special::lockservice>();
    return result;
  }
};

TEST_CASE("lockservice", "[special]") {
  size_t cluster_size = 2;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  lock_service_test_description_t td;
  td.init(cluster_size, true);

  for (auto &kv : td.nodes) {
    auto c = td.clients[kv.first];
    solidarity::special::lockservice_client lc(kv.first, c);
    std::cout << "try " << kv.first << std::endl;
    lc.lock(kv.first);
  }

  for (auto &kv : td.consumers) {
    auto asm_ptr = kv.second[0].get();
    auto lic_asm_ptr = dynamic_cast<solidarity::special::lockservice *>(asm_ptr);
    auto origin_locks = lic_asm_ptr->get_locks();
    auto snap = lic_asm_ptr->snapshot();
    solidarity::special::lockservice ls;
    ls.install_snapshot(snap);
    auto locks = ls.get_locks();
    EXPECT_GE(locks.size(), origin_locks.size());
  }

  auto tr = std::thread([&td]() {
    auto kv = td.clients.begin();
    auto c = kv->second;
    solidarity::special::lockservice_client lc(kv->first, c);
    ++kv;
    lc.lock(kv->first);
  });

  for (auto &kv : td.nodes) {
    auto c = td.clients[kv.first];
    solidarity::special::lockservice_client lc(kv.first, c);
    std::cout << "try unlock" << kv.first << std::endl;
    lc.unlock(kv.first);
  }

  tr.join();

  for (auto &kv : td.nodes) {
    std::cerr << "stop node " << kv.first << std::endl;

    kv.second->stop();
  }
  td.consumers.clear();
  td.clients.clear();
}

class license_service_test_description_t : public test_description_t {
public:
  license_service_test_description_t()
      : test_description_t() {}

  std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
  get_state_machines() override {
    std::unordered_map<uint32_t, std::shared_ptr<solidarity::abstract_state_machine>>
        result;
    result[uint32_t(0)] = std::make_shared<solidarity::special::licenseservice>(lics);
    return result;
  }

  std::unordered_map<std::string, size_t> lics = {{"l1", 2}, {"l2", 2}, {"l3", 2}};
};

TEST_CASE("licenseservice", "[special]") {
  size_t cluster_size = 2;
  auto tst_log_prefix = solidarity::utils::strings::to_string("test?> ");
  auto tst_logger = std::make_shared<solidarity::utils::logging::prefix_logger>(
      solidarity::utils::logging::logger_manager::instance()->get_shared_logger(),
      tst_log_prefix);

  license_service_test_description_t tdd;
  tdd.init(cluster_size, true);

  for (int i = 0; i < 2; ++i) {
    /// lock
    for (auto &kv : tdd.nodes) {
      auto c = tdd.clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : tdd.lics) {
        std::cout << "try " << lkv.first << std::endl;
        while (true) {
          auto r = lc.try_lock(lkv.first);
          if (r) {
            break;
          }
        }
      }
    }

    for (auto &kv : tdd.consumers) {
      auto asm_ptr = kv.second[0].get();
      auto lic_asm_ptr = dynamic_cast<solidarity::special::licenseservice *>(asm_ptr);

      auto origin_locks = lic_asm_ptr->get_locks();
      auto snap = lic_asm_ptr->snapshot();
      solidarity::special::licenseservice ls(tdd.lics);
      ls.install_snapshot(snap);
      auto locks = ls.get_locks();
      EXPECT_EQ(locks.size(), tdd.lics.size());
      for (auto &locks_kv : locks) {
        EXPECT_GE(locks_kv.second.owners.size(),
                  origin_locks[locks_kv.first].owners.size());
      }
    }

    /// read
    for (auto &kv : tdd.nodes) {
      auto c = tdd.clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : tdd.lics) {
        std::cout << "read lockers " << lkv.first << std::endl;
        auto r = lc.lockers(lkv.first);
        std::cout << "readed ";
        std::copy(
            r.begin(), r.end(), std::ostream_iterator<std::string>(std::cout, ", "));
        std::cout << std::endl;

        EXPECT_EQ(r.size(), tdd.nodes.size());
      }
    }

    /// lock2
    for (auto &kv : tdd.nodes) {
      auto c = tdd.clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : tdd.lics) {
        std::cout << "try " << lkv.first << std::endl;
        auto r = lc.try_lock(lkv.first);
        EXPECT_FALSE(r);
      }
    }

    /// unlock
    for (auto &kv : tdd.nodes) {
      auto c = tdd.clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : tdd.lics) {
        std::cout << "unlock " << lkv.first << std::endl;
        lc.unlock(lkv.first);
      }
    }

    /// read
    for (auto &kv : tdd.nodes) {
      auto c = tdd.clients[kv.first];
      solidarity::special::licenseservice_client lc(kv.first, c);
      for (auto lkv : tdd.lics) {
        std::cout << "read lockers " << lkv.first << std::endl;
        while (true) {
          auto r = lc.lockers(lkv.first);
          if (r.empty()) {
            break;
          }
        }
      }
    }
  }
}
