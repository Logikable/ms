#include "src/frontend/notification_box.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"

namespace ms {
namespace {

// The box's minimum width, wide enough that two short lines still look like a
// box and not a scrap of text against the border.
constexpr int kWidth = 24;

}  // namespace

void NotificationBox::Raise(std::vector<std::string> lines) {
  lines_ = std::move(lines);
  seconds_ = kNotificationSeconds;
  touched_ = false;
}

void NotificationBox::Advance(double elapsed_seconds) {
  seconds_ = std::max(0.0, seconds_ - elapsed_seconds);
}

void NotificationBox::Touch() {
  touched_ = true;
}

void NotificationBox::Dismiss() {
  lines_.clear();
  seconds_ = 0.0;
}

bool NotificationBox::visible() const {
  if (lines_.empty()) {
    return false;
  }
  return seconds_ > 0.0 || !touched_;
}

ftxui::Element NotificationBox::Render() const {
  ftxui::Elements rows;
  rows.reserve(lines_.size());
  for (const std::string& line : lines_) {
    rows.push_back(CenteredRow(line));
  }
  return AccentWindow("", ftxui::vbox(std::move(rows)), kGold) |
         ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, kWidth);
}

}  // namespace ms
