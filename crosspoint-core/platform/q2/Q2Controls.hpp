// SPDX-License-Identifier: MIT
// Q2 control-to-CrossPoint navigation adapter. No Q2 Pod app framework dependencies.
#pragma once
#include <charconv>
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace crosspod {
enum class PhysicalKind { WheelStep, KeyDown, KeyUp, TouchDown, TouchMove, TouchUp };
enum class LogicalAction {
    None, Previous, Next, Confirm, Back, Menu,
    TouchPress, TouchMove, TouchRelease
};
struct PhysicalEvent {
    PhysicalKind kind{};
    int value{}; // wheel count, or button code (Q2: 218 confirm, 170 back)
    int x{},y{}; // landscape touch pixel position
};
// repeat: how many times the action happened. A wheel datagram carries all the
// travel Q2 Pod gathered since its last one, which can be several ticks.
struct MappedEvent { LogicalAction action{LogicalAction::None};int x{},y{};int repeat{1}; };
inline constexpr int maxWheelStepsPerEvent=64; // a bound for absurd deltas, not a real turn
inline constexpr int defaultWheelSensitivity=50;

inline int parseWheelSensitivity(std::string_view value) {
    if(value.empty() || (value.size()>1 && value.front()=='0'))return defaultWheelSensitivity;
    int percent=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),percent);
    return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() &&
           percent>=10 && percent<=200 && percent%10==0
           ?percent:defaultWheelSensitivity;
}

class Q2Controls {
    static constexpr int wheelFine=16;
    int64_t wheel_accumulator_=0;
    int64_t wheelThreshold_;
    bool confirm_down_=false;
public:
    explicit Q2Controls(int percent=defaultWheelSensitivity) {
        if(percent<10 || percent>200 || percent%10!=0)percent=defaultWheelSensitivity;
        wheelThreshold_=(28*wheelFine*defaultWheelSensitivity+percent/2)/percent;
    }
    MappedEvent translate(const PhysicalEvent& event) {
        using K=PhysicalKind;
        using A=LogicalAction;
        switch(event.kind) {
        case K::WheelStep: {
            // Q2 Pod uses 28 raw units per tick at 50%, with 1/16-unit precision.
            const int64_t travel=-static_cast<int64_t>(event.value)*wheelFine;
            if((travel<0 && wheel_accumulator_>0) || (travel>0 && wheel_accumulator_<0))
                wheel_accumulator_=0;
            const int64_t total=wheel_accumulator_+travel;
            // Every tick in the datagram counts, the remainder carries on, as Q2 Pod's
            // own wheel_sample does: one step per datagram lost a fast turn's ticks.
            const int64_t steps=total/wheelThreshold_;
            wheel_accumulator_=total%wheelThreshold_;
            if(steps==0)return {};
            const int count=static_cast<int>(std::min<int64_t>(
                steps<0?-steps:steps,maxWheelStepsPerEvent));
            return {steps>0?A::Next:A::Previous,0,0,count};
        }
        case K::KeyDown:
            if(event.value==218){
                if(confirm_down_)return {};
                confirm_down_=true;
            }
            return {};
        case K::KeyUp:
            if(event.value==218){if(!confirm_down_)return {};confirm_down_=false;return {A::Confirm};}
            if(event.value==170)return {A::Back};
            if(event.value==172||event.value==222)return {A::Previous};
            if(event.value==173||event.value==223)return {A::Next};
            return {};
        case K::TouchDown:return {A::TouchPress,event.x,event.y};
        case K::TouchMove:return {A::TouchMove,event.x,event.y};
        case K::TouchUp:return {A::TouchRelease,event.x,event.y};
        }
        return {};
    }
};
}
