/* The furniture every panel is drawn out of.
 *
 * Windows, dialogs, buttons, tab bars, progress bars, scroll bars and the
 * separators between them -- the shapes that make one screen look like the
 * next. A caller hands in text and gets back an ftxui::Element.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_CHROME_H_
#define MS_SRC_FRONTEND_WIDGETS_CHROME_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// The colour a currency's mark is drawn in. The SHAPE says which tier its
// token belongs to and the colour which piece it buys.
ftxui::Color MarkColor(CurrencyColor color);

// The colour an Inner Ability line of `rank` is written in. The rank reaches
// the TEXT, so the lock beside it stays plain.
ftxui::Color RarityColor(AbilityRank rank);
// The same four colours for a potential's rank, which shares the ladder.
ftxui::Color RarityColor(PotentialRank rank);

// A one-row bar filled to `frac` in `fill`, `label` centred over it
// dark-on-filled and light-on-empty. Draws pixels rather than ftxui::gauge,
// which ignores colour decorators and cannot carry a label.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label);
// One label colour the whole way across, so it does not turn over a character
// at a time as the bar moves. Only for a fill dark enough to read against.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color);
// The same bar one row per label line, filled to the same fraction all the way
// down so it reads as one bar with a wrapped name.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::vector<std::string>& labels);

// Takes `element` out of the layout: it asks for no room and draws at its own
// size from the parent box's top-left. Put it LAST in a dbox, or the dbox
// stretches to hold it and pushes the covered borders out. The screen is the
// only bound -- an overlay running off it slides back on.
ftxui::Element Floating(ftxui::Element element);

// ftxui's clear_under, and the half glyph it leaves standing beside itself: a
// two-column glyph whose left half sits outside the overlay keeps both columns
// when printed, and the border beside it is never drawn. The leftover half is
// blanked.
//
// WRAP EVERY OVERLAY in this rather than bare clear_under: which row a
// floating window lands beside is not something its author knows.
ftxui::Element ClearUnder(ftxui::Element element);

// A modal result screen: the subject over a rule, `body`, a rule, [Continue].
// Every upgrade ends on one, so the rules and the button land alike. `accent`
// colours the border and rules, saying how it went before a word is read:
// gold for a success, red for a destroyed item, steel blue between.
ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body,
                            ftxui::Color accent = kTheme);

// A modal question: `body` over a rule, then `buttons`. The counterpart of
// ResultWindow, and what every asking dialog is built from, so none can be
// written without the rule over its answer. `accent` colours both.
ftxui::Element DialogWindow(const std::string& title,
                            std::vector<ftxui::Element> body,
                            ftxui::Element buttons,
                            ftxui::Color accent = kTheme);

/* The three marks that say where the cursor is, and which one a thing gets.
 *
 * **The caret "> " marks the cursor in every list walked with Up and Down.**
 * It is the mark; the other two are decoration on top of it. A dropdown -- the
 * item menu, the menu panel's box -- wears the caret and nothing else.
 *
 * **ftxui::inverted is the default highlight**, and goes on the ONE cell the
 * cursor acts on: a button, a tab chip, a [+], a lock, an option's name, a row
 * that is a single label. Plain text only. Inverting swaps foreground for
 * background, so a coloured cell comes back as a coloured block and a progress
 * bar comes back reading as the opposite value.
 *
 * **HighlightRow's band is for a table row** wide enough that a mark on one
 * cell leaves the rest of it unclaimed -- an item row with stats eight columns
 * out, a name with a price at the far side. It is also the fallback wherever
 * invert cannot go: a coloured cell, a bar, a centred row with nowhere to put
 * a caret.
 *
 * The cursor OUTRANKS dim, which says a row's action is shut (see colors.h):
 * the two are written as an if/else, never stacked, because a dimmed
 * highlight says neither thing.
 */

// `row` with the cursor's band behind it when `on_cursor`.
//
// Pass the WHOLE row, affixes and all, or a column reads as not part of it.
// Gate it on the test the caret uses: a band on an unfocused list claims a
// selection the arrows would not move.
ftxui::Element HighlightRow(ftxui::Element row, bool on_cursor);

// The box a switch is drawn as, thrown and not. Every screen with a switch on
// it draws this one, so the mark never depends on which screen you are on.
inline constexpr char kCheckedBox[] = "[✓]";
inline constexpr char kUncheckedBox[] = "[ ]";

// The one way a panel says it has nothing to show: " (empty)". A specific
// reason only where it tells the player something they cannot see.
ftxui::Element EmptyState(const std::string& what, int gutter = 1);

// The key a tab is recorded under once opened. WRITTEN INTO THE SAVE: change
// it and every player's tab goes gold again.
inline constexpr char kShopTabKey[] = "shop";

// And the Hyper tab's, which goes gold the level it arrives and stays that
// way until the player opens it.
inline constexpr char kHyperTabKey[] = "hyper";

// And the Ability tab's. One key for the ACCOUNT: a player is told what Inner
// Ability is the first time one of theirs reaches it.
inline constexpr char kAbilityTabKey[] = "ability";

// And the Buffs tab's, on the same terms. The key still reads "pots" because
// the saves do: renaming the tab must not forget who has seen it.
inline constexpr char kBuffsTabKey[] = "pots";

// The advancement tab's key for `stage`. One per STAGE: the tab arrives again
// at every threshold, and having seen the first is not having seen the
// second.
std::string AdvanceTabKey(int stage);

// The equip tab's key for the gear an advancement into `stage` handed over.
// Per stage for the same reason.
std::string EquipGiftTabKey(int stage);

// One chip of a tab bar in the game's one tab style. The active chip goes
// white while its row holds focus and keeps the theme invert otherwise, which
// is how the player tells which bar the arrows reach. `unseen` draws the label
// gold: a tab handed over but never opened.
ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen = false);

// One tab, as a bar needs it.
struct TabSpec {
  std::string label;
  bool unseen = false;  // see TabChip
};

// A whole tab bar, cut to `width` columns. Chips past the edge are held behind
// a mark shown only while there is something that way, and the window follows
// `active`. Pass -1 for a bar whose cursor has stepped off it, where a second
// highlight would put the selection in two places.
//
// The right mark keeps a column either way so the chips do not shuffle; the
// left stands in the leading chip's pad, so two stacked bars line up.
//
// PREFER THIS to a row of TabChip calls: a bar wide enough to overflow would
// otherwise widen the window, and which bar that is is not the author's to
// know. `width` should be the width of the ROWS under it.
ftxui::Element TabBar(const std::vector<TabSpec>& tabs, int active,
                      bool row_focused, int width);

// A bracketed button in the game's one button style, inverted when focused.
// Every button the player can land on is drawn with this.
ftxui::Element ActionButton(const std::string& label, bool focused);

// The one-button dialog's button, lit because it is the only thing to press.
// Every such dialog draws this rather than its own.
ftxui::Element ContinueButton(const std::string& label = "Continue");

// The doing button and the leaving one, spaced as the game spaces them
// everywhere. `go_enabled` false dims the left; the right never dims, leaving
// always being possible. BOTH focus flags are asked: a dialog whose cursor is
// elsewhere -- in a textbox -- inverts neither.
ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled);

// The first row on screen for a list of `total` showing `visible` with
// `selected` under the cursor. THE scroll rule: the selection sits in the
// middle of the window, clamped at both ends, so the head and foot of a list
// are shown whole. The arithmetic yframe does, for lists that keep their own
// offset -- anything else calls this rather than inventing a second rule.
int ScrollWindowStart(int total, int selected, int visible);

// A one-column scroll bar for a list showing `visible` of `total` from row
// `first_visible`, in the same half-height glyphs as ftxui's own
// vscroll_indicator so the two look alike. Empty when the list fits.
//
// REACH FOR vscroll_indicator FIRST; this is for a list that must know which
// row is on screen.
ftxui::Element ScrollBar(int total, int first_visible, int visible);

// The same bar a row at a time, for a panel laying each row out itself --
// one whose full-width separators would stop short of the bar's column.
std::vector<ftxui::Element> ScrollBarCells(int total, int first_visible,
                                           int visible);

// The border colour of a main-screen panel: gold while it is lit to be
// noticed, theme blue every other moment.
ftxui::Color PanelAccent(bool highlighted);

// The content width both celebration cards are held to. ONE constant: they
// land seconds apart at level 10, and two sizes would read as two moments. A
// minimum rather than padding, so a card does not breathe.
inline constexpr int kCelebrationContentWidth = 21;

// How long the focused title chip is held lit, and then dark. It says where
// the keys are going, so it reads as a heartbeat rather than a flicker.
constexpr std::chrono::milliseconds kTitleBlinkHalf(600);

// Whether a focused title's chip is lit at `now`. The phase comes off the
// CLOCK, so every window blinks on one beat and Tab does not restart it.
bool TitleChipLit(std::chrono::steady_clock::time_point now);

// Which end of its border a window's title sits at. kRight is for a panel read
// from that side -- the other player's half of a trade -- and pads the title
// out with the border's own rule, so only the name is lit when the window has
// focus.
enum class TitleAlign {
  kLeft,
  kRight,
};

// ThemedWindow in a colour of your choosing, for the few things that step out
// of the steel blue. Every window is built from this, so a lit one differs in
// colour and nothing else. `blink` is the player's Options setting, which only
// the five main-screen panels pass; `now` is its clock, for tests.
ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused = false,
                            bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now(),
                            TitleAlign align = TitleAlign::kLeft);

// Content in a bordered window in the game's steel blue, foreground white --
// explicitly coloured elements and ThemedSeparator override it. `focused`
// lights the title into a solid chip; `blink` pulses that chip instead.
ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused = false, bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now(),
                            TitleAlign align = TitleAlign::kLeft);

// Centres a row with a column of clearance each side. EVERY centred row goes
// through this rather than bare hcenter, so the longest line on a screen --
// whichever it turns out to be -- keeps its distance from the border.
ftxui::Element CenteredRow(ftxui::Element row);
ftxui::Element CenteredRow(const std::string& text);

// Returns a horizontal separator rule in `accent`, for a rule inside an
// AccentWindow: a steel-blue rule across a gold card reads as a seam.
ftxui::Element AccentSeparator(ftxui::Color accent);

// The rule inside a main-screen panel, the counterpart of PanelAccent(): a lit
// panel goes gold all the way through rather than only at its edge.
ftxui::Element PanelSeparator(bool highlighted);

// `cell` in red unless `ok`, for the one value the player falls short of. Red
// is the REASON (see colors.h), so it goes on the cell carrying it rather than
// the row: `RedUnless(text(price), Affordable())`.
ftxui::Element RedUnless(ftxui::Element cell, bool ok);

// The purse over the price, right-aligned in one column. EVERY screen that
// charges draws this, so the pair reads alike wherever it is asked, and
// `affordable` reddens a price that cannot be met. The column is never
// narrower than a hundred billion meso, so it does not shrink as they spend.
ftxui::Element PriceBlock(int64_t held, int64_t cost, bool affordable);

// The same pair over a currency that is not meso, both numbers already written
// out. `min_width` keeps the column from shrinking as it is spent.
ftxui::Element PriceBlock(const std::string& held, const std::string& cost,
                          bool affordable, int min_width = 0);

// Returns a horizontal separator rule in the theme border color.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CHROME_H_
