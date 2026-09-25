/* The pieces every panel is built from.
 *
 * Windows, dialogs, buttons, tab bars, progress bars, scroll bars and dividers:
 * the shapes that make every screen look consistent. A caller passes in text
 * and gets back an ftxui::Element.
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

// The colour a currency's mark is drawn in. The mark's shape says which tier
// its token belongs to, and the colour says which piece it buys.
ftxui::Color MarkColor(CurrencyColor color);

// The text colour for an Inner Ability line of `rank`. Only the text is
// coloured; the lock beside it stays plain.
ftxui::Color RarityColor(AbilityRank rank);
// The same four colours for a potential's rank, which uses the same ranks.
ftxui::Color RarityColor(PotentialRank rank);

// A one-row bar filled to `frac` in `fill`, with `label` centred over it, dark
// on the filled part and light on the empty part. Draws pixels rather than
// using ftxui::gauge, which ignores colour decorators and can't show a label.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label);
// The label is one colour all the way across, so it doesn't change a character
// at a time as the bar moves. Only for a fill dark enough to read against.
// `buff_count` puts dots at the bar's ends for active buffs; see BuffDots.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color,
                           int buff_count = 0);
// The same bar with one row per label line, all filled to the same fraction so
// it looks like one bar with a wrapped name.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::vector<std::string>& labels,
                           int buff_count = 0);

// Takes `element` out of the layout: it takes no space and draws at its own
// size from the parent box's top-left. Put it last in a dbox, or the dbox
// stretches to fit it and pushes out the borders it covers. Only the screen
// limits it: an overlay running off the screen is moved back on.
ftxui::Element Floating(ftxui::Element element);

// ftxui's clear_under, plus a fix for the half glyph it leaves beside itself: a
// two-column glyph whose left half is outside the overlay keeps both columns
// when printed, so the border beside it is never drawn. The leftover half is
// blanked.
//
// Wrap every overlay in this rather than bare clear_under, since the author
// can't know which row a floating window will land next to.
ftxui::Element ClearUnder(ftxui::Element element);

// A result dialog: the subject above a divider, `body`, another divider, and
// [Continue]. Every upgrade ends with one, so the dividers and button look the
// same. `accent` colours the border and dividers to show the outcome before a
// word is read: gold for success, red for a destroyed item, steel blue
// otherwise.
ftxui::Element ResultWindow(const std::string& title,
                            const std::string& subject,
                            std::vector<ftxui::Element> body,
                            ftxui::Color accent = kTheme);

// A question dialog: `body` above a divider, then `buttons`. The counterpart of
// ResultWindow, used by every dialog that asks something, so none can omit the
// divider above its answer. `accent` colours both.
ftxui::Element DialogWindow(const std::string& title,
                            std::vector<ftxui::Element> body,
                            ftxui::Element buttons,
                            ftxui::Color accent = kTheme);

/* The three cursor marks, and which to use where.
 *
 * The caret "> " marks the cursor in every list navigated with Up and Down. It
 * is the main mark; the other two are extra decoration. A dropdown (the item
 * menu, the menu panel's box) uses only the caret.
 *
 * ftxui::inverted is the default highlight, and goes on the one cell the cursor
 * acts on: a button, a tab, a [+], a lock, an option's name, or a row that is a
 * single label. Only on plain text: inverting swaps foreground and background,
 * so a coloured cell becomes a coloured block and a progress bar reads as the
 * opposite value.
 *
 * HighlightRow's band is for a table row wide enough that marking one cell
 * leaves the rest unclaimed, like an item row with stats eight columns out, or
 * a name with a price at the far side. It is also the fallback wherever
 * inverting won't work: a coloured cell, a bar, or a centred row with no room
 * for a caret.
 *
 * The cursor takes priority over dimming, which says a row's action is
 * unavailable (see colors.h). The two are written as if/else, never combined,
 * because a dimmed highlight says neither thing.
 */

// `row` with the cursor's band behind it when `on_cursor`.
//
// Pass the whole row, prefixes and suffixes included, or a column looks like it
// isn't part of it. Use the same condition as the caret: a band on an unfocused
// list suggests a selection the arrows wouldn't move.
ftxui::Element HighlightRow(ftxui::Element row, bool on_cursor);

// The checkbox drawn for a switch, on and off. Every screen with a switch uses
// this, so it looks the same everywhere.
inline constexpr char kCheckedBox[] = "[✓]";
inline constexpr char kUncheckedBox[] = "[ ]";

// The standard way a panel says it has nothing to show: " (empty)". Use a
// specific reason only when it tells the player something they can't see.
ftxui::Element EmptyState(const std::string& what, int gutter = 1);

// The key a tab is recorded under once opened. It is stored in the save, so
// changing it makes every player's tab gold again.
inline constexpr char kShopTabKey[] = "shop";

// The Hyper tab's key. The tab is gold from the level it unlocks until the
// player opens it.
inline constexpr char kHyperTabKey[] = "hyper";

// The Ability tab's key. It is per account: a player is told about Inner
// Ability the first time any of their characters reaches it.
inline constexpr char kAbilityTabKey[] = "ability";

// The Buffs tab's key, on the same terms. It is still "pots" because saves use
// that; renaming the tab must not forget who has seen it.
inline constexpr char kBuffsTabKey[] = "pots";

// The Advance tab's key for `stage`. One per stage, because the tab reappears
// at every advancement level, and seeing the first isn't seeing the second.
std::string AdvanceTabKey(int stage);

// The equip tab's key for the gear given by an advancement into `stage`. Per
// stage for the same reason.
std::string EquipGiftTabKey(int stage);

// One tab of a tab bar in the game's standard tab style. The active tab is
// white while its row has focus and theme-inverted otherwise, which shows the
// player which bar the arrows control. `unseen` draws the label gold: a tab
// that is unlocked but not yet opened.
ftxui::Element TabChip(const std::string& label, bool active, bool row_focused,
                       bool unseen = false);

// One tab, as a bar needs it.
struct TabSpec {
  std::string label;
  bool unseen = false;  // see TabChip
};

// A whole tab bar, limited to `width` columns. Tabs past the edge are hidden
// behind a mark that only shows when there is something that way, and the view
// follows `active`. Pass -1 for a bar the cursor has left, where a second
// highlight would show the selection in two places.
//
// The right mark always keeps its column so the tabs don't shift; the left one
// uses the first tab's padding, so two stacked bars line up.
//
// Use this instead of a row of TabChip calls: a bar wide enough to overflow
// would otherwise widen the window, and the author can't know which bars will.
// `width` should be the width of the rows below it.
ftxui::Element TabBar(const std::vector<TabSpec>& tabs, int active,
                      bool row_focused, int width);

// A bracketed button in the game's standard style, inverted when focused. Every
// button the player can select is drawn with this.
ftxui::Element ActionButton(const std::string& label, bool focused);

// A one-button dialog's button, always highlighted because it is the only thing
// to press. Every such dialog uses this.
ftxui::Element ContinueButton(const std::string& label = "Continue");

// The action button and the leave button, spaced the standard way. `go_enabled`
// false dims the left one; the right never dims, since leaving is always
// possible. Both focus flags are needed: a dialog whose cursor is elsewhere (in
// a textbox) highlights neither.
ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled);

// The first visible row of a list of `total` showing `visible` rows with
// `selected` under the cursor. This is the standard scroll rule: the selection
// stays in the middle of the view, clamped at both ends so the start and end of
// a list are shown fully. It is the arithmetic yframe uses, for lists that
// track their own offset; use this rather than writing a different rule.
int ScrollWindowStart(int total, int selected, int visible);

// A one-column scroll bar for a list showing `visible` of `total` rows from
// `first_visible`, using the same half-height glyphs as ftxui's
// vscroll_indicator so they look alike. Empty when the list fits.
//
// Use vscroll_indicator first; this is for a list that needs to know which rows
// are on screen.
ftxui::Element ScrollBar(int total, int first_visible, int visible);

// The same bar one row at a time, for a panel laying out each row itself, where
// full-width dividers would stop short of the bar's column.
std::vector<ftxui::Element> ScrollBarCells(int total, int first_visible,
                                           int visible);

// The border colour of a main-screen panel: gold while it is lit, theme blue
// otherwise.
ftxui::Color PanelAccent(bool highlighted);

// The content width both celebration cards use. One constant, because they
// appear seconds apart at level 10 and two sizes would look like two separate
// events. A minimum rather than padding, so the card doesn't change size.
inline constexpr int kCelebrationContentWidth = 21;

// How long the focused title tab stays lit, then dark. It shows where keys are
// going, so it should feel like a slow pulse rather than a flicker.
constexpr std::chrono::milliseconds kTitleBlinkHalf(600);

// Whether a focused title's tab is lit at `now`. The phase comes from the
// clock, so every window blinks in sync and Tab doesn't restart it.
bool TitleChipLit(std::chrono::steady_clock::time_point now);

// Which end of the border a window's title is at. kRight is for a panel read
// from that side (the other player's half of a trade), and pads the title with
// the border line so only the name lights up when the window has focus.
enum class TitleAlign {
  kLeft,
  kRight,
};

// ThemedWindow in any colour, for the few things that aren't steel blue. Every
// window is built from this, so a lit window differs only in colour. `blink` is
// the player's Options setting, which only the five main-screen panels pass;
// `now` is its clock, for tests.
ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused = false,
                            bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now(),
                            TitleAlign align = TitleAlign::kLeft);

// Content in a bordered window in the game's steel blue, with white text;
// explicitly coloured elements and ThemedSeparator override it. `focused`
// lights the title as a solid tab; `blink` pulses it instead.
ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused = false, bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now(),
                            TitleAlign align = TitleAlign::kLeft);

// Centres a row with a column of space on each side. Every centred row uses
// this rather than bare hcenter, so the longest line on a screen, whichever it
// is, keeps a gap from the border.
ftxui::Element CenteredRow(ftxui::Element row);
ftxui::Element CenteredRow(const std::string& text);

// A horizontal divider in `accent`, for use inside an AccentWindow: a
// steel-blue line across a gold card looks like a seam.
ftxui::Element AccentSeparator(ftxui::Color accent);

// The divider inside a main-screen panel, the counterpart of PanelAccent(): a
// lit panel is gold throughout, not just at its border.
ftxui::Element PanelSeparator(bool highlighted);

// `cell` in red unless `ok`, for the one value the player falls short of. Red
// marks the reason (see colors.h), so it goes on the cell with that value, not
// the whole row: `RedUnless(text(price), Affordable())`.
ftxui::Element RedUnless(ftxui::Element cell, bool ok);

// The meso held above the price, right-aligned in one column. Every screen that
// charges uses this so the pair looks the same everywhere, and `affordable`
// turns an unaffordable price red. The column is always wide enough for a
// hundred billion meso, so it doesn't shrink as meso is spent.
ftxui::Element PriceBlock(int64_t held, int64_t cost, bool affordable);

// The same pair for a currency other than meso, with both numbers already
// formatted. `min_width` keeps the column from shrinking as it is spent.
ftxui::Element PriceBlock(const std::string& held, const std::string& cost,
                          bool affordable, int min_width = 0);

// Returns a horizontal divider in the theme border colour.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CHROME_H_
