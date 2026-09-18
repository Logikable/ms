#include "src/frontend/screens/trade_panel.h"

#include <algorithm>
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
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"

namespace ms {
namespace {

// Each offer window, borders included, and the bag under the pair of them.
constexpr int kOfferWidth = 54;
constexpr int kBagWidth = 2 * (kOfferWidth + 2) - 2;

// The rows an offer keeps for what is put in it, and the rows the bag shows at
// once. Both fixed: a window that grew with what was put in it would move the
// other one's border.
constexpr int kOfferRows = kMaxTradeItems;
constexpr int kBagRows = 11;

// The currency cells. Wide enough for the most either can hold -- a hundred
// billion meso and a million traces -- so a number climbing never moves the
// one beside it.
constexpr int kMesoCell = 20;
constexpr int kTraceCell = 14;

// An offer row's two columns, and the cursor column before them.
constexpr int kOfferNameCell = 37;
constexpr int kOfferCountCell = 12;

// The acceptance mark, which keeps its column either way so that nothing
// beside it moves when a player accepts.
ftxui::Element AcceptMark(bool accepted) {
  if (!accepted) {
    return ftxui::text(" ");
  }
  return ftxui::text("✓") | ftxui::color(kGreen) | ftxui::bold;
}

ftxui::Element CurrencyCells(int64_t meso, int64_t traces, bool mine,
                             int cursor) {
  ftxui::Element meso_cell =
      ftxui::text(PadRight(FormatMeso(meso), kMesoCell)) | ftxui::color(kTheme);
  ftxui::Element trace_cell =
      ftxui::text(PadRight(FormatSpellTraces(traces), kTraceCell)) |
      ftxui::color(kTheme);
  return ftxui::hbox({
      HighlightRow(std::move(meso_cell), mine && cursor == 0),
      ftxui::text("  "),
      HighlightRow(std::move(trace_cell), mine && cursor == 1),
  });
}

}  // namespace

bool Tradeable(const ItemPrototype& proto) {
  return proto.kind() != ITEM_KIND_TOKEN &&
         proto.kind() != ITEM_KIND_SOUL_SHARD &&
         proto.kind() != ITEM_KIND_SPELL_TRACE;
}

TradeOffer OwnTradeOffer::ToWire(const CharacterInstance& character) const {
  TradeOffer offer;
  offer.set_meso(meso);
  offer.set_spell_traces(spell_traces);
  for (int index : equips) {
    if (index < 0 || index >= character.inventory().size()) {
      continue;
    }
    // SavedState rather than equip_state: the flag saying whether this is a
    // trace lives in the C++ type, and the other end has only the fields.
    *offer.add_equips() = character.inventory()[index].SavedState();
  }
  for (const TradeStack& stack : stacks) {
    *offer.add_stacks() = stack;
  }
  return offer;
}

TradePanel::TradePanel(const CharacterInstance& character,
                       const AccountInstance& account)
    : character_(character), account_(account) {
}

void TradePanel::SetTrade(const TradeState& trade) {
  trade_ = trade;
}

void TradePanel::Reset() {
  own_ = OwnTradeOffer();
  zone_ = TradeZone::kMine;
  top_ = 0;
  own_list_ = false;
  own_row_ = 0;
  their_row_ = 0;
  bag_row_ = 0;
  etc_tab_ = false;
}

int TradePanel::ClampedRow(int row, int rows) const {
  if (rows <= 0) {
    return 0;
  }
  return std::max(0, std::min(row, rows - 1));
}

bool TradePanel::in_list() const {
  return zone_ != TradeZone::kMine || own_list_;
}

std::vector<int> TradePanel::BagRows() const {
  std::vector<int> rows;
  if (!etc_tab_) {
    for (int i = 0; i < character_.inventory().size(); ++i) {
      if (std::find(own_.equips.begin(), own_.equips.end(), i) ==
          own_.equips.end()) {
        rows.push_back(i);
      }
    }
    return rows;
  }
  const std::vector<StackableItem>& stacks = character_.stackables();
  for (int i = 0; i < static_cast<int>(stacks.size()); ++i) {
    if (Tradeable(stacks[i].prototype()) && stack_left(i) > 0) {
      rows.push_back(i);
    }
  }
  return rows;
}

int TradePanel::stack_left(int index) const {
  const std::vector<StackableItem>& stacks = character_.stackables();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return 0;
  }
  int left = stacks[index].count();
  for (const TradeStack& put_up : own_.stacks) {
    if (put_up.name() == stacks[index].name()) {
      left -= put_up.count();
    }
  }
  return std::max(0, left);
}

void TradePanel::NextZone(int delta) {
  constexpr int kZones = 3;
  int zone = (static_cast<int>(zone_) + delta % kZones + kZones) % kZones;
  zone_ = static_cast<TradeZone>(zone);
  // Your own window is the only one entered above its list: the other two are
  // a list and nothing else.
  own_list_ = false;
}

void TradePanel::MoveCursor(int delta) {
  if (zone_ == TradeZone::kMine && !own_list_) {
    constexpr int kStops = 3;  // meso, traces, Accept
    top_ = ((top_ + delta) % kStops + kStops) % kStops;
    return;
  }
  if (zone_ == TradeZone::kBag) {
    etc_tab_ = !etc_tab_;
    bag_row_ = 0;
  }
}

void TradePanel::MoveRow(int delta) {
  if (zone_ == TradeZone::kTheirs) {
    their_row_ =
        ClampedRow(their_row_ + delta, trade_.theirs().equips_size() +
                                           trade_.theirs().stacks_size());
    return;
  }
  if (zone_ == TradeZone::kBag) {
    bag_row_ = ClampedRow(bag_row_ + delta, static_cast<int>(BagRows().size()));
    return;
  }
  if (!own_list_) {
    // Down off the top row drops into what you have put up, if anything has
    // been; Up there has nowhere to go.
    if (delta > 0 && own_.items() > 0) {
      own_list_ = true;
      own_row_ = 0;
    }
    return;
  }
  if (delta < 0 && own_row_ == 0) {
    own_list_ = false;
    return;
  }
  own_row_ = ClampedRow(own_row_ + delta, own_.items());
}

TradeCursor TradePanel::cursor() const {
  TradeCursor cursor;
  switch (zone_) {
    case TradeZone::kMine:
      if (!own_list_) {
        if (top_ == 2) {
          cursor.kind = TradeCursor::Kind::kAccept;
          return cursor;
        }
        cursor.kind = TradeCursor::Kind::kCurrency;
        cursor.currency =
            top_ == 0 ? TradeCurrency::kMeso : TradeCurrency::kSpellTraces;
        return cursor;
      }
      if (own_.items() == 0) {
        return cursor;
      }
      cursor.kind = TradeCursor::Kind::kOffered;
      cursor.index = ClampedRow(own_row_, own_.items());
      return cursor;
    case TradeZone::kTheirs: {
      int rows = trade_.theirs().equips_size() + trade_.theirs().stacks_size();
      if (rows == 0) {
        return cursor;
      }
      cursor.kind = TradeCursor::Kind::kTheirs;
      cursor.index = ClampedRow(their_row_, rows);
      return cursor;
    }
    case TradeZone::kBag: {
      std::vector<int> rows = BagRows();
      if (rows.empty()) {
        return cursor;
      }
      cursor.kind = TradeCursor::Kind::kBag;
      cursor.index = rows[ClampedRow(bag_row_, static_cast<int>(rows.size()))];
      return cursor;
    }
  }
  return cursor;
}

int64_t TradePanel::held(TradeCurrency currency) const {
  if (currency == TradeCurrency::kMeso) {
    return character_.meso();
  }
  return character_.CountStackable(kSpellTraceName);
}

int64_t TradePanel::offered(TradeCurrency currency) const {
  return currency == TradeCurrency::kMeso ? own_.meso : own_.spell_traces;
}

void TradePanel::PutUpCurrency(TradeCurrency currency, int64_t amount) {
  if (currency == TradeCurrency::kMeso) {
    own_.meso = amount;
    return;
  }
  own_.spell_traces = amount;
}

bool TradePanel::PutUpEquip(int index) {
  // Asked before the cap: one already on the table is not a ninth thing.
  if (std::find(own_.equips.begin(), own_.equips.end(), index) !=
      own_.equips.end()) {
    return true;
  }
  if (own_.items() >= kMaxTradeItems) {
    return false;
  }
  own_.equips.push_back(index);
  return true;
}

bool TradePanel::PutUpStack(int index, int count) {
  const std::vector<StackableItem>& stacks = character_.stackables();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return true;
  }
  const std::string& name = stacks[index].name();
  for (std::vector<TradeStack>::iterator it = own_.stacks.begin();
       it != own_.stacks.end(); ++it) {
    if (it->name() != name) {
      continue;
    }
    if (count <= 0) {
      own_.stacks.erase(it);
      return true;
    }
    it->set_count(count);
    return true;
  }
  if (count <= 0) {
    return true;
  }
  if (own_.items() >= kMaxTradeItems) {
    return false;
  }
  TradeStack stack;
  stack.set_name(name);
  stack.set_count(count);
  own_.stacks.push_back(std::move(stack));
  return true;
}

void TradePanel::TakeBack(int row) {
  int equips = static_cast<int>(own_.equips.size());
  if (row < 0 || row >= own_.items()) {
    return;
  }
  if (row < equips) {
    own_.equips.erase(own_.equips.begin() + row);
  } else {
    own_.stacks.erase(own_.stacks.begin() + (row - equips));
  }
  own_row_ = ClampedRow(own_row_, own_.items());
  own_list_ = own_.items() > 0 && own_list_;
}

std::vector<TradePanel::OfferRow> TradePanel::MyRows() const {
  std::vector<OfferRow> rows;
  for (int index : own_.equips) {
    if (index < 0 || index >= character_.inventory().size()) {
      continue;
    }
    rows.push_back({character_.inventory()[index].name(), ""});
  }
  for (const TradeStack& stack : own_.stacks) {
    rows.push_back({stack.name(), FormatWithCommas(stack.count())});
  }
  return rows;
}

std::vector<TradePanel::OfferRow> TradePanel::TheirRows() const {
  std::vector<OfferRow> rows;
  for (const Equip& equip : trade_.theirs().equips()) {
    rows.push_back({equip.equip_name(), ""});
  }
  for (const TradeStack& stack : trade_.theirs().stacks()) {
    rows.push_back({stack.name(), FormatWithCommas(stack.count())});
  }
  return rows;
}

ftxui::Element TradePanel::RenderOfferTable(const std::vector<OfferRow>& rows,
                                            int cursor) const {
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    ftxui::Element row = ftxui::hbox({
        ftxui::text(i == cursor ? "›" : " "),
        ftxui::text(PadRight(rows[i].name, kOfferNameCell)),
        ftxui::text(PadLeft(rows[i].quantity, kOfferCountCell)),
        ftxui::filler(),
    });
    list.push_back(HighlightRow(std::move(row), i == cursor));
  }
  if (list.empty()) {
    list.push_back(EmptyState("nothing"));
  }
  ftxui::Element header = ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(PadRight("Name", kOfferNameCell)),
      ftxui::text(PadLeft("Quantity", kOfferCountCell)),
      ftxui::filler(),
  });
  return ftxui::vbox({
      std::move(header),
      ftxui::vbox(std::move(list)) |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kOfferRows),
  });
}

ftxui::Element TradePanel::RenderMyTopRow() const {
  return ftxui::hbox({
      ftxui::text(" "),
      CurrencyCells(own_.meso, own_.spell_traces, /*mine=*/true,
                    own_list_ ? -1 : top_),
      ftxui::text("   "),
      ActionButton("Accept",
                   zone_ == TradeZone::kMine && !own_list_ && top_ == 2),
      ftxui::filler(),
      AcceptMark(trade_.mine_accepted()),
      ftxui::text(" "),
  });
}

ftxui::Element TradePanel::RenderTheirTopRow() const {
  return ftxui::hbox({
      ftxui::text(" "),
      AcceptMark(trade_.theirs_accepted()),
      ftxui::filler(),
      CurrencyCells(trade_.theirs().meso(), trade_.theirs().spell_traces(),
                    /*mine=*/false, -1),
      ftxui::text(" "),
  });
}

ftxui::Element TradePanel::RenderMine() const {
  ftxui::Element body =
      ftxui::vbox({
          RenderMyTopRow(),
          ThemedSeparator(),
          RenderOfferTable(
              MyRows(), zone_ == TradeZone::kMine && own_list_ ? own_row_ : -1),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kOfferWidth);
  return ThemedWindow(" " + character_.username() + " ", std::move(body),
                      zone_ == TradeZone::kMine);
}

ftxui::Element TradePanel::RenderTheirs() const {
  // Their name lands only once they have joined; until then the window is a
  // place set at the table rather than somebody sitting at it.
  std::string title;
  if (trade_.partner_joined()) {
    title = RightAlignedTitle(" " + trade_.partner_name() + " ", kOfferWidth);
  }
  ftxui::Element body =
      ftxui::vbox({
          RenderTheirTopRow(),
          ThemedSeparator(),
          RenderOfferTable(TheirRows(),
                           zone_ == TradeZone::kTheirs ? their_row_ : -1),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kOfferWidth);
  return ThemedWindow(title, std::move(body), zone_ == TradeZone::kTheirs);
}

ftxui::Element TradePanel::RenderEquipTab(int width) const {
  ItemListOptions options;
  options.bag = true;
  options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
  options.star_force = Unlocked(Feature::kStarForce, character_, account_);
  options.potential = Unlocked(Feature::kPotential, character_, account_);
  ItemColumns columns = FitItemColumns(width, options);
  std::vector<int> bag = BagRows();
  int cursor = ClampedRow(bag_row_, static_cast<int>(bag.size()));
  std::vector<InventoryRowState> all =
      BuildEquipRows(character_, bag.empty() ? -1 : bag[cursor],
                     std::chrono::steady_clock::duration::zero(), columns);
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(bag.size()); ++i) {
    list.push_back(
        RenderEquipRow(all[bag[i]], zone_ == TradeZone::kBag && i == cursor));
  }
  if (list.empty()) {
    list.push_back(EmptyState("empty"));
  }
  return ftxui::vbox({
      EquipHeader(columns),
      ThemedSeparator(),
      ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBagRows),
  });
}

ftxui::Element TradePanel::RenderEtcTab(int width) const {
  std::vector<int> bag = BagRows();
  int cursor = ClampedRow(bag_row_, static_cast<int>(bag.size()));
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(bag.size()); ++i) {
    // What is left of the stack rather than what it holds: an offered stack
    // is part on the table and part still yours.
    StackableItem left(character_.stackables()[bag[i]].prototype(),
                       stack_left(bag[i]));
    list.push_back(RenderStackRow(left, zone_ == TradeZone::kBag && i == cursor,
                                  std::chrono::steady_clock::duration::zero()));
  }
  if (list.empty()) {
    list.push_back(EmptyState("empty"));
  }
  return ftxui::vbox({
      StackHeader(),
      ThemedSeparator(),
      ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBagRows),
  });
}

ftxui::Element TradePanel::RenderBag() const {
  std::vector<TabSpec> tabs = {{"Equip"}, {"Etc"}};
  ftxui::Element bar = ftxui::hbox({
      TabBar(tabs, etc_tab_ ? 1 : 0, zone_ == TradeZone::kBag, kBagWidth),
      ftxui::filler(),
      // What is LEFT of each, as the lists below are: the two panels always
      // add up to what the player owns.
      ftxui::text(FormatMeso(held(TradeCurrency::kMeso) - own_.meso)) |
          ftxui::color(kTheme),
      ftxui::text("   "),
      ftxui::text(FormatSpellTraces(held(TradeCurrency::kSpellTraces) -
                                    own_.spell_traces)) |
          ftxui::color(kTheme),
      ftxui::text(" "),
  });
  ftxui::Element body =
      ftxui::vbox({
          std::move(bar),
          etc_tab_ ? RenderEtcTab(kBagWidth) : RenderEquipTab(kBagWidth),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBagWidth);
  return ThemedWindow(" Inventory ", std::move(body), zone_ == TradeZone::kBag);
}

ftxui::Element TradePanel::Render() const {
  return ftxui::vbox({
      ftxui::hbox({RenderMine(), RenderTheirs()}),
      RenderBag(),
  });
}

}  // namespace ms
