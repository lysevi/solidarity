#include <solidarity/utils/crc.h>
#include <solidarity/utils/property.h>
#include <solidarity/utils/strings.h>
#include <solidarity/utils/utils.h>

#include "helpers.h"
#include <array>
#include <catch.hpp>
#include <numeric>

TEST_CASE("utils.crc") {
  std::vector<uint8_t> data({1, 2, 3, 4, 5});
  solidarity::utils::crc32 c;
  c.calculate(data.cbegin(), data.cend());
  auto crc1 = c.checksum();
  data[0]++;

  solidarity::utils::crc32 c2;
  c2.calculate(data.cbegin(), data.cend());
  auto crc2 = c2.checksum();
  EXPECT_NE(crc1, crc2);
}

TEST_CASE("utils.split") {
  std::array<int, 8> tst_a;
  std::iota(tst_a.begin(), tst_a.end(), 1);

  std::string str = "1 2 3 4 5 6 7 8";
  auto splitted_s = solidarity::utils::strings::tokens(str);

  std::vector<int> splitted(splitted_s.size());
  std::transform(splitted_s.begin(),
                 splitted_s.end(),
                 splitted.begin(),
                 [](const std::string &s) { return std::stoi(s); });

  EXPECT_EQ(splitted.size(), size_t(8));

  bool is_equal
      = std::equal(tst_a.begin(), tst_a.end(), splitted.begin(), splitted.end());
  EXPECT_TRUE(is_equal);
}

TEST_CASE("utils.to_upper") {
  auto s = "lower string";
  auto res = solidarity::utils::strings::to_upper(s);
  EXPECT_EQ(res, "LOWER STRING");
}

TEST_CASE("utils.to_lower") {
  auto s = "UPPER STRING";
  auto res = solidarity::utils::strings::to_lower(s);
  EXPECT_EQ(res, "upper string");
}

TEST_CASE("utils.property") {
  class test_struct {
    PROPERTY(int, ivalue)
    PROPERTY(double, dvalue)
    PROPERTY(std::string, svalue)
  };

  test_struct p;
  p.set_dvalue(3.14).set_ivalue(3).set_svalue("string");
  EXPECT_TRUE(std::fabs(p.dvalue() - 3.14) < 0.001);
  EXPECT_EQ(p.ivalue(), 3);
  EXPECT_EQ(p.svalue(), "string");
}

void f_throw() {
  throw solidarity::utils::exceptions::exception_t("error");
}

TEST_CASE("utils.exception") {
  try {
    f_throw();
  } catch (solidarity::utils::exceptions::exception_t &e) {
    solidarity::utils::logging::logger_info(e.what());
  }
}
