#include "domains/games/libs/cards/spades/table_serde.h"

#include <gtest/gtest.h>

#include <deque>
#include <nlohmann/json.hpp>

namespace spades {
namespace {

const std::vector<std::string> kSeatIds = {"ann", "bob", "cat", "dan"};

std::deque<Card> FullDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

Table Step(const Table& table) {
  const int seat = table.hand().turn();
  if (table.phase() == Phase::kBidding) return *table.bid(seat, seat == 2 ? kNil : 3);
  return *table.play(seat, table.hand().legalPlays(seat).front());
}

Table Steps(Table table, int count) {
  for (int i = 0; i < count; ++i) table = Step(table);
  return table;
}

TEST(TableSerde, RoundTripsEveryShapeATableTakes) {
  const Table opened = *Table::open(kSeatIds, FullDeck());
  const Table mid_trick = Steps(opened, 4 + 6);
  const Table scored = Steps(opened, 4 + 52);
  const Table near =
      *Table::restore(kSeatIds, opened.hand(), 3, {460, 10}, {2, 3}, {}, {1, 0}, std::nullopt);
  const Table won = Steps(near, 4 + 52);
  ASSERT_EQ(won.phase(), Phase::kWon);
  const Table closed = *mid_trick.removePlayer(3);
  for (const Table& table : {opened, mid_trick, scored, *scored.deal(FullDeck()), won, closed}) {
    const auto back = deserializeTable(serializeTable(table));
    ASSERT_TRUE(back.ok()) << back.status() << "\n" << serializeTable(table);
    EXPECT_EQ(*back, table) << serializeTable(table);
  }
}

TEST(TableSerde, WritesTheDocumentedSchema) {
  const Table mid_trick = Steps(*Table::open(kSeatIds, FullDeck()), 4 + 6);
  const nlohmann::json row = nlohmann::json::parse(serializeTable(mid_trick));
  EXPECT_EQ(row["v"], 1);
  EXPECT_EQ(row["seats"], nlohmann::json(kSeatIds));
  EXPECT_EQ(row["dealer"], 0);
  EXPECT_EQ(row["bids"], nlohmann::json({3, 3, 0, 3}));
  EXPECT_EQ(row["tricks"], nlohmann::json({1, 0, 0, 0}));
  EXPECT_EQ(row["current"].size(), 2u);
  EXPECT_EQ(row["current"][0]["seat"], 0);
  EXPECT_EQ(row["last"]["winner"], 0);
  EXPECT_EQ(row["spadesBroken"], true);
  EXPECT_EQ(row["hands"][0].size(), 11u);
  EXPECT_EQ(row["handNumber"], 1);
  EXPECT_EQ(row["scores"], nlohmann::json({0, 0}));
  EXPECT_EQ(row["left"], nullptr);
  EXPECT_EQ(nlohmann::json::parse(serializeTable(*Table::open(kSeatIds, FullDeck())))["bids"],
            nlohmann::json({nullptr, nullptr, nullptr, nullptr}));
}

// Postgres jsonb refuses a NUL byte.
TEST(TableSerde, StoresNoNulByte) {
  const std::string nul_name("a\0b", 3);
  const Table table = *Table::open({nul_name, "bob", "cat", "dan"}, FullDeck());
  EXPECT_EQ(serializeTable(table).find('\0'), std::string::npos);
}

TEST(TableSerde, RefusesWhatItCannotRead) {
  const std::string good = serializeTable(*Table::open(kSeatIds, FullDeck()));
  EXPECT_TRUE(deserializeTable(good).ok());
  EXPECT_FALSE(deserializeTable("not json").ok());
  EXPECT_FALSE(deserializeTable("[]").ok());
  const auto with = [&good](const char* key, nlohmann::json value) {
    nlohmann::json row = nlohmann::json::parse(good);
    row[key] = std::move(value);
    return deserializeTable(row.dump());
  };
  EXPECT_FALSE(with("v", 2).ok());
  EXPECT_FALSE(with("seats", nlohmann::json({"ann", "bob"})).ok());
  EXPECT_FALSE(with("dealer", "0").ok());
  EXPECT_FALSE(with("bids", nlohmann::json({1, 2})).ok());
  EXPECT_FALSE(with("tricks", nlohmann::json({0, 0, 0, "x"})).ok());
  EXPECT_FALSE(with("spadesBroken", 1).ok());
  EXPECT_FALSE(with("scores", nlohmann::json::array({0})).ok());
  EXPECT_FALSE(with("left", 9).ok());
  nlohmann::json bad_card = nlohmann::json::parse(good);
  bad_card["hands"][0][0] = 52;
  EXPECT_FALSE(deserializeTable(bad_card.dump()).ok());
  nlohmann::json missing = nlohmann::json::parse(good);
  missing.erase("scoreSheet");
  EXPECT_FALSE(deserializeTable(missing.dump()).ok());
}

}  // namespace
}  // namespace spades
