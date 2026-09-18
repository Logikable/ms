#include "src/frontend/screens/trade_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
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
constexpr int kOfferRows = 7;
constexpr int kBagRows = 10;

// The currency cells. Wide enough for the most either can hold -- a hundred
// billion meso and a million traces -- so a number climbing never moves the
// one beside it.
constexpr int kMesoCell = 20;
constexpr int kTraceCell = 14;

// An offer row's two columns, and the cursor column before them.
constexpr int kOfferNameCell = 37;
constexpr int kOfferCountCell = 12;

// Where a menu hangs inside its window: past the name a row leads with, so it
// covers what an item is worth rather than which item it is.
constexpr int kMenuColumn = 40;

// The acceptance mark, which keeps its column either way so that nothing
// beside it moves when a player accepts.
ftxui::Element AcceptMark(bool accepted) {
  if (!accepted) {
    return ftxui::text(" ");
  }
  return ftxui::text("✓") | ftxui::color(kGreen) | ftxui::bold;
}

// Your own two currencies, the cursor's on the selection band: the meso first,
// where the eye already is, and the room left to its right.
ftxui::Element MyCurrencyCells(int64_t meso, int64_t traces, int cursor) {
  ftxui::Element meso_cell =
      ftxui::text(PadRight(FormatMeso(meso), kMesoCell)) | ftxui::color(kTheme);
  ftxui::Element trace_cell =
      ftxui::text(PadRight(FormatSpellTraces(traces), kTraceCell)) |
      ftxui::color(kTheme);
  return ftxui::hbox({
      HighlightRow(std::move(meso_cell), cursor == 0),
      ftxui::text("  "),
      HighlightRow(std::move(trace_cell), cursor == 1),
  });
}

// And theirs, the mirror of it: the traces, then the meso hard against their
// window's right border, so the two offers are read outward from the middle.
ftxui::Element TheirCurrencyCells(int64_t meso, int64_t traces) {
  return ftxui::hbox({
      ftxui::text(PadLeft(FormatSpellTraces(traces), kTraceCell)) |
          ftxui::color(kTheme),
      ftxui::text("  "),
      ftxui::text(PadLeft(FormatMeso(meso), kMesoCell)) | ftxui::color(kTheme),
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
    : character_(character),
      account_(account),
      menu_({"Inspect", "Offer", "Remove", "Close"}) {
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
  menu_open_ = false;
}

void TradePanel::OpenMenu() {
  TradeCursor::Kind kind = cursor().kind;
  menu_.Reset();
  switch (kind) {
    case TradeCursor::Kind::kBag:
      menu_.Hide(kTradeMenuRemove);
      break;
    case TradeCursor::Kind::kOffered:
      menu_.Hide(kTradeMenuOffer);
      break;
    case TradeCursor::Kind::kTheirs:
      menu_.Hide(kTradeMenuOffer);
      menu_.Hide(kTradeMenuRemove);
      break;
    default:
      return;
  }
  menu_open_ = true;
}

void TradePanel::MoveMenuCursor(int delta) {
  if (delta < 0) {
    menu_.Up();
    return;
  }
  menu_.Down();
}

ftxui::Box& TradePanel::CursorBox(TradeZone zone) const {
  // Only the window holding the cursor keeps its row: every list marks its
  // selected row so the frame scrolls to it, and the last one drawn would
  // otherwise be the one a menu opened beside.
  return zone == zone_ ? cursor_box_ : scratch_box_;
}

int TradePanel::MenuRow() const {
  // Read from the RENDER rather than worked out from the cursor: every list
  // here scrolls, and a place in the data stops agreeing with the row on
  // screen as soon as one does. One row back from it, so the entry standing
  // highlighted lands beside what the menu is about rather than below it.
  return cursor_box_.y_min - panel_box_.y_min - 1;
}

int TradePanel::MenuColumn() const {
  if (zone_ == TradeZone::kTheirs) {
    return kOfferWidth + 2 + kMenuColumn;
  }
  return kMenuColumn;
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

int TradePanel::stack_offered(int index) const {
  const std::vector<StackableItem>& stacks = character_.stackables();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return 0;
  }
  int up = 0;
  for (const TradeStack& put_up : own_.stacks) {
    if (put_up.name() == stacks[index].name()) {
      up += put_up.count();
    }
  }
  return up;
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

void TradePanel::PutUpEquip(int index) {
  if (std::find(own_.equips.begin(), own_.equips.end(), index) !=
      own_.equips.end()) {
    return;
  }
  own_.equips.push_back(index);
}

void TradePanel::PutUpStack(int index, int count) {
  const std::vector<StackableItem>& stacks = character_.stackables();
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return;
  }
  const std::string& name = stacks[index].name();
  for (std::vector<TradeStack>::iterator it = own_.stacks.begin();
       it != own_.stacks.end(); ++it) {
    if (it->name() != name) {
      continue;
    }
    // A stack already up is changed rather than added to, and taken down
    // altogether at nothing.
    if (count <= 0) {
      own_.stacks.erase(it);
    } else {
      it->set_count(count);
    }
    return;
  }
  if (count <= 0) {
    return;
  }
  TradeStack stack;
  stack.set_name(name);
  stack.set_count(count);
  own_.stacks.push_back(std::move(stack));
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
                                            int cursor, bool focused,
                                            ftxui::Box& cursor_box) const {
  const int height = kOfferRows + 2;  // the header and its rule
  if (rows.empty()) {
    // No header over nothing, as an empty bag tab draws: column names are
    // there to tell rows apart, and there are none.
    return ftxui::vbox({EmptyState("nothing", /*gutter=*/2), ftxui::filler()}) |
           ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
  }
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    // The caret alone marks the row: a handful of rows in a window of their
    // own are not a column of stats to be read back to a name.
    // The caret shows only while this window holds the cursor: one drawn in
    // each of them would put the selection in three places at once.
    ftxui::Element row = ftxui::hbox({
        ftxui::text(focused && i == cursor ? "> " : "  "),
        ftxui::text(PadRight(rows[i].name, kOfferNameCell)),
        ftxui::text(PadLeft(rows[i].quantity, kOfferCountCell)),
        ftxui::filler(),
    });
    if (i == cursor) {
      // What the frame scrolls to, marked whether or not this window holds
      // focus so the view does not jump on the way back, and reflected so a
      // menu knows the row to open beside.
      row = std::move(row) | ftxui::focus | ftxui::reflect(cursor_box);
    }
    list.push_back(std::move(row));
  }
  return ftxui::vbox({
             ftxui::hbox({
                 ftxui::text("  "),
                 ftxui::text(PadRight("Name", kOfferNameCell)),
                 ftxui::text(PadLeft("Quantity", kOfferCountCell)),
                 ftxui::filler(),
             }),
             ThemedSeparator(),
             // Only the rows scroll; the header and its rule stay put.
             ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator |
                 ftxui::yframe | ftxui::flex,
         }) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
}

ftxui::Element TradePanel::RenderMyTopRow() const {
  return ftxui::hbox({
      ftxui::text(" "),
      MyCurrencyCells(own_.meso, own_.spell_traces, own_list_ ? -1 : top_),
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
      TheirCurrencyCells(trade_.theirs().meso(),
                         trade_.theirs().spell_traces()),
      ftxui::text(" "),
  });
}

ftxui::Element TradePanel::RenderMine() const {
  ftxui::Element body =
      ftxui::vbox({
          RenderMyTopRow(),
          ThemedSeparator(),
          RenderOfferTable(
              MyRows(),
              own_.items() == 0 ? -1 : ClampedRow(own_row_, own_.items()),
              zone_ == TradeZone::kMine && own_list_,
              CursorBox(TradeZone::kMine)),
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
    title = " " + trade_.partner_name() + " ";
  }
  ftxui::Element body =
      ftxui::vbox({
          RenderTheirTopRow(),
          ThemedSeparator(),
          RenderOfferTable(
              TheirRows(),
              ClampedRow(their_row_, static_cast<int>(TheirRows().size())),
              zone_ == TradeZone::kTheirs, CursorBox(TradeZone::kTheirs)),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kOfferWidth);
  return ThemedWindow(title, std::move(body), zone_ == TradeZone::kTheirs,
                      /*blink=*/false, std::chrono::steady_clock::now(),
                      TitleAlign::kRight);
}

ftxui::Element TradePanel::RenderBag() const {
  std::vector<int> bag = BagRows();
  const bool focused = zone_ == TradeZone::kBag;
  int cursor = ClampedRow(bag_row_, static_cast<int>(bag.size()));
  // The tab rides in the key, so the same row of the other tab counts as a
  // different name and starts from its own head.
  name_clock_.Follow((etc_tab_ ? kTabStride : 0) + cursor, focused);

  ftxui::Element list;
  if (etc_tab_) {
    // What is LEFT of each stack rather than what it holds: an offered stack
    // is part on the table and part still yours.
    std::vector<StackableItem> left;
    left.reserve(bag.size());
    for (int index : bag) {
      left.emplace_back(character_.stackables()[index].prototype(),
                        stack_left(index));
    }
    std::vector<int> rows(left.size());
    std::iota(rows.begin(), rows.end(), 0);
    list =
        RenderStackList(left, rows, cursor, focused, CursorBox(TradeZone::kBag),
                        /*highlighted=*/false, name_clock_.Elapsed());
  } else {
    ItemListOptions options;
    options.bag = true;
    options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
    options.star_force = Unlocked(Feature::kStarForce, character_, account_);
    options.potential = Unlocked(Feature::kPotential, character_, account_);
    list = RenderEquipList(character_, bag, cursor, focused,
                           FitItemColumns(kBagWidth, options),
                           CursorBox(TradeZone::kBag), /*highlighted=*/false,
                           name_clock_.Elapsed());
  }
  std::vector<TabSpec> tabs = {{"Equip"}, {"Etc"}};
  ftxui::Element body =
      ftxui::vbox({
          // The bar never holds the cursor here: Left and Right switch tabs
          // from the list, so nothing has to climb out of it first.
          RenderBagTabBar(tabs, etc_tab_ ? 1 : 0,
                          RenderBalances(held(TradeCurrency::kMeso) - own_.meso,
                                         held(TradeCurrency::kSpellTraces) -
                                             own_.spell_traces,
                                         character_, account_),
                          /*row_selected=*/false, /*highlighted=*/false,
                          ftxui::text(""), kBagWidth, bar_box_),
          // The header, its rule and the rows, which are all the list is.
          std::move(list) |
              ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kBagRows + 2),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBagWidth);
  return ThemedWindow(" Inventory ", std::move(body), focused);
}

ftxui::Element TradePanel::Render() const {
  // Reflected so a menu can be put beside a row inside it: what the lists
  // report is where they landed on the SCREEN, and a floating menu is placed
  // from the panel's own corner.
  ftxui::Element screen = ftxui::vbox({
                              ftxui::hbox({RenderMine(), RenderTheirs()}),
                              RenderBag(),
                          }) |
                          ftxui::reflect(panel_box_);
  if (!menu_open_) {
    return screen;
  }
  return ftxui::dbox({
      std::move(screen),
      Floating(menu_.Render(MenuRow(), MenuColumn())),
  });
}

}  // namespace ms
