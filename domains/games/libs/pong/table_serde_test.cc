#include "domains/games/libs/pong/table_serde.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

namespace pong {
namespace {

using nlohmann::json;

void ExpectSame(const Table& a, const Table& b) {
  EXPECT_EQ(a.players(), b.players());
  EXPECT_EQ(a.variant(), b.variant());
  EXPECT_EQ(a.lives(), b.lives());
  EXPECT_EQ(a.points(), b.points());
  EXPECT_EQ(a.scoreSheet(), b.scoreSheet());
  EXPECT_EQ(a.inPlay(), b.inPlay());
  EXPECT_EQ(a.isOver(), b.isOver());
  EXPECT_EQ(a.endedByClose(), b.endedByClose());
}

TEST(TableSerde, RoundTripsEveryShapeATableTakes) {
  Table table = *Table::open({"alice", "bob"}, Variant::kThreeD);
  table = *table.point(1);
  for (const Table& shape : {table, *table.point(1)->point(1), *table.removePlayer(0),
                             *table.point(1)->point(1)->next(Variant::kSmallcat)}) {
    const auto back = deserializeTable(serializeTable(shape));
    ASSERT_TRUE(back.ok()) << back.status();
    ExpectSame(*back, shape);
  }
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  const json row = json::parse(serializeTable(*Table::open({"alice", "bob"}, Variant::kSmallcat)));
  EXPECT_EQ(row["v"], 1);
  EXPECT_EQ(row["variant"], "smallcat");
  EXPECT_EQ(row["players"], json::array({"alice", "bob"}));
  EXPECT_EQ(row["lives"], json::array({3, 3}));
  EXPECT_EQ(row["points"], 0);
  EXPECT_EQ(row["inPlay"], true);
  EXPECT_EQ(row["closed"], false);
  EXPECT_EQ(row["endedByClose"], false);
  EXPECT_EQ(row["scoreSheet"], json::array());
}

// postgres jsonb refuses a NUL byte, so a name is stored without one.
TEST(TableSerde, StoresNoNulByte) {
  const std::string serialized =
      serializeTable(*Table::open({std::string("al\0ce", 5), "bob"}, Variant::kSmallcat));
  EXPECT_EQ(serialized.find('\0'), std::string::npos);
  EXPECT_EQ(serialized.find("\\u0000"), std::string::npos);
}

TEST(TableSerde, RefusesWhatItCannotRead) {
  const json good = json::parse(serializeTable(*Table::open({"alice", "bob"}, Variant::kSmallcat)));
  EXPECT_FALSE(deserializeTable("not json").ok());
  EXPECT_FALSE(deserializeTable("[]").ok());
  for (const char* field : {"v", "variant", "players", "lives", "points", "inPlay", "closed",
                            "endedByClose", "scoreSheet"}) {
    json missing = good;
    missing.erase(field);
    EXPECT_FALSE(deserializeTable(missing.dump()).ok()) << field;
  }
  json version = good;
  version["v"] = 2;
  EXPECT_FALSE(deserializeTable(version.dump()).ok());
  json variant = good;
  variant["variant"] = "squash";
  EXPECT_FALSE(deserializeTable(variant.dump()).ok());
  json lives = good;
  lives["lives"] = json::array({3});
  EXPECT_FALSE(deserializeTable(lives.dump()).ok());
  json ending = good;
  ending["scoreSheet"] = json::array({{{"winner", "alice"}, {"ending", "checkmate"}}});
  EXPECT_FALSE(deserializeTable(ending.dump()).ok());
  // Readable, but not a table play could reach.
  json impossible = good;
  impossible["lives"] = json::array({0, 3});
  EXPECT_FALSE(deserializeTable(impossible.dump()).ok());
}

}  // namespace
}  // namespace pong
