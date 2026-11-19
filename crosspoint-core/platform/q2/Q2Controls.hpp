// SPDX-License-Identifier: MIT
// Q2 control-to-CrossPoint navigation adapter. No Q2 Pod app framework dependencies.
#pragma once
#include <cstdint>

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
class Q2Controls {
    int wheel_accumulator_=0;
    bool confirm_down_=false;
public:
    MappedEvent translate(const PhysicalEvent& event) {
        using K=PhysicalKind;
        using A=LogicalAction;
        switch(event.kind) {
        case K::WheelStep:
            // Q2 wheel turns in 200 raw increments/revolution.
            // Do not interpret wheel as repeated keypresses until threshold reached.
            wheel_accumulator_ -= event.value;
            if(wheel_accumulator_>=35){wheel_accumulator_-=35;return {A::Next};}
            if(wheel_accumulator_<=-35){wheel_accumulator_+=35;return {A::Previous};}
            return {};
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
