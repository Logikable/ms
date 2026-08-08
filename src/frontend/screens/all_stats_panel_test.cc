#include "src/frontend/screens/all_stats_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

class AllStatsPanelTest : public PanelTest {
 protected:
  CharacterInstance MakeWarrior() {
    Character proto;
    proto.set_level(15);
    proto.set_job(JOB_SWORDMAN);
    proto.set_job_stage(1);
    proto.mutable_allocated_stats()->set_str(40);
    proto.mutable_allocated_stats()->set_hp(500);
    return CharacterInstance(rng_, std::move(proto));
  }

  // The panel's rows as plain characters, one per column, read off the screen
  // grid -- the borders are box-drawing, so byte offsets do not line up.
  static std::vector<std::string> Rows(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                                 ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, element);
    std::vector<std::string> rows;
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 1; x < AllStatsPanel::kTotalWidth - 1; ++x) {
        const std::string& cell = screen.PixelAt(x, y).character;
        row += cell.empty() ? " " : cell;
      }
      rows.push_back(row);
    }
    return rows;
  }

  // The row holding `label` in its left column, or "" if no row does.
  static std::string RowWith(ftxui::Element element, const std::string& label) {
    for (const std::string& row : Rows(std::move(element))) {
      if (row.compare(1, label.size(), label) == 0) {
        return row;
      }
    }
    return "";
  }
};

TEST_F(AllStatsPanelTest, PairsTheStatsTwoToARow) {
  CharacterInstance c = MakeWarrior();
  AllStatsPanel panel(c, {});
  // The pairings the screen is laid out for. Reading the left label and
  // finding the right one on the same row is the whole assertion.
  EXPECT_NE(RowWith(panel.Render(), "HP").find("MP"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "STR").find("INT"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "DEX").find("LUK"), std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Attack ").find("Magic Attack"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Damage").find("Final Damage"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Critical Rate").find("Critical Damage"),
            std::string::npos);
  EXPECT_NE(RowWith(panel.Render(), "Attack Speed").find("Defense"),
            std::string::npos);
}

TEST_F(AllStatsPanelTest, ShowsTheHeadingAndNothingSpendable) {
  Character proto;
  proto.set_level(60);
  proto.set_job(JOB_ICE_LIGHTNING_WIZARD);
  proto.set_job_stage(2);
  proto.set_ap(5);
  CharacterInstance c(rng_, std::move(proto));

  AllStatsPanel panel(c, {});
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Lv 60 I/L Wizard"), std::string::npos);
  EXPECT_NE(rendered.find("Combat Power"), std::string::npos);
  // This screen is for reading: no AP to spend and nothing to spend it on.
  EXPECT_EQ(rendered.find("AP"), std::string::npos);
  EXPECT_EQ(rendered.find("[+]"), std::string::npos);
}

TEST_F(AllStatsPanelTest, AnAddedToStatCarriesItsBreakdown) {
  CharacterInstance c = MakeWarrior();
  sword_.mutable_base_stats()->set_str(5);
  c.PickUp(std::make_unique<EquipInstance>(sword_));
  c.Equip(0);

  AllStatsPanel panel(c, {});
  EXPECT_NE(RenderElement(panel.Render()).find("(40+5) 45"), std::string::npos);
}

}  // namespace
}  // namespace ms
