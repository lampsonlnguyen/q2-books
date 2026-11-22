// SPDX-License-Identifier: MIT
// Framebuffer output adapter. CrossPoint renders the page; this adapter only
// publishes CrossPoint's monochrome raster to the LCD. No game renderer.
#pragma once
#include <cstddef>
#include <cstdint>

namespace books {
class Q2Display {
public:
    static constexpr int Width=375,Height=320;
    // frame[i] is a 32-bit ARGB framebuffer; the caller owns it.
    // bw is CrossPoint's row-major 1-bit black/white image, MSB first.
    // Bit 1 is dark ink, bit 0 light paper. Use only after the page changes.
    static bool presentBW(const uint8_t *bw,std::size_t bytes,uint32_t *frame,
                          std::size_t pixels,unsigned warmth,bool night) {
        constexpr std::size_t RowBytes=(Width+7)/8;
        if(!bw||!frame||bytes<RowBytes*Height||pixels<Width*Height)return false;
        if(warmth>100)warmth=100;
        auto lerp=[warmth](unsigned cold,unsigned warm) {
            return (cold*(100-warmth)+warm*warmth)/100;
        };
        const unsigned background[3]={
            night?lerp(12,35):lerp(232,255),
            night?lerp(24,22):lerp(242,220),
            night?lerp(37,18):lerp(253,188)
        };
        const unsigned foreground[3]={
            night?lerp(181,255):lerp(22,76),
            night?lerp(207,212):lerp(38,51),
            night?lerp(230,168):lerp(58,27)
        };
        const uint32_t paper=0xff000000u|(background[0]<<16)|(background[1]<<8)|background[2];
        const uint32_t ink=0xff000000u|(foreground[0]<<16)|(foreground[1]<<8)|foreground[2];
        for(int y=0;y<Height;y++){
            for(int x=0;x<Width;x++){
                const unsigned bit=(bw[std::size_t(y)*RowBytes+unsigned(x)/8u]>>(7-(unsigned(x)&7u)))&1u;
                frame[std::size_t(y)*Width+unsigned(x)]=bit?ink:paper;
            }
        }
        return true;
    }
};
}
