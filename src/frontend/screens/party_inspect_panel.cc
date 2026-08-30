#include "src/frontend/screens/party_inspect_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "google/protobuf/util/message_differencer.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/widgets/equipped_list.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

constexpr char kCursorHere[] = "> ";
constexpr char kCursorAway[] = "  ";

}  // namespace

PartyInspectPanel::PartyInspectPanel(GameState& state)
    : state_(state), character_(state.rng, Character()) {
}

void PartyInspectPanel::SetPlayer(const PlayerInfo& player) {
  if (google::protobuf::util::MessageDifferencer::Equals(player, shown_)) {
    return;
  }
  bool same_member = player.account_id() == shown_.account_id();
  shown_ = player;
  character_.RestoreFrom(player.sheet(), state_.equips, state_.items);
  character_.UseEquipSets(state_.equip_sets);
  if (!same_member) {
    Reset();
    return;
  }
  // They changed under the cursor -- took off a hat, or a whole set. Held
  // where it was rather than thrown back to the top.
  cursor_ = std::clamp(cursor_, 0, std::max(0, ItemCount() - 1));
}

void PartyInspectPanel::Reset() {
  cursor_ = 0;
}

int PartyInspectPanel::ItemCount() const {
  return static_cast<int>(character_.equipped().size());
}

void PartyInspectPanel::MoveCursor(int delta) {
  cursor_ = StepCursor(cursor_, delta, ItemCount());
}

const EquipInstance* PartyInspectPanel::selected_item() const {
  int at = 0;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       character_.equipped()) {
    if (at++ == cursor_) {
      return &kv.second;
    }
  }
  return nullptr;
}

int PartyInspectPanel::VisibleRows(int items) const {
  int room = max_rows_ > 0 ? max_rows_ - kFixedRows : kListRows;
  return std::clamp(items, 1,
                    std::max(kLeastListRows, std::min(room, kListRows)));
}

ftxui::Element PartyInspectPanel::RenderEquipped() const {
  std::vector<EquippedRow> rows =
      EquippedRows(character_, cursor_, name_clock_.Elapsed());
  if (rows.empty()) {
    return EmptyState("empty");
  }
  std::vector<ftxui::Element> drawn;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    bool on_cursor = i == cursor_;
    ftxui::Element row =
        ftxui::text((on_cursor ? kCursorHere : kCursorAway) + rows[i].text);
    if (rows[i].inactive) {
      // Worn but contributing nothing, the same as on the player's own list.
      row |= ftxui::dim;
    }
    if (on_cursor) {
      // The row the frame scrolls to, so the cursor cannot walk out of view.
      row |= ftxui::focus;
    }
    drawn.push_back(std::move(row));
  }
  return ftxui::vbox({
      ftxui::text(EquippedHeader()),
      ThemedSeparator(),
      ftxui::vbox(std::move(drawn)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                      VisibleRows(static_cast<int>(rows.size()))),
  });
}

ftxui::Element PartyInspectPanel::Render() const {
  // The sheet is drawn by the screen the member reads their own stats on, so
  // the two cannot disagree about what a number is or what it is called --
  // window and all, at the width it keeps everywhere else.
  // No account: this is somebody else's sheet, so it carries no Farm/Boss row.
  AllStatsPanel stats(character_, /*account=*/nullptr, state_.skills);
  ftxui::Element sheet =
      stats.Render() |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, AllStatsPanel::kTotalWidth);
  ftxui::Element worn =
      RenderEquipped() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
  // A vbox stretches its children, so the narrower window is held to its width
  // and centred over the wider one by hand.
  return ftxui::vbox({
      ftxui::hbox({ftxui::filler(), std::move(sheet), ftxui::filler()}),
      ThemedWindow(" Equipped ", std::move(worn)),
  });
}

}  // namespace ms
