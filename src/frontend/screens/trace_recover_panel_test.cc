#include "src/frontend/screens/trace_recover_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The Confirm/Cancel mechanics are covered by confirm_prompt_test; these cover
// what is specific to this panel -- which inventory items it will offer as
// recovery targets, and which one Enter is about to act on.
class TraceRecoverPanelTest : public PanelTest {
 protected:
  // A "Sword" prototype under a distinct name, so a test can put an item in the
  // bag that must NOT be offered as a target for a Sword trace.
  EquipPrototype OtherPrototype() {
    EquipPrototype other = sword_;
    other.set_name("Wand");
    return other;
  }

  // Puts a live Sword at `stars` in the bag and returns its index.
  int AddSword(int stars) {
    Equip state;
    state.set_stars(stars);
    c_.PickUp(std::make_unique<EquipInstance>(sword_, state));
    return c_.inventory().size() - 1;
  }

  // Puts a Sword trace at `stars` in the bag and returns it. The panel holds
  // the pointer, so the item has to outlive the panel -- the inventory owns it.
  const EquipTabItem* AddTrace(int stars) {
    Equip state;
    state.set_stars(stars);
    c_.PickUp(std::make_unique<EquipTrace>(sword_, state));
    return &c_.inventory()[c_.inventory().size() - 1];
  }
};

TEST_F(TraceRecoverPanelTest, OffersOnlyItemsSharingTheTracesPrototype) {
  int sword_index = AddSword(0);
  c_.PickUp(std::make_unique<EquipInstance>(OtherPrototype()));
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_EQ(panel.selected_index(), sword_index);
}

// A trace is not a recovery target, or a bag holding two traces of the same
// item would offer each as somewhere to put the other.
TEST_F(TraceRecoverPanelTest, DoesNotOfferAnotherTraceAsATarget) {
  const EquipTabItem* trace = AddTrace(17);
  AddTrace(15);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_EQ(panel.selected_index(), -1);
}

TEST_F(TraceRecoverPanelTest, ChipsCarryEachCandidatesStarCount) {
  AddSword(3);
  AddSword(8);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  std::string rendered = RenderElement(panel.RenderTabs());
  EXPECT_NE(rendered.find("3★"), std::string::npos);
  EXPECT_NE(rendered.find("8★"), std::string::npos);
}

TEST_F(TraceRecoverPanelTest, ArrowsMoveBetweenCandidates) {
  int first = AddSword(3);
  int second = AddSword(8);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_EQ(panel.selected_index(), first);
  EXPECT_TRUE(panel.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_EQ(panel.selected_index(), second);
  EXPECT_TRUE(panel.OnEvent(ftxui::Event::ArrowLeft));
  EXPECT_EQ(panel.selected_index(), first);
}

TEST_F(TraceRecoverPanelTest, ArrowsStopAtTheEnds) {
  int only = AddSword(3);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::ArrowLeft));
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_EQ(panel.selected_index(), only);
}

TEST_F(TraceRecoverPanelTest, SaysSoWhenNothingCanBeRecoveredOnto) {
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_NE(RenderElement(panel.RenderTabs()).find("(no matching items)"),
            std::string::npos);
}

// With no target there is nothing to confirm, so Enter must not open the
// prompt -- confirming would recover onto index -1.
TEST_F(TraceRecoverPanelTest, EnterDoesNothingWithNoCandidates) {
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::Return));
  EXPECT_FALSE(panel.IsConfirming());
}

TEST_F(TraceRecoverPanelTest, EnterOpensTheConfirmAndASecondTakesIt) {
  AddSword(0);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_TRUE(panel.OnEvent(ftxui::Event::Return));
  EXPECT_TRUE(panel.IsConfirming());
  EXPECT_FALSE(panel.TakeConfirmed());

  EXPECT_TRUE(panel.OnEvent(ftxui::Event::Return));
  EXPECT_FALSE(panel.IsConfirming());
  EXPECT_TRUE(panel.TakeConfirmed());
  // Taking it clears it, so the caller cannot recover twice off one confirm.
  EXPECT_FALSE(panel.TakeConfirmed());
}

TEST_F(TraceRecoverPanelTest, CancellingLeavesNothingConfirmed) {
  AddSword(0);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  panel.OnEvent(ftxui::Event::Return);
  panel.OnEvent(ftxui::Event::ArrowRight);  // Confirm -> Cancel
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_FALSE(panel.IsConfirming());
  EXPECT_FALSE(panel.TakeConfirmed());
}

// The arrows belong to the prompt while it is up, so they must not walk the
// chip row behind it.
TEST_F(TraceRecoverPanelTest, ChipSelectionIsFrozenWhileConfirming) {
  int first = AddSword(3);
  AddSword(8);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  panel.OnEvent(ftxui::Event::Return);
  panel.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.selected_index(), first);
}

// 17 stars recover to 12, per the GMS recovery table.
TEST_F(TraceRecoverPanelTest, PreviewCarriesTheRecoveredStarCount) {
  AddSword(0);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  EXPECT_EQ(panel.PreviewResult().stars(), 12);
}

TEST_F(TraceRecoverPanelTest, SettingANewTraceRestartsTheSelection) {
  int first = AddSword(3);
  AddSword(8);
  const EquipTabItem* trace = AddTrace(17);

  TraceRecoverPanel panel(c_);
  panel.SetTrace(trace);
  panel.OnEvent(ftxui::Event::ArrowRight);
  panel.SetTrace(trace);
  EXPECT_EQ(panel.selected_index(), first);
}

}  // namespace
}  // namespace ms
