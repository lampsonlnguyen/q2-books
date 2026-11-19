#include <cassert>
#include <cstdint>
#include <string>
#include "TxtToHtml.h"

struct BufferPrint:Print {
    std::string output;
    size_t write(uint8_t b) override {output.push_back(static_cast<char>(b));return 1;}
    size_t write(const uint8_t *b,size_t n) override {
        output.append(reinterpret_cast<const char*>(b),n);
        return n;
    }
};
int main(){
    BufferPrint out;
    const std::string vietnamese=u8"Nguyễn ở Đà Lạt & đọc <sách>\nDòng thứ hai";
    assert(TxtToHtml::stream("Books/Tiếng Việt.txt",vietnamese,out));
    assert(out.output.find("Nguyễn ở Đà Lạt &amp; đọc &lt;sách&gt;")!=std::string::npos);
    assert(out.output.find("Dòng thứ hai")!=std::string::npos);
    assert(out.output.find("<br />")!=std::string::npos);
    assert(out.output.find("Tiếng Việt")!=std::string::npos);
}
