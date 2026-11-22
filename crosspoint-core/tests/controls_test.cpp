#include <cassert>
#include <climits>
#include "../platform/q2/Q2Controls.hpp"
using namespace crosspod;
int main() {
  Q2Controls input;
  assert(input.translate({PhysicalKind::WheelStep,-20}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::WheelStep,-15}).action==LogicalAction::Next);
  assert(input.translate({PhysicalKind::WheelStep,35}).action==LogicalAction::Previous);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::Confirm);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,170}).action==LogicalAction::Back);
  auto t=input.translate({PhysicalKind::TouchDown,0,150,200});
  assert(t.action==LogicalAction::TouchPress&&t.x==150&&t.y==200);

  // A Q2 settings adapter may select a threshold without changing defaults.
  Q2Controls sensitive(10);
  assert(sensitive.translate({PhysicalKind::WheelStep,-9}).action==LogicalAction::None);
  assert(sensitive.translate({PhysicalKind::WheelStep,-1}).action==LogicalAction::Next);
  assert(sensitive.translate({PhysicalKind::WheelStep,-9}).action==LogicalAction::None);
  sensitive.setWheelThreshold(20);
  assert(sensitive.translate({PhysicalKind::WheelStep,-11}).action==LogicalAction::None);
  assert(sensitive.translate({PhysicalKind::WheelStep,-9}).action==LogicalAction::Next);
  sensitive.setWheelThreshold(0); // invalid threshold is ignored
  assert(sensitive.translate({PhysicalKind::WheelStep,20}).action==LogicalAction::Previous);

  // Extreme wheel deltas must not overflow signed integer arithmetic.
  Q2Controls large;
  assert(large.translate({PhysicalKind::WheelStep,INT_MIN}).action==LogicalAction::Next);
  large.setWheelThreshold(35);
  assert(large.translate({PhysicalKind::WheelStep,INT_MAX}).action==LogicalAction::Previous);
  return 0;
}
