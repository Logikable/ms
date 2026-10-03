#include "src/frontend/screens/soul_panel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/soul.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr char kNameHeading[] = "Soul";
constexpr char kTierHeading[] = "Tier";
constexpr char kCountHeading[] = "Quantity";

// "Zakum's Soul", from "Zakum's Soul Shard".
std::string SoulName(const ItemPrototype& shard) {
  const std::string suffix = " Shard";
  const std::string& name = shard.name();
  if (name.size() > suffix.size() &&
      name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return name.substr(0, name.size() - suffix.size());
  }
  return name;
}

int Souls(int64_t shards) {
  return static_cast<int>(
      std::min<int64_t>(shards / kShardsPerSoul, kMaxSoulsShown));
}

}  // namespace

void SoulPanel::SetItem(const EquipInstance* item, const CurrencyPurse& purse) {
  item_ = item;
  rows_.clear();
  for (const CurrencyAmount& held : purse.entries()) {
    if (held.prototype().kind() == ITEM_KIND_SOUL_SHARD &&
        held.count() >= kShardsPerSoul) {
      rows_.push_back({held.prototype(), held.count()});
    }
  }
  if (asked_.has_value()) {
    asked_->count = purse.Count(asked_->shard.name());
  }
  selected_ =
      std::clamp(selected_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
  if (prompt_.open() && !Affordable()) {
    prompt_.FocusCancel();
  }
}

void SoulPanel::Reset() {
  selected_ = 0;
  asked_.reset();
  prompt_.Close();
}

const ItemPrototype* SoulPanel::selected_shard() const {
  if (asked_.has_value()) {
    return &asked_->shard;
  }
  if (rows_.empty()) {
    return nullptr;
  }
  return &rows_[selected_].shard;
}

bool SoulPanel::Affordable() const {
  return asked_.has_value() && asked_->count >= kShardsPerSoul;
}

void SoulPanel::MoveCursor(int delta) {
  if (prompt_.open() || rows_.empty()) {
    return;
  }
  selected_ = StepCursor(selected_, delta, rows_.size());
}

ftxui::Element SoulPanel::Render(bool focused) const {
  if (rows_.empty()) {
    return ThemedWindow(
        " Soul Selection ",
        CenteredRow("Collect 10 of any Soul Shard.") | ftxui::dim, focused);
  }
  int name_width = TextColumns(kNameHeading);
  for (const Row& row : rows_) {
    name_width = std::max(name_width, TextColumns(ShortName(row.shard)));
  }
  const int tier_width = TextColumns(kTierHeading);
  const int count_width = TextColumns(kCountHeading);
  std::vector<ftxui::Element> lines = {
      ftxui::text("  " + PadRight(kNameHeading, name_width) + "  " +
                  kTierHeading + "  " + kCountHeading + " "),
      ThemedSeparator(),
  };
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    const Row& row = rows_[i];
    const bool on_cursor = i == selected_;
    lines.push_back(HighlightRow(
        ftxui::text(
            (on_cursor ? "> " : "  ") +
            PadRight(ShortName(row.shard), name_width) + "  " +
            PadRight(SoulTierName(row.shard.soul_tier()), tier_width) + "  " +
            PadLeft(FormatWithCommas(Souls(row.count)), count_width) + " "),
        on_cursor));
  }
  return ThemedWindow(" Soul Selection ", ftxui::vbox(std::move(lines)),
                      focused);
}

std::vector<ftxui::Element> SoulPanel::CurrentSoulRows() const {
  if (item_ == nullptr) {
    return {};
  }
  const Soul& soul = item_->equip_state().soul();
  if (soul.line() == SOUL_LINE_UNSPECIFIED) {
    return {};
  }
  return {
      ThemedSeparator(),
      CenteredRow("This will override"),
      CenteredRow("your current Soul:"),
      CenteredRow(ftxui::hbox({
          ftxui::text("◈") | ftxui::color(kSoul),
          ftxui::text(" Soul: " + soul.boss()),
      })),
      CenteredRow(SoulEffectText(soul, item_->prototype())),
  };
}

ftxui::Element SoulPanel::RenderConfirm() const {
  const ItemPrototype* shard = selected_shard();
  std::vector<ftxui::Element> body = {
      CenteredRow("Apply " + (shard == nullptr ? "" : SoulName(*shard)) + "?"),
  };
  for (ftxui::Element& row : CurrentSoulRows()) {
    body.push_back(std::move(row));
  }
  return DialogWindow(" Soul ", std::move(body),
                      ConfirmButtons(prompt_.confirm_focus(), Affordable()));
}

RerollAction SoulPanel::OnEvent(ftxui::Event event) {
  if (!prompt_.open()) {
    if (rows_.empty()) {
      return RerollAction::kNone;
    }
    asked_ = rows_[selected_];
  }
  const RerollAction action = prompt_.OnEvent(std::move(event), Affordable(),
                                              /*has_after=*/false);
  if (!prompt_.open()) {
    asked_.reset();
  }
  return action;
}

}  // namespace ms
