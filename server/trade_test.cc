#include "server/trade.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

PlayerInfo Player(const std::string& account_id) {
  PlayerInfo player;
  player.set_account_id(account_id);
  player.set_name(account_id);
  return player;
}

TradeOffer Offer(int64_t meso, int64_t traces) {
  TradeOffer offer;
  offer.set_meso(meso);
  offer.set_spell_traces(traces);
  return offer;
}

class TradeTest : public ::testing::Test {
 protected:
  TradeTest() : trades_(5) {
  }

  // Opens the trade both sides have asked for, which is the state most of
  // these are about.
  void Open(const std::string& from, const std::string& to) {
    ASSERT_TRUE(trades_.Request(Player(from), Player(to), false).ok);
    ASSERT_TRUE(trades_.Request(Player(to), Player(from), false).ok);
    trades_.TakeChanged();
    trades_.TakeNotices();
  }

  bool Told(const std::string& account_id) {
    std::vector<std::string> changed = trades_.TakeChanged();
    return std::find(changed.begin(), changed.end(), account_id) !=
           changed.end();
  }

  Trades trades_;
};

TEST_F(TradeTest, AsksWithoutOpeningTheirScreen) {
  ASSERT_TRUE(trades_.Request(Player("one"), Player("two"), false).ok);

  // The asker has a trade to draw; the one asked has a notification and
  // nothing else, their screen being theirs to open.
  TradeState mine = trades_.StateFor("one");
  EXPECT_FALSE(mine.id().empty());
  EXPECT_EQ(mine.partner_account_id(), "two");
  EXPECT_EQ(mine.partner_name(), "two");
  EXPECT_FALSE(mine.partner_joined());

  std::vector<std::string> changed = trades_.TakeChanged();
  EXPECT_EQ(changed, std::vector<std::string>{"one"});

  std::vector<TradeNotice> notices = trades_.TakeNotices();
  ASSERT_EQ(notices.size(), 1);
  EXPECT_EQ(notices[0].account_id, "two");
  ASSERT_EQ(notices[0].notification.lines_size(), 2);
  EXPECT_EQ(notices[0].notification.lines(0), "Trade request from");
  EXPECT_EQ(notices[0].notification.lines(1), "one");
}

TEST_F(TradeTest, AskingBackJoins) {
  ASSERT_TRUE(trades_.Request(Player("one"), Player("two"), false).ok);
  trades_.TakeChanged();
  trades_.TakeNotices();
  ASSERT_TRUE(trades_.Request(Player("two"), Player("one"), false).ok);

  // One trade, not two, and both sides now have it.
  EXPECT_EQ(trades_.trade_count(), 1);
  EXPECT_TRUE(trades_.StateFor("one").partner_joined());
  EXPECT_TRUE(trades_.StateFor("two").partner_joined());
  EXPECT_EQ(trades_.StateFor("two").partner_name(), "one");
  EXPECT_TRUE(Told("two"));
  EXPECT_TRUE(trades_.TakeNotices().empty());
}

TEST_F(TradeTest, AskingAgainWhileWaitingIsQuiet) {
  ASSERT_TRUE(trades_.Request(Player("one"), Player("two"), false).ok);
  trades_.TakeNotices();

  ASSERT_TRUE(trades_.Request(Player("one"), Player("two"), false).ok);
  EXPECT_EQ(trades_.trade_count(), 1);
  EXPECT_FALSE(trades_.StateFor("one").partner_joined());
  EXPECT_TRUE(trades_.TakeNotices().empty());
}

TEST_F(TradeTest, RefusesABusyPlayer) {
  TradeResult fighting = trades_.Request(Player("one"), Player("two"), true);
  EXPECT_FALSE(fighting.ok);
  EXPECT_EQ(fighting.reason, Refused::REASON_BUSY);
  EXPECT_EQ(fighting.message, "They're currently busy.");
  EXPECT_EQ(trades_.trade_count(), 0);

  // Being asked already counts: one ask at a time, so an answer is never
  // ambiguous about which trade it means.
  ASSERT_TRUE(trades_.Request(Player("three"), Player("two"), false).ok);
  EXPECT_EQ(trades_.Request(Player("one"), Player("two"), false).reason,
            Refused::REASON_BUSY);

  // And nobody trades with themselves.
  EXPECT_EQ(trades_.Request(Player("one"), Player("one"), false).reason,
            Refused::REASON_BUSY);
  EXPECT_EQ(trades_.trade_count(), 1);
}

TEST_F(TradeTest, RefusesAnAskerAlreadyTrading) {
  Open("one", "two");

  TradeResult result = trades_.Request(Player("one"), Player("three"), false);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, Refused::REASON_BUSY);
  EXPECT_EQ(result.message, "You are already trading.");
  EXPECT_TRUE(trades_.TakeNotices().empty());
}

TEST_F(TradeTest, AnOfferHasASide) {
  Open("one", "two");

  trades_.SetOffer("one", Offer(5000, 30));
  trades_.SetOffer("two", Offer(0, 12));

  TradeState mine = trades_.StateFor("one");
  EXPECT_EQ(mine.mine().meso(), 5000);
  EXPECT_EQ(mine.mine().spell_traces(), 30);
  EXPECT_EQ(mine.theirs().spell_traces(), 12);

  TradeState theirs = trades_.StateFor("two");
  EXPECT_EQ(theirs.mine().spell_traces(), 12);
  EXPECT_EQ(theirs.theirs().meso(), 5000);
  EXPECT_TRUE(Told("two"));
}

TEST_F(TradeTest, LeavingEndsItForBoth) {
  Open("one", "two");
  trades_.SetOffer("one", Offer(5000, 30));
  trades_.TakeChanged();

  trades_.Leave("one");

  EXPECT_EQ(trades_.trade_count(), 0);
  EXPECT_TRUE(trades_.StateFor("one").id().empty());
  EXPECT_TRUE(trades_.StateFor("two").id().empty());
  EXPECT_FALSE(trades_.Busy("one"));
  EXPECT_FALSE(trades_.Busy("two"));
  std::vector<std::string> changed = trades_.TakeChanged();
  EXPECT_EQ(changed.size(), 2);

  // Both are free to trade again, and leaving twice is quiet.
  trades_.Leave("one");
  EXPECT_TRUE(trades_.Request(Player("one"), Player("three"), false).ok);
}

TEST_F(TradeTest, LeavingBeforeTheAnswerFreesThem) {
  ASSERT_TRUE(trades_.Request(Player("one"), Player("two"), false).ok);
  EXPECT_TRUE(trades_.Busy("two"));

  trades_.Leave("one");

  EXPECT_FALSE(trades_.Busy("two"));
  EXPECT_TRUE(trades_.Request(Player("three"), Player("two"), false).ok);
}

}  // namespace
}  // namespace ms
