#ifndef MS_SRC_FRONTEND_WIDGETS_PANEL_UTIL_H_
#define MS_SRC_FRONTEND_WIDGETS_PANEL_UTIL_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// True for the "go back" key. Every key the player has bound to Cancel
// arrives here as Escape.
inline bool IsBack(const ftxui::Event& e) {
  return e == ftxui::Event::Escape;
}

// True for the "confirm / advance" key, reached the same way.
inline bool IsForward(const ftxui::Event& e) {
  return e == ftxui::Event::Return;
}

// Wraps `child` so it always reports itself focusable, forwarding rendering
// and events untouched.
//
// Container::Tab drops every key when its active child says it is not
// focusable, and an ftxui::Menu says that whenever its list is empty -- taking
// the tab bar above the list deaf with it.
ftxui::Component AlwaysFocusable(ftxui::Component child);

// Wraps a list so its cursor comes out the other end: Up on the first row
// lands on the last, and back. Only the two edges are taken -- the steps
// between them are the one thing ftxui::Menu gets right. `selected` must
// outlive the component, and `count` is asked per keypress because these lists
// gain and lose rows under the cursor.
//
// Wrong tool for a list under a tab bar: there the bar is a stop in the same
// ring, so those panels count it as stop 0 and call StepCursor.
ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count);

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

// Breaks `text` into as few lines as fit `width` columns, balanced so the
// lines come out near enough the same length: "Aquatic Letter" over "Eye
// Accessory" rather than as much as fits and one word left over.
//
// `tail` is what the LAST line leaves free, for a value that sits beside it --
// a rate, a price. `indent` is the margin every line after the first is
// returned with, which is what makes a wrapped name read as one name rather
// than as two rows. A word too long for the line gets one to itself and runs
// over rather than being cut: half a name names nothing.
std::vector<std::string> WrapBalanced(const std::string& text, int width,
                                      int tail = 0, int indent = 0);

// Formats an integer with thousands-separator commas (e.g. 1234567 ->
// "1,234,567"). Handles negatives.
std::string FormatWithCommas(int64_t n);

// Formats a big number short: "5.6M", "500M", "1570M", "2.34B". A unit is
// only taken up once the value reaches two thousand of the one below it, so a
// number keeps the unit a reader can still weigh it in -- 1570M rather than
// 1.57B. The units are M, B, T and Q; anything under two million is written
// out with commas.
std::string FormatCompact(int64_t n);

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

// A list of weapon types as the player reads it: "Dagger", or "Sword / Axe".
// Both hands' versions of one weapon collapse to the bare name, which is how
// the data says "any sword" and not how it should be shown. A pair lands where
// its first half was listed. Empty in, empty out.
std::string FormatWeaponList(const std::vector<EquipType>& types);

// Returns the display name for a set of equipment (e.g. "Frozen Set"), or ""
// for an unnamed one.
std::string FormatEquipSet(EquipSetName set);

// The colour a currency's mark is drawn in. The mark's shape says which tier of
// gear its token belongs to and this says which piece it buys, so the two read
// together without a legend.
ftxui::Color MarkColor(CurrencyColor color);

// The tag a skill row opens with: what the player does with the skill, said
// once at the front of the row instead of being worked out from the name.
// Four columns wide whichever tag it is, so every name after it starts at the
// same place. A kind-less skill gets the blanks rather than a wrong tag.
struct KindTag {
  const char* text;
  ftxui::Color color;
};
constexpr int kSkillTagWidth = 4;
KindTag TagFor(const Skill& skill);

// One advancement's skills out of `catalog`, in the order every page lists
// them: GMS's own skill_order, then settled so nothing waits on a skill listed
// below it. What a skill does has no say -- the wiki does not gather the
// attacks above the passives, and a second rule would only fight skill_order.
//
// The pointers are into `catalog`, which has to outlive them. Empty for an
// unspecified advancement, so a caller may pass one straight through.
std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement);

// The name of an attack-speed stage, "Slower" through "Fastest 3", or "" for
// an unspecified one. The stage number is the proto enum's own value, so a
// caller wanting both can print it beside this.
std::string AttackSpeedName(AttackSpeed speed);

// True for a skill the player casts, attack or otherwise -- everything that
// isn't a passive. It is what the inspect screen titles itself with, and what
// decides whether a skill's damage line is worth showing at all.
bool IsActive(const Skill& skill);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Returns the display name for a job (e.g. "Swordman"), or "Unknown" for a job
// not yet given a name. The full name: use it where the job is being chosen or
// confirmed, and ShortJobName everywhere else.
std::string JobName(Job job);

// The same name shortened where the full one is too wide to live in a column
// -- "I/L Wizard" for the Ice/Lightning Wizard. Every other job is already as
// short as it gets and answers its own name.
//
// This is the default for showing a job. The two places that spell it out in
// full are the advancement picker and the dialog confirming the choice: what
// the player is picking between deserves its whole name, once.
std::string ShortJobName(Job job);

// Returns the short display label for an AP stat field (e.g. "STR"), or "" for
// STAT_FIELD_UNSPECIFIED.
std::string StatFieldName(StatField field);

// How much room an item name gets in a list. Names run past it -- "Fafnir
// Mistilteinn Trace" does -- so a name is cut to this and slides under it
// while its row is selected; see ScrollingWindow.
constexpr int kItemNameWidth = 26;

// The name cell of a list row: `name` cut to kItemNameWidth columns, sliding
// under them while the row is selected. FormatItemEntry opens with exactly
// this, so a caller that wants to colour the name on its own can split the
// rendered row on this cell's length -- the name may hold multibyte
// characters, and its column width says nothing about how many bytes it is.
std::string ItemNameCell(const std::string& name,
                         std::chrono::steady_clock::duration elapsed =
                             std::chrono::steady_clock::duration::zero());

// Formats a single item list entry: name (kItemNameWidth cols), slot (10
// cols), info (padded to 20 cols), and scroll pass/left/restore counts. Pass
// -1 for all three scroll values to render "-" (use for non-upgradeable
// items).
//
// `elapsed` is how long this row has been the selected one, which is what
// slides a name too long for the column. Zero -- the default, and what every
// unselected row passes -- shows the head of the name and holds it there.
std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore,
                            std::chrono::steady_clock::duration elapsed =
                                std::chrono::steady_clock::duration::zero());

// The same row, with the scroll counts read off the item itself. Every list
// that draws equipment uses this one, so no two of them can disagree about
// what an item with no slots shows.
std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info,
                            const EquipPrototype& proto, const Equip& state,
                            std::chrono::steady_clock::duration elapsed =
                                std::chrono::steady_clock::duration::zero());

// A one-row bar filled to frac (clamped to [0, 1]) in `fill`, `label` centred
// over it dark-on-filled and light-on-empty. Pass "" for no label.
//
// Draws pixels rather than using ftxui::gauge, which ignores colour decorators
// and cannot carry a label.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label);
// One label colour the whole way across, so it does not turn over a character
// at a time as the bar moves. Only for a fill dark enough to read against.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color);
// The same bar drawn one row per label line, for a label too long to sit on
// one: filled to the same fraction the whole way down, so it reads as one bar
// with a wrapped name on it rather than as a stack of them.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::vector<std::string>& labels);

// Takes `element` out of the layout: it asks for no room, then draws itself at
// its own size from the parent box's top-left corner. Put it last in a dbox to
// overlay something -- otherwise the dbox stretches to hold the overlay and
// pushes the covered panel's borders out.
//
// The screen is the only bound: an overlay running off the bottom or right
// slides back onto it, and one too big for the screen loses its top-left.
ftxui::Element Floating(ftxui::Element element);

// A modal result screen: the subject over a rule, `body`, a rule, [Continue].
// Scrolling, star forcing and recovering all end on one of these, so the rules
// and the button land in the same place on each.
//
// `accent` colours the border and the rules, so the window says how it went
// before the player reads a word of it: gold for a success worth having, red
// for a destroyed item, steel blue for everything in between.
ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body,
                            ftxui::Color accent = kTheme);

// The one way a panel says it has nothing to show: " (empty)". Use a specific
// reason ("no matching items") only where it tells the player something they
// could not already see. `gutter` lines the row up with the list's cursor
// column.
ftxui::Element EmptyState(const std::string& what, int gutter = 1);

// The key a tab is recorded under once the player has opened it. Written into
// the save: changing it forgets that anybody ever opened that tab, and it
// would go gold again for every player.
inline constexpr char kShopTabKey[] = "shop";

// The advancement tab's key for `stage` (1 = 1st job). One key per stage
// rather than one for the tab: the tab arrives again at every advancement
// threshold, and having seen the first is not having seen the second.
std::string AdvanceTabKey(int stage);

// The equip tab's key for the gear an advancement into `stage` handed over.
// Per stage for the same reason: the 2nd advancement puts an off-hand in the
// bag, and having gone to look at the 1st job's weapon is not having seen it.
std::string EquipGiftTabKey(int stage);

// One chip of a tab bar in the game's one tab style. The active chip goes
// white while its row holds focus and keeps the theme-blue invert otherwise,
// which is how the player tells which bar the arrows are reaching; pass
// row_focused=true for a bar that is the only thing on its screen.
//
// `unseen` draws the label gold: a tab handed over but never opened. It is the
// quiet half of the level-up celebration, and it waits there until it is.
ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen = false);

// One tab, as a bar needs it.
struct TabSpec {
  std::string label;
  bool unseen = false;  // see TabChip
};

// A whole tab bar, cut to `width` columns. Chips past the edge are held back
// behind a mark -- kTabMore -- shown only while there is something that way,
// though its column is reserved either way so the chips do not shuffle as it
// comes and goes. The window follows `active`, which is therefore always drawn.
//
// Prefer this to building a row of TabChips: a bar wide enough to overflow is
// a bar that would otherwise widen the window around it, and which bar that
// will be is not something the widget's author gets to know.
//
// `width` is the columns the bar may use, and should be the width of the rows
// under it -- the content is what a window should be sized by, not its tabs.
// Pass 0 for no limit.
ftxui::Element TabBar(const std::vector<TabSpec>& tabs, int active,
                      bool row_focused, int width);

// A bracketed button in the game's one button style, inverted when focused.
// Every button the player can land on is drawn with this.
ftxui::Element ActionButton(const std::string& label, bool focused);

// The [Continue] button, lit because it is the only thing to press. Every
// one-button dialog draws this rather than its own button.
ftxui::Element ContinueButton();

// A row of two buttons, the doing one and the leaving one, spaced the way the
// game spaces them everywhere it asks a question. `go_enabled` false dims the
// left button, for an answer that cannot be given from where the player
// stands; the right one is never dimmed, since leaving always can.
//
// Both focus flags are asked rather than one: a dialog whose cursor is
// somewhere else entirely -- the amount selector's textbox -- inverts neither.
ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled);

// A one-column scroll bar for a list showing `visible` of its `total` rows,
// starting at row `first_visible`. Drawn with the same half-height glyphs as
// ftxui's own vscroll_indicator, which the bag scrolls with, so the two bars
// look alike. Empty when the whole list fits: there is nothing to indicate.
//
// For a list that keeps its own scroll offset rather than handing one to
// ftxui's yframe. Reach for vscroll_indicator first; this is for the case
// where something else has to know which row is on screen.
ftxui::Element ScrollBar(int total, int first_visible, int visible);

// Where a cursor lands after stepping `delta` places in a ring of `stops`,
// coming out the other end rather than stopping. Every list walks with this --
// if you are writing `std::max(0, sel - 1)`, write this instead.
//
// A list under a tab bar counts the bar as stop 0, which makes "Up off the top
// row goes to the bar" and "Up off the bar goes to the last row" one rule. No
// stops answers 0; a `current` outside the ring is folded back into it.
int StepCursor(int current, int delta, int stops);

// The border colour of a main-screen panel: gold while it is lit to be
// noticed, theme blue every other moment.
ftxui::Color PanelAccent(bool highlighted);

// The content width both celebration cards are held to (the border adds two).
// One constant for both: they land seconds apart at level 10, and a pair that
// differed in size would read as two things rather than one moment. A minimum
// rather than padding, so a card does not breathe as a level gains a digit.
inline constexpr int kCelebrationContentWidth = 21;

// ThemedWindow in a colour of your choosing, for the few things that step out
// of the steel blue to be noticed. Every window is built from this, so a lit
// one differs from an ordinary one in colour and nothing else.
ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused = false);

// Wraps content in a bordered window with the game's steel-blue theme color on
// the border and title. Content foreground is set to white; explicitly colored
// elements (gold stars, gold SF, and so on) and ThemedSeparator override it.
// Pass focused=true to invert the title into a solid chip, marking the panel
// that currently holds focus.
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
