#include <cassert>
#include <cstdint>
#include "../platform/q2/Q2Display.hpp"

int main(){
    static uint8_t bw[((crosspod::Q2Display::Width+7)/8)*crosspod::Q2Display::Height]={};
    static uint32_t frame[crosspod::Q2Display::Width*crosspod::Q2Display::Height]={};
    bw[0]=0x80;
    assert(crosspod::Q2Display::presentBW(bw,sizeof bw,frame,sizeof frame/sizeof frame[0],50,false));
    assert(frame[0]!=frame[1]);
    assert((frame[0]>>24)==255);
    assert(crosspod::Q2Display::presentBW(bw,sizeof bw,frame,sizeof frame/sizeof frame[0],95,true));
    assert(frame[0]!=frame[1]);
    assert(!crosspod::Q2Display::presentBW(bw,10,frame,sizeof frame/sizeof frame[0],0,false));
}
