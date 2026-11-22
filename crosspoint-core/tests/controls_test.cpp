#include <cassert>
#include <climits>
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
  assert(parseWheelSensitivity("050")==defaultWheelSensitivity);
  assert(parseWheelSensitivity("40")==40);
  assert(parseWheelSensitivity("200")==200);
  assert(parseWheelSensitivity("10")==10);
  // Reverse motion discards the prior partial detent.
  Q2Controls reverse;
  assert(reverse.translate({PhysicalKind::WheelStep,-20}).action==LogicalAction::None);
  assert(reverse.translate({PhysicalKind::WheelStep,15}).action==LogicalAction::None);
  assert(reverse.translate({PhysicalKind::WheelStep,13}).action==LogicalAction::Previous);
  // Overflow and duplicate key presses must not affect navigation.
  Q2Controls extreme;
  assert(extreme.translate({PhysicalKind::WheelStep,INT_MIN}).action==LogicalAction::Next);
  assert(extreme.translate({PhysicalKind::WheelStep,INT_MAX}).action==LogicalAction::Previous);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyDown,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::Confirm);
  assert(input.translate({PhysicalKind::KeyUp,218}).action==LogicalAction::None);
  assert(input.translate({PhysicalKind::KeyUp,170}).action==LogicalAction::Back);
  auto t=input.translate({PhysicalKind::TouchDown,0,150,200});
  assert(t.action==LogicalAction::TouchPress&&t.x==150&&t.y==200);
  // One datagram carries all the travel since the last: a full turn (200 units) at
  // 50% is 7 ticks, a fast half turn 3, and the remainder carries on.
  Q2Controls turn;
  auto full=turn.translate({PhysicalKind::WheelStep,-200});
  assert(full.action==LogicalAction::Next && full.repeat==7);
  auto rest=turn.translate({PhysicalKind::WheelStep,-4}); // 200-196=4 left, +4 = 8 < 28
  assert(rest.action==LogicalAction::None);
  auto half=turn.translate({PhysicalKind::WheelStep,-100});
  assert(half.action==LogicalAction::Next && half.repeat==3);  // 8+100=108 = 3*28+24
  Q2Controls fast200(parseWheelSensitivity("200"));             // 7 units a tick
  auto packet=fast200.translate({PhysicalKind::WheelStep,28});
  assert(packet.action==LogicalAction::Previous && packet.repeat==4);
  Q2Controls fast100(parseWheelSensitivity("100"));
  int moved=0;
  for(int i=0;i<3;++i){auto m=fast100.translate({PhysicalKind::WheelStep,-28});moved+=m.repeat*(m.action==LogicalAction::Next);}
  assert(moved==6);  // was 3: 100% behaved as 50% at 28-unit packets
  // An absurd delta is bounded, not a runaway.
  Q2Controls absurd;
  auto huge=absurd.translate({PhysicalKind::WheelStep,-2000000});
  assert(huge.action==LogicalAction::Next && huge.repeat==maxWheelStepsPerEvent);
  // Keys and touches are single events.
  assert(Q2Controls{}.translate({PhysicalKind::KeyUp,170}).repeat==1);
  return 0;
}
