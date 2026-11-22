// SPDX-License-Identifier: MIT
// Q2 control-to-CrossPoint navigation adapter. No Q2 Pod app framework dependencies.
#pragma once
#include <charconv>
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
struct MappedEvent { LogicalAction action{LogicalAction::None};int x{},y{}; };
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
            wheel_accumulator_=total%wheelThreshold_;
            if(total>=wheelThreshold_)return {A::Next};
            if(total<=-wheelThreshold_)return {A::Previous};
            return {};
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
