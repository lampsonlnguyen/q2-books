#include <cassert>
#include "../platform/q2/Q2Controls.hpp"
using namespace crosspod;
int main() {
  Q2Controls input;
  assert(input.translate({PhysicalKind::WheelStep,-20}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::WheelStep,-8}).action==LogicalAction::Next);
  assert(input.translate({PhysicalKind::WheelStep,28}).action==LogicalAction::Previous);
  Q2Controls faster(parseWheelSensitivity("200"));
  assert(faster.translate({PhysicalKind::WheelStep,-6}).action==LogicalAction::None);
  assert(faster.translate({PhysicalKind::WheelStep,7}).action==LogicalAction::Previous);
  Q2Controls slower(parseWheelSensitivity("10"));
  assert(slower.translate({PhysicalKind::WheelStep,-139}).action==LogicalAction::None);
  assert(slower.translate({PhysicalKind::WheelStep,-1}).action==LogicalAction::Next);
  assert(parseWheelSensitivity("0")==defaultWheelSensitivity);
  assert(parseWheelSensitivity("55")==defaultWheelSensitivity);
  assert(parseWheelSensitivity("100%")==defaultWheelSensitivity);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::Confirm);
  assert(input.translate({PhysicalKind::KeyUp,170}).action==LogicalAction::Back);
  auto t=input.translate({PhysicalKind::TouchDown,0,150,200});
  assert(t.action==LogicalAction::TouchPress&&t.x==150&&t.y==200);
  return 0;
}
