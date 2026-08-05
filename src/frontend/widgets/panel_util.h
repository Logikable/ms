#ifndef MS_SRC_FRONTEND_WIDGETS_PANEL_UTIL_H_
#define MS_SRC_FRONTEND_WIDGETS_PANEL_UTIL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// True for any "go back" key (Escape or Backspace).
inline bool IsBack(const ftxui::Event& e) {
  return e == ftxui::Event::Escape || e == ftxui::Event::Backspace;
}

// True for any "confirm / advance" key (Enter or Space).
inline bool IsForward(const ftxui::Event& e) {
  return e == ftxui::Event::Return || e == ftxui::Event::Character(' ');
}

// Wraps `child` so it always reports itself focusable, whatever the child says.
// Forwards rendering and events to it untouched.
//
// Container::Tab asks only its active child whether it is focusable, and drops
// every key before dispatch when the answer is no. A panel built around an
// ftxui::Menu therefore goes deaf whenever its list is empty -- including the
// parts of it, like a tab bar, that have nothing to do with the list. A panel
// drawn with the Renderer(bool) overload gets this for free; one that wraps a
// real child in order to forward events to it does not.
ftxui::Component AlwaysFocusable(ftxui::Component child);

// A single displayable stat: its label and how to read it from an EquipStats.
struct DisplayStat {
  const char* label;
  int (EquipStats::*fn)() const;
  int GetFrom(const EquipStats& s) const {
    return (s.*fn)();
  }
};

// Canonical display order for equip stats. Zero-value fields are typically
// hidden by callers. Update this array to add or reorder stats site-wide.
inline const DisplayStat kDisplayStats[] = {
    {"STR", &EquipStats::str},    {"DEX", &EquipStats::dex},
    {"INT", &EquipStats::int_},   {"LUK", &EquipStats::luk},
    {"HP", &EquipStats::max_hp},  {"MP", &EquipStats::max_mp},
    {"ATT", &EquipStats::attack}, {"MATT", &EquipStats::magic_attack},
    {"DEF", &EquipStats::def},
};

// The kDisplayStats entry a StatField names, or nullptr for a field with no
// equip stat behind it. Lets a caller that knows a stat by its proto field --
// a job's primary stat, say -- read it off an EquipStats without writing its
// own switch over the four stats.
const DisplayStat* DisplayStatFor(StatField field);

// Pads s to width with trailing spaces, or truncates if longer.
std::string PadRight(const std::string& s, int width);

// Pads s to width with LEADING spaces, right-aligning it in the column.
// Unlike PadRight this never truncates: it is for numbers, and dropping digits
// off a number that outgrew its column would quietly show the wrong one.
std::string PadLeft(const std::string& s, int width);

// Formats an integer with thousands-separator commas (e.g. 1234567 ->
// "1,234,567"). Handles negatives.
std::string FormatWithCommas(int64_t n);

// Formats a meso amount as the coin indicator followed by the comma-separated
// value (e.g. "🪙 1,234,567"). Use everywhere meso is shown.
std::string FormatMeso(int64_t meso);

// Appends "+val label" to out (with "  " separator if non-empty).
// No-op if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

// Returns the display name for an equip slot (e.g. "Weapon"). Returns ""
// for slot types not yet implemented.
std::string FormatSlot(EquipSlot slot);

// Returns the display name for a weapon type (e.g. "Claw"). Returns "" for
// types not yet implemented.
std::string FormatEquipType(EquipType type);

// True for a skill the player casts, attack or otherwise -- everything that
// isn't a passive. It is what the skill list sorts on and what the inspect
// screen titles itself with, so both must answer it the same way.
bool IsActive(const Skill& skill);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Returns the display name for a job (e.g. "Swordman"), or "Unknown" for a job
// not yet given a name.
std::string JobName(Job job);

// Returns the short display label for an AP stat field (e.g. "STR"), or "" for
// STAT_FIELD_UNSPECIFIED.
std::string StatFieldName(StatField field);

// Formats a single item list entry: name (26 cols), slot (10 cols), info
// (padded to 20 cols), and scroll pass/left/restore counts. Pass -1 for all
// three scroll values to render "-" (use for non-upgradeable items).
std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore);

// A one-row progress bar filled to frac (clamped to [0, 1]) in `fill`, with the
// remainder in kBarEmpty. `label` is centered over the bar, dark on the filled
// side and light on the unfilled side; pass "" for an unlabelled bar.
//
// Writes pixels directly rather than using ftxui::gauge, which ignores color
// decorators and cannot carry a label without a dbox overwriting one or the
// other.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label);
// Overload holding the label to one color the whole way across, for a fill
// dark enough to read that color against. The label then keeps its color as
// the bar moves, rather than turning over a character at a time. Don't reach
// for this on a light fill -- that's what the two-tone default is for.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color);

// Takes `element` out of the layout: it asks its parent for no room at all,
// then draws itself at its own size from the parent box's top-left corner.
//
// Put it last in a `dbox` to overlay something. Without this, a dbox grows to
// hold its tallest child and stretches every other child to match, so an
// overlay reaching past the panel it covers pushes that panel's borders out.
// Floating it leaves the panel exactly the size and place it had on its own,
// and the overlay spills outside it instead.
//
// The screen is then the only bound. An overlay that would run off the bottom
// or right edge slides back just far enough to put that corner on it. One too
// big for the screen gives up its top-left instead, which for an overlay is
// the empty space positioning it rather than anything it draws.
ftxui::Element Floating(ftxui::Element element);

// A modal result screen, shown once an action has resolved: the subject
// centered over a rule, `body` below it, then a rule and a [Continue]. Every
// screen the game shows after scrolling, star forcing or recovering is built
// from this, so the rules and the button land in the same place on each.
ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body);

// The one way a panel says it has nothing to show: the reason in parentheses,
// e.g. " (empty)". Use "empty" for a list with no contents and a specific
// reason ("no item", "no matching items") only where it tells the player
// something they couldn't already see. `gutter` is the leading indent, so the
// row can line up with the cursor column of the list it stands in for.
ftxui::Element EmptyState(const std::string& what, int gutter = 1);

// The keys a tab is recorded under once the player has opened it, kept
// together because they are written into the save: changing one forgets that
// anybody ever opened that tab, and it would go gold again for every player.
inline constexpr char kSkillsTabKey[] = "skills";
inline constexpr char kShopTabKey[] = "shop";

// The advancement tab's key for `stage` (1 = 1st job). One key per stage
// rather than one for the tab: the tab arrives again at every advancement
// threshold, and having seen the first is not having seen the second.
std::string AdvanceTabKey(int stage);

// One chip of a tab bar in the game's one tab style: the label padded by a
// space either side, theme-colored, and highlighted when it is the active tab.
// An active chip goes white while its row holds focus and keeps the theme-blue
// invert otherwise, which is how the player tells which bar the arrow keys are
// reaching. Pass row_focused=true unconditionally for a bar that is the only
// thing on its screen.
//
// `unseen` draws the label gold instead of theme blue: a tab the player has
// been given but never opened. It is the quiet half of the level-up
// celebration -- the gold outlives the four seconds of the card and waits on
// the bar until the tab is opened.
ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen = false);

// Renders a bracketed button in the game's one button style, inverted when
// focused. Every button the player can land on is drawn with this -- the
// confirm bar, the amount selector's [1]/[MAX], the [+] beside a stat, and the
// always-selected [Continue] on the result screens.
ftxui::Element ActionButton(const std::string& label, bool focused);

// The border color a main-screen panel draws itself in: gold while it is lit
// to be noticed -- a level-up handing over a panel the player has not seen
// before -- and the theme blue every other moment. One answer rather than a
// conditional repeated in each panel, so lit means the same thing everywhere.
ftxui::Color PanelAccent(bool highlighted);

// ThemedWindow in a color of your choosing, for the few things that step out
// of the steel blue to be noticed -- the level-up and advancement cards, and
// the panels lit up behind them. Every window in the game is built from this,
// so a highlighted one differs from an ordinary one in its color and nothing
// else.
ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused = false);

// Wraps content in a bordered window with the game's steel-blue theme color on
// the border and title. Content foreground is set to white; explicitly colored
// elements (gold stars, amber SF, etc.) and ThemedSeparator override it. Pass
// focused=true to invert the title into a solid chip, marking the panel that
// currently holds focus.
ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused = false);

// Centres a row in its window with a column of clearance on each side. Every
// centred row in the game goes through this rather than bare hcenter, so that
// the longest line on a screen -- whichever it turns out to be -- keeps its
// distance from the border.
ftxui::Element CenteredRow(ftxui::Element row);
ftxui::Element CenteredRow(const std::string& text);

// Returns a horizontal separator rule in `accent`, for a rule inside an
// AccentWindow: a steel-blue rule across a gold card reads as a seam.
ftxui::Element AccentSeparator(ftxui::Color accent);

// The rule to draw inside a main-screen panel: the counterpart of
// PanelAccent(), so that a lit panel goes gold all the way through rather than
// gold around the edge with steel-blue seams across the middle of it.
ftxui::Element PanelSeparator(bool highlighted);

// Returns a horizontal separator rule in the theme border color.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_PANEL_UTIL_H_
