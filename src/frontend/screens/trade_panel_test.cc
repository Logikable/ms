#include "src/frontend/screens/trade_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/item/item.h"
#include "src/protos/item.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

TradeState Trade(const std::string& partner, bool joined) {
  TradeState trade;
  trade.set_id("t1");
  trade.set_partner_account_id("two");
  trade.set_partner_name(partner);
  trade.set_partner_joined(joined);
  return trade;
}

class TradePanelTest : public PanelTest {
 protected:
  void SetUp() override {
    c_.SetUsername("Dagger");
    c_.AddMeso(1234567);
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_category(ITEM_CATEGORY_ETC);
    c_.AddStackable(trace, 900);
  }

  std::vector<std::string> Rows(const TradePanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, ftxui::center(panel.Render()));
    return ScreenRows(screen);
  }

  std::string Text(const TradePanel& panel) {
    std::string whole;
    for (const std::string& row : Rows(panel)) {
      whole += row;
      whole += "\n";
    }
    return whole;
  }

  TradePanel panel_{c_, account_};
};

TEST_F(TradePanelTest, DrawsBothOffersAndTheBag) {
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_mine()->set_meso(5000);
  trade.mutable_mine()->set_spell_traces(30);
  trade.mutable_theirs()->set_meso(120);
  panel_.SetTrade(trade);

  std::string screen = Text(panel_);
  EXPECT_NE(screen.find("Dagger"), std::string::npos);
  EXPECT_NE(screen.find("Wand"), std::string::npos);
  EXPECT_NE(screen.find("Inventory"), std::string::npos);
  // Both offers, drawn with the bag's own marks.
  EXPECT_NE(screen.find("5,000"), std::string::npos);
  EXPECT_NE(screen.find("30"), std::string::npos);
  EXPECT_NE(screen.find("120"), std::string::npos);

  // The two currencies share one line on each side, the meso first.
  std::vector<std::string> rows = Rows(panel_);
  int line = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find("5,000") != std::string::npos) {
      line = i;
    }
  }
  ASSERT_GE(line, 0);
  EXPECT_LT(rows[line].find("5,000"), rows[line].find("30"));
  EXPECT_NE(rows[line].find("120"), std::string::npos)
      << "both sides draw their currencies on the same row";
}

TEST_F(TradePanelTest, TheirWindowHasNoNameUntilTheyJoin) {
  panel_.SetTrade(Trade("Wand", /*joined=*/false));
  EXPECT_EQ(Text(panel_).find("Wand"), std::string::npos);

  panel_.SetTrade(Trade("Wand", /*joined=*/true));
  EXPECT_NE(Text(panel_).find("Wand"), std::string::npos);
}

TEST_F(TradePanelTest, TheCursorWalksYourOwnTwoCurrencies) {
  TradeState trade = Trade("Wand", /*joined=*/true);
  trade.mutable_mine()->set_meso(5000);
  trade.mutable_mine()->set_spell_traces(30);
  panel_.SetTrade(trade);
  panel_.Reset();

  EXPECT_EQ(panel_.selected(), TradeCurrency::kMeso);
  EXPECT_EQ(panel_.held(), 1234567);
  EXPECT_EQ(panel_.offered(), 5000);

  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected(), TradeCurrency::kSpellTraces);
  EXPECT_EQ(panel_.held(), 900);
  EXPECT_EQ(panel_.offered(), 30);

  // A ring of two: one more step comes back round.
  panel_.MoveCursor(1);
  EXPECT_EQ(panel_.selected(), TradeCurrency::kMeso);
  panel_.MoveCursor(-1);
  EXPECT_EQ(panel_.selected(), TradeCurrency::kSpellTraces);
}

}  // namespace
}  // namespace ms
