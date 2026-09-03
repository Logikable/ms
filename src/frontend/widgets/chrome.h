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

// The colour a currency's mark is drawn in. The mark's shape says which tier of
// gear its token belongs to and this says which piece it buys, so the two read
// together without a legend.
ftxui::Color MarkColor(CurrencyColor color);

// The colour an Inner Ability line of `rank` is written in. The rank reaches
// the text rather than a background, so a row is its rank's colour and the
// lock beside it stays plain.
ftxui::Color RarityColor(AbilityRank rank);
// The same four colours for a potential's rank, which shares the ladder.
ftxui::Color RarityColor(PotentialRank rank);

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

// ftxui's clear_under, and the half glyph it leaves standing beside itself.
//
// A two-column glyph -- the meso mark, an emoji -- whose left half sits just
// outside the overlay keeps both of its columns when the screen is printed,
// and the overlay's border in the cell beside it is never drawn at all. There
// is no drawing half a glyph, so the half left over is blanked.
//
// Every overlay in the game is wrapped in this rather than in bare
// clear_under: which row a floating window lands beside is not something its
// author gets to know.
ftxui::Element ClearUnder(ftxui::Element element);

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

// A modal question: `body` over a rule, then `buttons`. The counterpart of
// ResultWindow -- every dialog that asks something is built from this, so no
// one of them can be written without the rule over its answer.
//
// `accent` colours the border and the rule, for a question the player should
// read before answering.
ftxui::Element DialogWindow(const std::string& title,
                            std::vector<ftxui::Element> body,
                            ftxui::Element buttons,
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

// And the Hyper tab's, which goes gold the level it arrives and stays that
// way until the player opens it.
inline constexpr char kHyperTabKey[] = "hyper";

// And the Ability tab's. One key for the account, not one per character: a
// player is told what Inner Ability is the first time one of theirs reaches
// it.
inline constexpr char kAbilityTabKey[] = "ability";

// And the Pots tab's, for the same reason and on the same terms: what a pot
// is arrives once, and the account is told about it once.
inline constexpr char kPotsTabKey[] = "pots";

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
// behind a mark, shown only while there is something that way. The window
// follows `active`, which is therefore always drawn. Pass -1 for a bar with no
// active chip -- one whose cursor has stepped off it onto something drawn
// beside it, where a second highlight would say the selection is in two
// places.
//
// The right mark keeps a column of its own, reserved either way so the chips
// do not shuffle as it comes and goes; the left one stands in the leading
// chip's own pad, so a bar that scrolls begins in the same column as one that
// fits and two bars stacked in a panel line up.
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

// The one-button dialog's button, lit because it is the only thing to press.
// Every such dialog draws this rather than its own. `label` names it for a
// dialog the player is closing rather than reading on through.
ftxui::Element ContinueButton(const std::string& label = "Continue");

// A row of two buttons, the doing one and the leaving one, spaced the way the
// game spaces them everywhere it asks a question. `go_enabled` false dims the
// left button, for an answer that cannot be given from where the player
// stands; the right one is never dimmed, since leaving always can.
//
// Both focus flags are asked rather than one: a dialog whose cursor is
// somewhere else entirely -- the amount selector's textbox -- inverts neither.
ftxui::Element ButtonRow(const std::string& go, const std::string& leave,
                         bool go_focused, bool leave_focused, bool go_enabled);

// The first row on screen for a list of `total` rows showing `visible` of
// them with `selected` under the cursor. The one scroll rule in the game: the
// selection sits in the middle of the window, clamped at both ends, so the
// head and the foot of a list are shown whole.
//
// This is the arithmetic ftxui's yframe does, written out for the lists that
// keep their own offset. A list handing its scrolling to yframe already
// follows it; anything else calls this rather than inventing a second rule.
int ScrollWindowStart(int total, int selected, int visible);

// A one-column scroll bar for a list showing `visible` of its `total` rows,
// starting at row `first_visible`. Drawn with the same half-height glyphs as
// ftxui's own vscroll_indicator, which the bag scrolls with, so the two bars
// look alike. Empty when the whole list fits: there is nothing to indicate.
//
// For a list that keeps its own scroll offset rather than handing one to
// ftxui's yframe. Reach for vscroll_indicator first; this is for the case
// where something else has to know which row is on screen.
ftxui::Element ScrollBar(int total, int first_visible, int visible);

// The same bar a row at a time, for a panel that has to lay each row out
// itself -- one whose full-width separators would otherwise stop short of the
// bar's column. Empty when the whole list fits, exactly as ScrollBar is.
std::vector<ftxui::Element> ScrollBarCells(int total, int first_visible,
                                           int visible);

// The border colour of a main-screen panel: gold while it is lit to be
// noticed, theme blue every other moment.
ftxui::Color PanelAccent(bool highlighted);

// The content width both celebration cards are held to (the border adds two).
// One constant for both: they land seconds apart at level 10, and a pair that
// differed in size would read as two things rather than one moment. A minimum
// rather than padding, so a card does not breathe as a level gains a digit.
inline constexpr int kCelebrationContentWidth = 21;

// How long the focused panel's title chip is held lit, and then dark. The
// blink is what says where the keys are going, so it is slow enough to read
// as a heartbeat rather than as a flicker.
constexpr std::chrono::milliseconds kTitleBlinkHalf(600);

// Whether a focused title's chip is lit at `now`. The phase comes off the
// clock itself rather than off the moment focus arrived, so every window on
// screen blinks on one beat and Tab does not restart it.
bool TitleChipLit(std::chrono::steady_clock::time_point now);

// ThemedWindow in a colour of your choosing, for the few things that step out
// of the steel blue to be noticed. Every window is built from this, so a lit
// one differs from an ordinary one in colour and nothing else.
//
// `blink` is the player's Options setting, which only the five main-screen
// panels have any reason to pass: a focused title pulses with it and is held
// solid without it. It defaults to off, so a window blinks only where someone
// hands it the preference.
//
// `now` is the blink's clock, which only a test has any reason to hand in.
ftxui::Element AccentWindow(const std::string& title, ftxui::Element content,
                            ftxui::Color accent, bool focused = false,
                            bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now());

// Wraps content in a bordered window with the game's steel-blue theme color on
// the border and title. Content foreground is set to white; explicitly colored
// elements (gold stars, gold SF, and so on) and ThemedSeparator override it.
// Pass focused=true to light the title into a solid chip, marking the panel
// that currently holds focus, and blink=true to pulse that chip instead.
ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused = false, bool blink = false,
                            std::chrono::steady_clock::time_point now =
                                std::chrono::steady_clock::now());

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

// `cell` in red unless `ok`, for the one value the player falls short of: a
// price out of reach, a level not met, an Arcane Force short of the map's.
//
// The rule the colour follows is in colors.h -- red is the REASON, and it goes
// on the cell that carries it rather than on the row around it. This is how it
// is written: `RedUnless(text(price), Affordable())`.
ftxui::Element RedUnless(ftxui::Element cell, bool ok);

// The purse over the price, labelled and right-aligned in one column: what
// the player holds, and then what they are about to spend it on. Every
// screen that charges for something draws this, so the pair reads the same
// wherever it is asked, and `affordable` reddens the price on the one that
// cannot be met.
//
// The column is never narrower than a hundred billion meso, so a window does
// not shrink around the player as they spend. A purse past a trillion widens
// it, there being nothing else to do with a number that long.
ftxui::Element PriceBlock(int64_t held, int64_t cost, bool affordable);

// Returns a horizontal separator rule in the theme border color.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_CHROME_H_
