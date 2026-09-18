#include "src/frontend/screens/trade_panel.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/inventory_list.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/item/item.h"

namespace ms {
namespace {

// Each offer window, borders included, and the bag under the pair of them.
constexpr int kOfferWidth = 54;
constexpr int kBagWidth = 2 * (kOfferWidth + 2) - 2;

// The rows an offer keeps for the items that will go in it, and the rows the
// bag shows at once. Both fixed: a window that grew with what was put in it
// would move the other one's border.
constexpr int kOfferRows = 8;
constexpr int kBagRows = 11;

// The currency cells. Wide enough for the most either can hold -- a hundred
// billion meso and a million traces -- so a number climbing never moves the
// one beside it.
constexpr int kMesoCell = 20;
constexpr int kTraceCell = 14;

}  // namespace

TradePanel::TradePanel(const CharacterInstance& character,
                       const AccountInstance& account)
    : character_(character), account_(account) {
}

void TradePanel::SetTrade(const TradeState& trade) {
  trade_ = trade;
}

void TradePanel::Reset() {
  cursor_ = 0;
}

void TradePanel::MoveCursor(int delta) {
  constexpr int kStops = 2;
  cursor_ = ((cursor_ + delta) % kStops + kStops) % kStops;
}

TradeCurrency TradePanel::selected() const {
  if (cursor_ == 0) {
    return TradeCurrency::kMeso;
  }
  return TradeCurrency::kSpellTraces;
}

int64_t TradePanel::held() const {
  if (selected() == TradeCurrency::kMeso) {
    return character_.meso();
  }
  return character_.CountStackable(kSpellTraceName);
}

int64_t TradePanel::offered() const {
  if (selected() == TradeCurrency::kMeso) {
    return trade_.mine().meso();
  }
  return trade_.mine().spell_traces();
}

ftxui::Element TradePanel::RenderCurrencies(const TradeOffer& offer,
                                            bool mine) const {
  ftxui::Element meso =
      ftxui::text(PadRight(FormatMeso(offer.meso()), kMesoCell)) |
      ftxui::color(kTheme);
  ftxui::Element traces =
      ftxui::text(
          PadRight(FormatSpellTraces(offer.spell_traces()), kTraceCell)) |
      ftxui::color(kTheme);
  return ftxui::hbox({
      ftxui::text(" "),
      HighlightRow(std::move(meso), mine && selected() == TradeCurrency::kMeso),
      ftxui::text("  "),
      HighlightRow(std::move(traces),
                   mine && selected() == TradeCurrency::kSpellTraces),
      ftxui::filler(),
  });
}

ftxui::Element TradePanel::RenderOffer(const TradeOffer& offer,
                                       const std::string& title,
                                       bool mine) const {
  ftxui::Element body =
      ftxui::vbox({
          RenderCurrencies(offer, mine),
          ThemedSeparator(),
          ftxui::filler() |
              ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kOfferRows),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kOfferWidth);
  return ThemedWindow(title, std::move(body));
}

ftxui::Element TradePanel::RenderBag() const {
  ItemListOptions options;
  options.bag = true;
  options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
  options.star_force = Unlocked(Feature::kStarForce, character_, account_);
  options.potential = Unlocked(Feature::kPotential, character_, account_);
  ItemColumns columns = FitItemColumns(kBagWidth, options);
  // No cursor anywhere in it: what is tradeable is not settled, so the bag is
  // drawn to be read rather than taken from.
  std::vector<InventoryRowState> rows =
      BuildEquipRows(character_, /*selected=*/-1,
                     std::chrono::steady_clock::duration::zero(), columns);
  std::vector<ftxui::Element> list;
  list.reserve(rows.size());
  for (const InventoryRowState& row : rows) {
    list.push_back(RenderEquipRow(row, /*on_cursor=*/false));
  }
  if (list.empty()) {
    list.push_back(EmptyState("empty"));
  }
  ftxui::Element body =
      ftxui::vbox({
          EquipHeader(columns),
          ThemedSeparator(),
          ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator |
              ftxui::yframe |
              ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBagRows),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBagWidth);
  return ThemedWindow(" Inventory ", std::move(body));
}

ftxui::Element TradePanel::Render() const {
  // Their name lands only once they have joined; until then the window is a
  // place set at the table rather than somebody sitting at it.
  std::string theirs;
  if (trade_.partner_joined()) {
    theirs = " " + trade_.partner_name() + " ";
  }
  return ftxui::vbox({
      ftxui::hbox({
          RenderOffer(trade_.mine(), " " + character_.username() + " ",
                      /*mine=*/true),
          RenderOffer(trade_.theirs(), theirs, /*mine=*/false),
      }),
      RenderBag(),
  });
}

}  // namespace ms
