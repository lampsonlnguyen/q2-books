#include <cassert>
#include "../platform/q2/Q2Controls.hpp"
using namespace crosspod;
int main() {
  Q2Controls input;
  assert(input.translate({PhysicalKind::WheelStep,-20}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::WheelStep,-15}).action==LogicalAction::Next);
  assert(input.translate({PhysicalKind::WheelStep,35}).action==LogicalAction::Previous);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::Confirm);
  assert(input.translate({PhysicalKind::KeyUp,170}).action==LogicalAction::Back);
  auto t=input.translate({PhysicalKind::TouchDown,0,150,200});
  assert(t.action==LogicalAction::TouchPress&&t.x==150&&t.y==200);
  return 0;
}
