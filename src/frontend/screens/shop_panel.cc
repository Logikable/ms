#include "src/frontend/screens/shop_panel.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Column widths. Name and level match the bag's equip tab, so the two lists
// line up and the same item reads the same way in both. The type column takes
// the longest name a weapon has ("Two-Handed Sword"). The cost column holds the
// dearest thing on the shelf, the Meister Ring's eight figures and its coin,
// with nothing to spare: a wider price would slide the whole window.
constexpr int kNameWidth = 26;
constexpr int kTypeWidth = 16;
constexpr int kLevelWidth = 7;
constexpr int kCostWidth = 13;

// Stock rows on screen at once. Deep enough to hold most of a warrior's list --
// the longest any class has -- and short enough that the window still clears
// the bottom of a modest terminal.
constexpr int kVisibleRows = 15;

// Room for any row index under one tab, so folding the tab and the row into
// one key cannot make two different selections collide.
constexpr int kNameClockTabStride = 4096;

// Every row and header ends one column clear of the border, and the scroll bar
// takes the column after that whether or not it is drawn. Held exactly, so the
// window is one width: it is drawn centred, and a price one digit longer than
// the last would otherwise slide the whole shop sideways.
constexpr int kContentWidth =
    2 + kNameWidth + 2 + kTypeWidth + 2 + kLevelWidth + kCostWidth + 1 + 1;

// A "<mark> <text>" cell, right-aligned in kCostWidth screen columns. A coin
// is two columns and a token's mark one, which is what PadLeft counts, so the
// cell is the same width whatever the number in it.
std::string MarkedCell(const std::string& mark, const std::string& text) {
  return PadLeft(mark + " " + text, kCostWidth);
}

std::string CoinCell(const std::string& text) {
  return MarkedCell("🪙", text);
}

// The cost cell of one row. The mark keeps its own colour whatever the price
// does: red is the reason a row is out of reach, and the currency is not a
// reason (colors.h).
ftxui::Element CostCell(const ItemPrototype* token, const std::string& text,
                        bool affordable) {
  if (token == nullptr) {
    return RedUnless(ftxui::text(CoinCell(text)), affordable);
  }
  ftxui::Element amount = RedUnless(ftxui::text(" " + text), affordable);
  // Padded by hand rather than by MarkedCell: the mark is its own element so
  // it can keep its own colour.
  int columns = TextColumns(token->currency_mark() + " " + text);
  return ftxui::hbox({
      ftxui::text(std::string(std::max(0, kCostWidth - columns), ' ')),
      ftxui::text(token->currency_mark()) |
          ftxui::color(MarkColor(token->currency_color())),
      std::move(amount),
  });
}

// Two leading spaces match the "  " / "> " cursor on the rows below.
ftxui::Element ColumnHeader(const ItemPrototype* token) {
  std::string cost = token == nullptr
                         ? CoinCell("Cost")
                         : MarkedCell(token->currency_mark(), "Cost");
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Type", kTypeWidth) + "  " +
                     PadRight("Level", kLevelWidth) + cost);
}

// The Etc shelf has no type and no level to show, so the two columns between
// the name and the price become one: how many the player owns already. "Owned"
// is the word for that everywhere -- the buy dialog says it too.
ftxui::Element EtcColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Owned", kTypeWidth + 2 + kLevelWidth) +
                     CoinCell("Cost"));
}

// The buy-back shelf holds both kinds at once, so it shows what only one of
// them has: an equip comes back as the one item it was, and a stack comes back
// as many. The type is left to Inspect -- a name the player chose to sell is
// one they already know.
ftxui::Element BuyBackColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Qty", kTypeWidth + 2 + kLevelWidth) +
                     CoinCell("Cost"));
}

// What the type column says about one item. An accessory has no equip type --
// nothing about a ring turns on which kind of ring it is -- so it falls back to
// the slot, which is the column the bag shows for the same item.
std::string TypeCell(const EquipPrototype& proto) {
  std::string type = FormatEquipType(proto.equip_type());
  return type.empty() ? FormatSlot(proto.equip_slot()) : type;
}

// The level cell, e.g. "Lv30  ". An item with no level requirement reads as
// level 1 rather than as a blank, matching the bag.
std::string LevelCell(const EquipPrototype& proto) {
  int level = proto.required_level();
  if (level <= 0) {
    level = 1;
  }
  return PadRight("Lv" + std::to_string(level), kLevelWidth);
}

}  // namespace

ShopPanel::ShopPanel(const CharacterInstance& character,
                     const std::map<std::string, EquipPrototype>& equips,
                     const std::map<std::string, ItemPrototype>& items)
    : character_(character),
      equips_(equips),
      items_(items),
      menu_({"Inspect", "Buy", "Close"}) {
  Reset();
}

int ShopPanel::RowCount() const {
  if (tab_ == kShopBuyBackTab) {
    return character_.buy_backs().size();
  }
  return static_cast<int>(stock_.size());
}

void ShopPanel::Restock() {
  // Rebuilt rather than kept, because the shop stocks what this character can
  // hold and that changes when they advance. Cheap: the catalog is small and
  // the screen opens on a keypress.
  stock_.clear();
  // The buy-back shelf is the character's, not the shop's. It is read where it
  // lives, so a sale made while the screen is open shows up without restocking
  // -- and nothing here filters it: what a player sold is theirs to buy back
  // whatever their class or level says now.
  if (tab_ == kShopBuyBackTab) {
    return;
  }
  if (tab_ == kShopEtcTab) {
    stock_ = ShopEtcStock(items_);
    return;
  }
  Payment payment = pay_ == kShopTokenTab ? kPaidInTokens : kPaidInMeso;
  std::vector<std::string> shelf = tab_ == kShopEquipsTab
                                       ? ShopEquipStock(equips_, payment)
                                       : ShopWeaponStock(equips_, payment);
  for (const std::string& key : shelf) {
    if (character_.MeetsJob(equips_.at(key))) {
      stock_.push_back(key);
    }
  }
}

void ShopPanel::Reset() {
  tab_ = kShopWeaponTab;
  pay_ = kShopMesoTab;
  Restock();
  zone_ = kZoneList;
  selected_ = 0;
  first_visible_ = 0;
  menu_open_ = false;
}

void ShopPanel::StepTab(int direction) {
  int next = tab_ + direction;
  if (next < 0 || next >= kNumShopTabs) {
    return;  // the ends of the bar are walls, not wrapping points
  }
  tab_ = next;
  Restock();
  selected_ = 0;
  first_visible_ = 0;
}

void ShopPanel::StepPayTab(int direction) {
  int next = pay_ + direction;
  if (next < 0 || next >= kNumShopPayTabs) {
    return;
  }
  pay_ = next;
  Restock();
  selected_ = 0;
  first_visible_ = 0;
}

bool ShopPanel::HasPayRow() const {
  return tab_ == kShopWeaponTab || tab_ == kShopEquipsTab;
}

void ShopPanel::MoveCursor(int delta) {
  int bars = HasPayRow() ? 2 : 1;
  int next = StepCursor(CursorStop(), delta, bars + RowCount());
  if (next < bars) {
    // The window stays where it is: the cursor has left the list rather than
    // moved within it, and it comes back to the row it left.
    zone_ = next == 0 ? kZoneTabs : kZonePay;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - bars;
  ScrollToCursor();
}

// The stops above the list: the tab bar always, and the pay bar under the two
// tabs that have one.
int ShopPanel::CursorStop() const {
  if (zone_ == kZoneTabs) {
    return 0;
  }
  if (zone_ == kZonePay) {
    return 1;
  }
  return selected_ + (HasPayRow() ? 2 : 1);
}

void ShopPanel::ScrollToCursor() {
  first_visible_ = ScrollWindowStart(RowCount(), selected_, kVisibleRows);
}

void ShopPanel::OpenMenu() {
  if (zone_ != kZoneList) {
    // Nothing to open a menu on: the cursor is on a bar, not on an item.
    return;
  }
  if (selected_item() == nullptr && selected_stackable() == nullptr &&
      selected_buy_back() == nullptr) {
    return;
  }
  menu_.Reset();
  menu_open_ = true;
}

bool ShopPanel::menu_open() const {
  return menu_open_;
}

Screen ShopPanel::OnMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    menu_open_ = false;
    return kShop;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kShopMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kShopMenu;
  }
  if (IsForward(event)) {
    // Closed on the way out whichever entry was chosen, so the screen it opens
    // is not drawn with the menu still standing over the list behind it.
    menu_open_ = false;
    if (menu_.selected() == kShopMenuInspect) {
      return kShopInspect;
    }
    if (menu_.selected() == kShopMenuBuy) {
      return kShopBuy;
    }
    return kShop;
  }
  // Swallow everything else: the menu is modal over the list.
  return kShopMenu;
}

const BuyBackEntry* ShopPanel::selected_buy_back() const {
  if (tab_ != kShopBuyBackTab || selected_ < 0 ||
      selected_ >= character_.buy_backs().size()) {
    return nullptr;
  }
  return &character_.buy_backs().Get(selected_);
}

const ItemPrototype* ShopPanel::RowToken(const EquipPrototype& proto) const {
  if (proto.token_item().empty()) {
    return nullptr;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      items_.find(proto.token_item());
  return it == items_.end() ? nullptr : &it->second;
}

const ItemPrototype* ShopPanel::selected_token() const {
  // Asked of the open shelf as well as of the row, so the answer is the
  // contract the header states however the two ever come apart.
  if (!HasPayRow() || pay_ != kShopTokenTab) {
    return nullptr;
  }
  const EquipPrototype* item = selected_item();
  return item == nullptr ? nullptr : RowToken(*item);
}

const EquipPrototype* ShopPanel::selected_item() const {
  if (tab_ == kShopEtcTab || tab_ == kShopBuyBackTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &equips_.at(stock_[selected_]);
}

const ItemPrototype* ShopPanel::selected_stackable() const {
  if (tab_ != kShopEtcTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &items_.at(stock_[selected_]);
}

bool ShopPanel::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Left and Right belong to whichever bar the cursor is standing on, and only
  // while it is standing on one -- in the list they would be a keypress that
  // quietly changed the list under the cursor.
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    int direction = event == ftxui::Event::ArrowLeft ? -1 : 1;
    if (zone_ == kZoneTabs) {
      StepTab(direction);
      return true;
    }
    if (zone_ == kZonePay) {
      StepPayTab(direction);
      return true;
    }
  }
  return false;
}

const ItemPrototype* ShopPanel::TabToken() const {
  if (!HasPayRow() || pay_ != kShopTokenTab) {
    return nullptr;
  }
  // The unfiltered shelf, so the tab still knows what it deals in while the
  // class filter has left the player nothing to look at.
  std::vector<std::string> shelf =
      tab_ == kShopEquipsTab ? ShopEquipStock(equips_, kPaidInTokens)
                             : ShopWeaponStock(equips_, kPaidInTokens);
  for (const std::string& key : shelf) {
    std::map<std::string, ItemPrototype>::const_iterator it =
        items_.find(equips_.at(key).token_item());
    if (it != items_.end()) {
      return &it->second;
    }
  }
  return nullptr;
}

ftxui::Element ShopPanel::RenderTabBar() const {
  // Chips are white while the bar holds the cursor and theme-blue otherwise,
  // which is how the player tells the arrow keys are on the bar.
  bool focused = zone_ == kZoneTabs;
  const std::vector<TabSpec> kTabs = {
      {"Weapon"}, {"Equips"}, {"Etc"}, {"Buy-Back"}};
  std::vector<ftxui::Element> chips;
  // No width limit: four fixed labels, and the shop's rows are far wider.
  chips.push_back(TabBar(kTabs, tab_, focused, /*width=*/0));
  // The counter sits in what the chips leave rather than over the whole row: a
  // third chip took the bar out to where a centred counter was drawn on top of
  // it, and a fourth would reach further still.
  const ItemPrototype* token = TabToken();
  ftxui::Element counter =
      token == nullptr
          ? ftxui::text(FormatMeso(character_.meso())) | ftxui::color(kTheme)
          : ftxui::hbox({
                ftxui::text(token->currency_mark()) |
                    ftxui::color(MarkColor(token->currency_color())),
                ftxui::text(
                    " " + FormatWithCommas(character_.CountStackable(*token))) |
                    ftxui::color(kTheme),
            });
  chips.push_back(ftxui::filler());
  chips.push_back(std::move(counter));
  chips.push_back(ftxui::filler());
  return ftxui::hbox(std::move(chips));
}

// Blank under a tab with nothing to choose, so the window is one height
// whichever tab is open -- it is drawn centred, and a row that came and went
// would move the whole shop up the screen.
ftxui::Element ShopPanel::RenderPayBar() const {
  if (!HasPayRow()) {
    return ftxui::text("");
  }
  const std::vector<TabSpec> kTabs = {{"Meso"}, {"Token"}};
  return TabBar(kTabs, pay_, zone_ == kZonePay, /*width=*/0);
}

// The price is red when the player cannot pay it, so the list answers "what can
// I buy" without arithmetic on every row.
ftxui::Element ShopPanel::RenderEtcRow(
    const ItemPrototype& item, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  ftxui::Element cost =
      RedUnless(ftxui::text(CoinCell(FormatWithCommas(item.shop_price()))),
                item.shop_price() <= character_.meso());
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(item.name(), kNameWidth, elapsed) +
                  "  " +
                  PadRight(FormatWithCommas(character_.CountStackable(item)),
                           kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

// The level is red on the bag's rule and in the bag's colour. There is no class
// to colour: the list holds nothing this character is the wrong class for.
ftxui::Element ShopPanel::RenderEquipRow(
    const EquipPrototype& proto, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  ftxui::Element level =
      RedUnless(ftxui::text(LevelCell(proto)), character_.MeetsLevel(proto));
  // Each row asks in its own currency: the shelf it came off says which, and
  // the item says how many.
  const ItemPrototype* token = RowToken(proto);
  int64_t price = token == nullptr ? proto.shop_price() : proto.token_price();
  int64_t held =
      token == nullptr ? character_.meso() : character_.CountStackable(*token);
  ftxui::Element cost = CostCell(token, FormatWithCommas(price), price <= held);
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(proto.name(), kNameWidth, elapsed) +
                  "  " +
                  // The type scrolls too: "Arrow for Crossbow" is wider than
                  // the column, and a cut type reads as a different item. Only
                  // here -- every other column in the game is written to fit.
                  ScrollingWindow(TypeCell(proto), kTypeWidth, elapsed) + "  "),
      std::move(level),
      std::move(cost),
      ftxui::text(" "),
  });
}

ftxui::Element ShopPanel::RenderBuyBackRow(
    const BuyBackEntry& entry, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  // The name is the item's own, and a trace's name already says it is one.
  std::string name;
  std::string qty;
  if (entry.has_equip()) {
    const EquipPrototype* proto =
        FindEquipByName(equips_, entry.equip().equip_name());
    name = proto == nullptr ? entry.equip().equip_name() : proto->name();
    if (entry.equip().trace()) {
      name += " Trace";
    }
  } else {
    name = entry.stack().name();
    qty = FormatWithCommas(entry.stack().count());
  }
  ftxui::Element cost =
      RedUnless(ftxui::text(CoinCell(FormatWithCommas(entry.unit_price()))),
                entry.unit_price() <= character_.meso());
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(name, kNameWidth, elapsed) + "  " +
                  PadRight(qty, kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

ftxui::Element ShopPanel::RenderStock() const {
  std::vector<ftxui::Element> item_rows;
  if (RowCount() == 0) {
    // The game's one word for a list with nothing in it. A shelf is empty for
    // a reason the player can already see -- the tab they are standing on.
    item_rows.push_back(EmptyState("empty", /*gutter=*/2));
  }
  // The tab rides in the key beside the row, so the same row of another tab
  // counts as a different name and starts from its own head.
  name_clock_.Follow(tab_ * kNameClockTabStride + selected_);
  int last = std::min(RowCount(), first_visible_ + kVisibleRows);
  for (int i = first_visible_; i < last; ++i) {
    bool selected = zone_ == kZoneList && i == selected_;
    std::string cursor = selected ? "> " : "  ";
    std::chrono::steady_clock::duration elapsed =
        selected ? name_clock_.Elapsed()
                 : std::chrono::steady_clock::duration::zero();
    if (tab_ == kShopBuyBackTab) {
      item_rows.push_back(
          RenderBuyBackRow(character_.buy_backs().Get(i), cursor, elapsed));
    } else if (tab_ == kShopEtcTab) {
      item_rows.push_back(RenderEtcRow(items_.at(stock_[i]), cursor, elapsed));
    } else {
      item_rows.push_back(
          RenderEquipRow(equips_.at(stock_[i]), cursor, elapsed));
    }
  }
  // Padded out to the full window, so the shop is one height whatever the tab
  // holds. It is drawn centred: a shelf two rows shorter than the last would
  // otherwise slide the title, the bar and the column header up the screen.
  while (static_cast<int>(item_rows.size()) < kVisibleRows) {
    item_rows.push_back(ftxui::text(""));
  }
  return ftxui::hbox({
      ftxui::vbox(std::move(item_rows)),
      ScrollBar(RowCount(), first_visible_, kVisibleRows),
  });
}

ftxui::Element ShopPanel::Render() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderTabBar());
  rows.push_back(RenderPayBar());
  rows.push_back(ThemedSeparator());
  if (tab_ == kShopBuyBackTab) {
    rows.push_back(BuyBackColumnHeader());
  } else if (tab_ == kShopEtcTab) {
    rows.push_back(EtcColumnHeader());
  } else {
    rows.push_back(ColumnHeader(TabToken()));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(RenderStock());
  // Held at one width for the same reason it is held at one height: a shelf
  // whose dearest item has a digit more than the last would otherwise widen
  // the window, and a centred window that changes width moves.
  ftxui::Element body = ftxui::vbox(std::move(rows)) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
  ftxui::Element window = ThemedWindow(" Shop ", std::move(body));
  if (!menu_open_) {
    return window;
  }
  // Anchored inside the panel rather than on the terminal, because the shop is
  // centred and so has no fixed place on screen to measure from.
  //
  // kMenuCol clears the border and the name column, so the menu covers what the
  // item asks for rather than what it is called.
  constexpr int kMenuCol = 1 + 2 + kNameWidth;
  // Floated, so a menu opened on one of the last few items hangs out past the
  // bottom border instead of stretching the window down to hold it. Sliding it
  // up to fit would leave it clear of the item it belongs to, which reads as a
  // menu for some other row.
  return ftxui::dbox({
      std::move(window),
      Floating(menu_.Render(MenuRow(), kMenuCol)),
  });
}

int ShopPanel::MenuRow() const {
  // +6 rows: the window's top border, the two tab rows, their separator, the
  // column header, its separator. Measured from the top of the window, so it is
  // the cursor's place within the scrolled view and not its place in the stock.
  constexpr int kFirstItemRow = 6;
  return kFirstItemRow + selected_ - first_visible_;
}

}  // namespace ms
