#include <cassert>
#include <iostream>
#include <string>
#include "Utf8.h"

int main() {
    // These are the original CrossPoint Unicode functions, compiled without Q2 Pod code.
    const std::string nfd="Nguye\xCC\x82\xCC\x83n"; // Nguyễn (NFD)
    assert(utf8ComposeNfc(nfd)==u8"Nguyễn");
    char filename[]="Da\xCC\x80 La\xCC\xA3t";
    utf8ComposeNfcInPlace(filename);
    assert(std::string(filename)==u8"Dà Lạt");
    assert(utf8ComposeNfc(u8"Tiếng Việt: ă â đ ê ô ơ ư")==u8"Tiếng Việt: ă â đ ê ô ơ ư");
    std::cout << "Upstream CrossPoint UTF-8 NFC tests passed\n";
}
