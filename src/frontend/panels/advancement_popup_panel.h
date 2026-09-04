/* The card shown for a few seconds when the player takes a job advancement.
 *
 * The level-up card's sibling, and the same kind of thing: it holds nothing,
 * decides nothing, and the caller decides when it is up. Gold for the same
 * reason -- an advancement is the largest thing that happens to a character,
 * and it should be visible from across the room.
 */
#ifndef MS_SRC_FRONTEND_PANELS_ADVANCEMENT_POPUP_PANEL_H_
#define MS_SRC_FRONTEND_PANELS_ADVANCEMENT_POPUP_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

// The card as a bordered window: the job left behind, an arrow down, and the
// advancement taken. Read top to bottom, so the change is the shape of the
// card rather than something to be worked out from two names side by side.
// The same size as the level-up card, which arrives seconds before it at
// level 10.
//
// `to_stage` is the stage advanced into, which names the 5th: without it the
// card would read "Night Lord" over "Night Lord".
ftxui::Element AdvancementPopupPanel(Job from_job, Job to_job, int to_stage);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANELS_ADVANCEMENT_POPUP_PANEL_H_
