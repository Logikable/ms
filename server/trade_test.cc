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

  // Opens a trade that both sides have requested.
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

  // The requester has a trade state. The invited player only gets a
  // notification until they request back.
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

  // There is one trade, and both sides are in it.
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

  // A player with an unanswered request is busy too.
  ASSERT_TRUE(trades_.Request(Player("three"), Player("two"), false).ok);
  EXPECT_EQ(trades_.Request(Player("one"), Player("two"), false).reason,
            Refused::REASON_BUSY);

  // Nobody can trade with themselves.
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

TEST_F(TradeTest, ItemsCrossWhole) {
  Open("one", "two");

  TradeOffer offer = Offer(0, 0);
  Equip* equip = offer.add_equips();
  equip->set_equip_name("Fafnir Mace");
  equip->set_stars(17);
  TradeStack* stack = offer.add_stacks();
  stack->set_name("Chaos Scroll");
  stack->set_count(3);
  trades_.SetOffer("one", offer);

  TradeState theirs = trades_.StateFor("two");
  ASSERT_EQ(theirs.theirs().equips_size(), 1);
  EXPECT_EQ(theirs.theirs().equips(0).equip_name(), "Fafnir Mace");
  EXPECT_EQ(theirs.theirs().equips(0).stars(), 17);
  ASSERT_EQ(theirs.theirs().stacks_size(), 1);
  EXPECT_EQ(theirs.theirs().stacks(0).name(), "Chaos Scroll");
  EXPECT_EQ(theirs.theirs().stacks(0).count(), 3);
}

TEST_F(TradeTest, AnOfferClearsBothAcceptances) {
  Open("one", "two");
  trades_.SetAccept("one", true);
  trades_.SetAccept("two", true);
  ASSERT_TRUE(trades_.StateFor("one").mine_accepted());
  ASSERT_TRUE(trades_.StateFor("one").theirs_accepted());

  trades_.SetOffer("two", Offer(10, 0));

  EXPECT_FALSE(trades_.StateFor("one").mine_accepted());
  EXPECT_FALSE(trades_.StateFor("one").theirs_accepted());
  EXPECT_TRUE(Told("one"));

  // Acceptance can be withdrawn.
  trades_.SetAccept("one", true);
  trades_.SetAccept("one", false);
  EXPECT_FALSE(trades_.StateFor("two").theirs_accepted());
}

TEST_F(TradeTest, BothConfirmsPayBothSides) {
  Open("one", "two");
  trades_.SetOffer("one", Offer(5000, 30));
  trades_.SetOffer("two", Offer(0, 12));
  trades_.SetAccept("one", true);
  trades_.SetAccept("two", true);

  trades_.SetConfirm("one", true);
  EXPECT_TRUE(trades_.StateFor("one").mine_confirmed());
  EXPECT_FALSE(trades_.StateFor("two").mine_confirmed());
  EXPECT_TRUE(trades_.TakeCompletions().empty());

  trades_.SetConfirm("two", true);

  std::vector<TradeCompletion> paid = trades_.TakeCompletions();
  ASSERT_EQ(paid.size(), 2);
  EXPECT_EQ(paid[0].account_id, "one");
  EXPECT_EQ(paid[0].received.spell_traces(), 12);
  EXPECT_EQ(paid[1].account_id, "two");
  EXPECT_EQ(paid[1].received.meso(), 5000);

  // The trade is gone and nobody gets a change update, since an empty state
  // would look like a walk-out. This includes the update the first confirm
  // queued, which is still pending here.
  EXPECT_EQ(trades_.trade_count(), 0);
  EXPECT_FALSE(trades_.Busy("one"));
  EXPECT_TRUE(trades_.TakeChanged().empty());
}

TEST_F(TradeTest, CancellingKeepsTheOtherAcceptance) {
  Open("one", "two");
  trades_.SetAccept("one", true);
  trades_.SetAccept("two", true);
  trades_.SetConfirm("two", true);

  trades_.SetConfirm("one", false);

  EXPECT_FALSE(trades_.StateFor("one").mine_accepted());
  EXPECT_TRUE(trades_.StateFor("two").mine_accepted());
  EXPECT_FALSE(trades_.StateFor("two").mine_confirmed());
  EXPECT_TRUE(trades_.TakeCompletions().empty());
  EXPECT_EQ(trades_.trade_count(), 1);
}

TEST_F(TradeTest, ConfirmingWithoutBothAcceptancesDoesNothing) {
  Open("one", "two");
  trades_.SetAccept("one", true);

  trades_.SetConfirm("one", true);
  trades_.SetConfirm("two", true);

  EXPECT_FALSE(trades_.StateFor("one").mine_confirmed());
  EXPECT_EQ(trades_.trade_count(), 1);
  EXPECT_TRUE(trades_.TakeCompletions().empty());
}

TEST_F(TradeTest, AgreementOutsideATradeIsQuiet) {
  trades_.SetAccept("nobody", true);
  trades_.SetConfirm("nobody", true);
  EXPECT_TRUE(trades_.TakeChanged().empty());
  EXPECT_TRUE(trades_.TakeCompletions().empty());
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

  // Both can trade again, and leaving twice does nothing.
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
