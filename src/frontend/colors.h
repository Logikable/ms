#ifndef MS_SRC_FRONTEND_COLORS_H_
#define MS_SRC_FRONTEND_COLORS_H_

#include "ftxui/dom/elements.hpp"

namespace ms {

// Identity color: borders, separators, panel titles, structural labels.
inline const ftxui::Color kTheme = ftxui::Color::RGB(100, 150, 200);

// Star bar in the inspect panel.
inline const ftxui::Color kYellow = ftxui::Color::RGB(255, 210, 50);
inline const ftxui::Color kGray = ftxui::Color::RGB(100, 100, 100);

// Stat source breakdown in the inspect panel.
inline const ftxui::Color kPurple = ftxui::Color::RGB(173, 163, 255);
inline const ftxui::Color kOrange = ftxui::Color::RGB(255, 198, 50);

// Star Force outcome rates.
inline const ftxui::Color kGreen = ftxui::Color::RGB(100, 175, 100);
inline const ftxui::Color kMutedYellow = ftxui::Color::RGB(185, 155, 70);

// Error / bad outcome: item requirements not met, SF destroy.
inline const ftxui::Color kRed = ftxui::Color::RGB(185, 70, 70);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_COLORS_H_
