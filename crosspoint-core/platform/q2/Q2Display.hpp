// SPDX-License-Identifier: MIT
// Framebuffer output adapter. CrossPoint renders the page; this adapter only
// publishes CrossPoint's monochrome raster to the LCD. No game renderer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace books {
class Q2Display {
public:
    static constexpr int Width=375,Height=320;
    static constexpr std::size_t RowBytes=(Width+7)/8;

    // ---- Smooth text -------------------------------------------------------
    // Each pixel can carry a coverage (alpha, 0..255 of ink) and the ink or
    // paper state it was drawn with ("expect": 0 none, 1 paper, 2 ink). The
    // coverage shows only while the 1-bit image still has that state, so
    // anything drawn over it later (a menu, a highlight, the progress bar)
    // shows as plain ink or paper.
    //
    // Book pages: CrossPoint fonts store 2-bit coverage (0 white, 1 light,
    // 2 dark, 3 black) and the BW pass paints levels 1-3 as solid ink. The
    // engine's two grayscale passes (made for e-ink) set a bit where a pixel
    // is grey: the MSB plane for levels 1 and 2, the LSB plane for level 2
    // only; solid ink sets neither. So for an ink pixel of the BW page:
    // MSB 0 -> 3, MSB 1 LSB 1 -> 2, MSB 1 LSB 0 -> 1. Images use the same
    // planes for their two greys. Text drawn from TrueType (UiText) writes
    // its full 8-bit coverage directly.
    static constexpr uint8_t kExpectNone=0,kExpectPaper=1,kExpectInk=2;
    static unsigned bitAt(const uint8_t* plane,int x,int y) {
        return (plane[std::size_t(y)*RowBytes+unsigned(x)/8u]>>(7-(unsigned(x)&7u)))&1u;
    }
    static unsigned coverage(unsigned pageInk,unsigned lsb,unsigned msb) {
        if(!pageInk)return 0;
        if(!msb)return 3;
        return lsb?2:1;
    }
    static uint8_t planeAlpha(unsigned pageInk,unsigned lsb,unsigned msb) {
        return static_cast<uint8_t>(coverage(pageInk,lsb,msb)*85u);
    }
    // Final ink share per pixel (Width*Height) for the 1-bit image ink
    // (1 = ink), from the coverage layer where it still applies.
    static void computeAlpha(const uint8_t* ink,const uint8_t* alpha,const uint8_t* expect,
                             uint8_t* out) {
        for(int y=0;y<Height;y++)
            for(int x=0;x<Width;x++){
                const std::size_t i=std::size_t(y)*Width+unsigned(x);
                const unsigned now=bitAt(ink,x,y);
                const uint8_t e=expect[i];
                out[i]=(e!=kExpectNone && now==(e==kExpectInk?1u:0u))?alpha[i]:static_cast<uint8_t>(now*255u);
            }
    }
    // Coverage is linear; an LCD shows sRGB. Blending straight in sRGB makes
    // thin strokes look washed out, worst for light text on a dark page.
    // Each edge share is raised by a gamma: 1.25 for dark text on light,
    // 1.6 for light text on dark (full ink and paper stay exact).
    static uint8_t boost(uint8_t a,bool lightOnDark) {
        struct Table {
            uint8_t v[256];
            explicit Table(double gamma){for(int i=0;i<256;i++)v[i]=static_cast<uint8_t>(255.0*std::pow(i/255.0,1.0/gamma)+0.5);}
        };
        static const Table dark(1.25),light(1.6);
        return (lightOnDark?light:dark).v[a];
    }
    static unsigned luma(uint32_t c){return (((c>>16)&255u)*299u+((c>>8)&255u)*587u+(c&255u)*114u)/1000u;}
    static uint32_t blend(uint32_t paper,uint32_t ink,unsigned alpha) {
        if(alpha==0)return 0xff000000u|paper;
        if(alpha>=255)return 0xff000000u|ink;
        const unsigned a=boost(static_cast<uint8_t>(alpha),luma(ink)>luma(paper));
        auto mix=[a](uint32_t p,uint32_t i,int shift){
            const unsigned pc=(p>>shift)&0xffu,ic=(i>>shift)&0xffu;
            return ((pc*(255u-a)+ic*a+127u)/255u)<<shift;
        };
        return 0xff000000u|mix(paper,ink,16)|mix(paper,ink,8)|mix(paper,ink,0);
    }
    // frame[i] is a 32-bit ARGB framebuffer; the caller owns it.
    // bw is CrossPoint's row-major 1-bit black/white image, MSB first.
    // Bit 1 is dark ink, bit 0 light paper. Use only after the page changes.
    static bool presentBW(const uint8_t *bw,std::size_t bytes,uint32_t *frame,
                          std::size_t pixels,unsigned warmth,bool night,
                          const uint8_t* alpha=nullptr) {
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
                const std::size_t i=std::size_t(y)*Width+unsigned(x);
                frame[i]=blend(paper,ink,alpha?alpha[i]:bitAt(bw,x,y)*255u);
            }
        }
        return true;
    }
    // Q2 Pod theme colours over the same 1-bit image (1 = ink): the base
    // pair everywhere, then each zone in order over its rectangle (rounded
    // corners left to what is under them).
    struct Zone {
        int16_t x,y,w,h;
        uint8_t radius;
        uint32_t ink,paper;
        const uint32_t* paperRows;
        uint8_t rowCount;
    };
    static bool insideRounded(int px,int py,const Zone& z) {
        const int r=z.radius;
        if(r<=0)return true;
        const int lx=px-z.x,ly=py-z.y;
        const int cx=lx<r?r-1:(lx>=z.w-r?z.w-r:lx);
        const int cy=ly<r?r-1:(ly>=z.h-r?z.h-r:ly);
        if(cx==lx||cy==ly)return true;
        const int dx=lx-cx,dy=ly-cy;
        return dx*dx+dy*dy<=r*r;
    }
    static bool presentThemed(const uint8_t *bw,std::size_t bytes,uint32_t *frame,std::size_t pixels,
                              uint32_t baseInk,uint32_t basePaper,const Zone* zones,std::size_t count,
                              const uint8_t* alpha=nullptr) {
        if(!bw||!frame||bytes<RowBytes*Height||pixels<Width*Height)return false;
        auto level=[bw,alpha](int x,int y){
            return alpha?unsigned(alpha[std::size_t(y)*Width+unsigned(x)]):bitAt(bw,x,y)*255u;
        };
        for(int y=0;y<Height;y++)
            for(int x=0;x<Width;x++)
                frame[std::size_t(y)*Width+unsigned(x)]=blend(basePaper,baseInk,level(x,y));
        for(std::size_t i=0;zones&&i<count;i++){
            const Zone& z=zones[i];
            if(z.w<=0||z.h<=0)continue;
            const int y0=z.y<0?0:z.y,y1=z.y+z.h>Height?Height:z.y+z.h;
            const int x0=z.x<0?0:z.x,x1=z.x+z.w>Width?Width:z.x+z.w;
            for(int y=y0;y<y1;y++){
                uint32_t rowPaper=z.paper;
                if(z.paperRows&&z.rowCount)rowPaper=z.paperRows[std::size_t(y-z.y)*z.rowCount/std::size_t(z.h)];
                for(int x=x0;x<x1;x++)
                    if(insideRounded(x,y,z))frame[std::size_t(y)*Width+unsigned(x)]=blend(rowPaper,z.ink,level(x,y));
            }
        }
        return true;
    }
};
}
