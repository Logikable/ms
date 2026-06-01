#ifndef MS_SRC_FRONTEND_PANEL_TEST_BASE_H_
#define MS_SRC_FRONTEND_PANEL_TEST_BASE_H_

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

class PanelTest : public testing::Test {
 protected:
  static CharacterInstance MakeCharacter(int level = 1) {
    Character proto;
    proto.set_level(level);
    proto.set_job(JOB_BEGINNER);
    return CharacterInstance(std::move(proto));
  }

  static std::string RenderElement(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, element);
    return screen.ToString();
  }

  static std::string RenderComponent(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, component->Render());
    return screen.ToString();
  }

  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_required_level(10);
    sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  }

  CharacterInstance c_ = MakeCharacter();
  EquipPrototype sword_;
  int panel_focus_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_TEST_BASE_H_
