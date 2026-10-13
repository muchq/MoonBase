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
  const Table opened = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
  const Table mid_trick = Steps(opened, 4 + 6);
  const Table scored = Steps(opened, 4 + 52);
  const Table near = *Table::restore(Mode::kPartners, kSeatIds, opened.hand(), 2, {460, 10}, {2, 3},
                                     {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, {460, 10}, {460, 10}}},
                                     {{1, {80, 510}}}, std::nullopt);
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
  const Table mid_trick = Steps(*Table::open(Mode::kPartners, kSeatIds, FullDeck()), 4 + 6);
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
  EXPECT_EQ(nlohmann::json::parse(
                serializeTable(*Table::open(Mode::kPartners, kSeatIds, FullDeck())))["bids"],
            nlohmann::json({nullptr, nullptr, nullptr, nullptr}));
}

// Postgres jsonb refuses a NUL byte.
TEST(TableSerde, StoresNoNulByte) {
  const std::string nul_name("a\0b", 3);
  const Table table = *Table::open(Mode::kPartners, {nul_name, "bob", "cat", "dan"}, FullDeck());
  EXPECT_EQ(serializeTable(table).find('\0'), std::string::npos);
}

TEST(TableSerde, RefusesWhatItCannotRead) {
  const std::string good = serializeTable(*Table::open(Mode::kPartners, kSeatIds, FullDeck()));
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
  nlohmann::json short_hands = nlohmann::json::parse(good)["hands"];
  short_hands.erase(short_hands.size() - 1);
  EXPECT_FALSE(with("hands", short_hands).ok());
  // A card past an int's range is refused, not narrowed to the card it
  // wraps to.
  nlohmann::json wrapped = nlohmann::json::parse(good);
  wrapped["hands"][1][0] = 4294967296ull + wrapped["hands"][1][0].get<uint64_t>();
  EXPECT_FALSE(deserializeTable(wrapped.dump()).ok());
  nlohmann::json bad_card = nlohmann::json::parse(good);
  bad_card["hands"][0][0] = 52;
  EXPECT_FALSE(deserializeTable(bad_card.dump()).ok());
  // A count past an int's range is refused, not narrowed to a bid of nil.
  nlohmann::json big_bid = nlohmann::json::parse(good);
  big_bid["bids"][1] = 4294967296LL;
  EXPECT_FALSE(deserializeTable(big_bid.dump()).ok());
  EXPECT_FALSE(with("dealer", 4294967296ULL).ok());
  // Narrowed, this would read as hand 1.
  EXPECT_FALSE(with("handNumber", -4294967295LL).ok());
  EXPECT_FALSE(with("games", nlohmann::json::array({{{"winner", "x"}, {"scores", {0, 0}}}})).ok());
  EXPECT_FALSE(with("last", 3).ok());
  EXPECT_FALSE(with("current", 3).ok());
  EXPECT_FALSE(with("seats", nlohmann::json({"ann", "bob", "cat", 4})).ok());
  nlohmann::json missing = nlohmann::json::parse(good);
  missing.erase("lines");
  EXPECT_FALSE(deserializeTable(missing.dump()).ok());
}

TEST(TableSerde, RoundTripsCutthroat) {
  const Table three =
      Steps(*Table::open(Mode::kCutthroat, {"ann", "bob", "cat"}, FullDeck()), 3 + 4);
  const auto read = deserializeTable(serializeTable(three));
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(*read, three);
  EXPECT_EQ(nlohmann::json::parse(serializeTable(three))["mode"], "cutthroat");
}

// A row with no mode is partners.
TEST(TableSerde, ARowWithNoModeIsPartners) {
  const Table partners = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
  nlohmann::json row = nlohmann::json::parse(serializeTable(partners));
  EXPECT_EQ(row["mode"], "partners");
  row.erase("mode");
  const auto read = deserializeTable(row.dump());
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(*read, partners);
  row["mode"] = "solo";
  EXPECT_FALSE(deserializeTable(row.dump()).ok());
  row["mode"] = 1;
  EXPECT_FALSE(deserializeTable(row.dump()).ok());
}

}  // namespace
}  // namespace spades
