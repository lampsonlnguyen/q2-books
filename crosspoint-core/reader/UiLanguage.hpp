// SPDX-License-Identifier: MIT
#pragma once
// M3 translated menus. The UI language is saved in <state>/language.conf
// ("BOOKS-LANG1 vi"); English is the default and the source text. tr()
// returns the Vietnamese text for an English UI string, or the string itself
// when there is no translation (book text, names and numbers pass through).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace books {

enum class UiLanguage { English, Vietnamese };

inline std::string uiLanguageFile() {
  const char* env = std::getenv("BOOKS_STATE_DIR");
  return std::string(env && *env ? env : "/mnt/mmc/.books") + "/language.conf";
}
inline UiLanguage& currentUiLanguage() {
  static UiLanguage language = [] {
    UiLanguage l = UiLanguage::English;
    if (FILE* f = std::fopen(uiLanguageFile().c_str(), "rb")) {
      char code[8] = {};
      if (std::fscanf(f, "BOOKS-LANG1 %7s", code) == 1 && std::strcmp(code, "vi") == 0) l = UiLanguage::Vietnamese;
      std::fclose(f);
    }
    return l;
  }();
  return language;
}
inline UiLanguage uiLanguage() { return currentUiLanguage(); }
inline bool setUiLanguage(UiLanguage l) {
  currentUiLanguage() = l;
  const std::string file = uiLanguageFile(), tmp = file + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fprintf(f, "BOOKS-LANG1 %s\n", l == UiLanguage::Vietnamese ? "vi" : "en") > 0;
  ok = std::fclose(f) == 0 && ok;
  if (ok && std::rename(tmp.c_str(), file.c_str()) == 0) return true;
  std::remove(tmp.c_str());
  return false;
}
inline const char* uiLanguageName(UiLanguage l) { return l == UiLanguage::Vietnamese ? "Tiếng Việt" : "English"; }

struct UiTranslation {
  const char* en;
  const char* vi;
};
// Short on purpose: menu rows are about 13 characters wide.
inline constexpr UiTranslation kVietnamese[] = {
    // Reading settings
    {"Reading settings", "Cài đặt đọc"},
    {"Font", "Phông chữ"},
    {"Size", "Cỡ chữ"},
    {"Line spacing", "Giãn dòng"},
    {"Margins", "Lề"},
    {"Alignment", "Căn lề"},
    {"Hyphenation", "Ngắt từ"},
    {"Book CSS", "CSS sách"},
    {"Night mode", "Chế độ đêm"},
    {"Add bookmark", "Thêm dấu"},
    {"Bookmarks", "Dấu trang"},
    {"Chapters", "Mục lục"},
    {"Page links", "Liên kết"},
    {"Return link", "Quay về"},
    {"Select text", "Chọn chữ"},
    {"Sync: upload", "Gửi tiến độ"},
    {"Sync: download", "Lấy tiến độ"},
    {"Focus read", "Đọc nhấn"},
    {"Auto turn", "Tự lật"},
    {"Go to %", "Đi tới %"},
    {"About", "Giới thiệu"},
    {"Dictionaries", "Từ điển"},
    {"Export notes", "Xuất ghi chú"},
    {"Language", "Ngôn ngữ"},
    {"On", "Bật"},
    {"Off", "Tắt"},
    {"Justify", "Đều hai bên"},
    {"Book default", "Theo sách"},
    {"Save page", "Lưu trang"},
    {"Saved pages", "Đã lưu"},
    {"Browse book", "Xem sách"},
    {"Footnotes / links", "Chú thích"},
    {"Go back", "Quay lại"},
    {"No previous link", "Không có"},
    {"Highlight / define", "Tô / tra từ"},
    {"Save position", "Lưu vị trí"},
    {"Use remote position", "Lấy vị trí"},
    {"To Clippings", "Vào Clippings"},
    {"installed", "đã cài"},
    {"Wheel: scroll  Center: select", "Xoay: cuộn  Giữa: chọn"},
    {"Over 2 min idle needs Q2 Pod Smart Awake", "Trên 2 phút cần Smart Awake"},
    {"Reading settings not saved", "Chưa lưu được cài đặt"},
    {"Bookmark saved", "Đã lưu dấu trang"},
    {"Cannot save bookmark", "Không lưu được dấu"},
    {"No highlights or bookmarks yet", "Chưa có ghi chú"},
    {"Cannot write Clippings", "Không ghi được Clippings"},
    {"Selected font file missing", "Thiếu tệp phông"},
    {"Font file not installed", "Chưa cài phông"},
    // Dialogs and overlays
    {"Go to percent", "Đi tới phần trăm"},
    {"Wheel: 1%   Keys: 10%", "Xoay: 1%   Phím: 10%"},
    {"Center: go   Back: cancel", "Giữa: đi   Back: hủy"},
    {"Any key: back", "Phím bất kỳ: quay lại"},
    {"Center: on/off   Back: settings", "Giữa: bật/tắt   Back: cài đặt"},
    {"None installed.", "Chưa cài từ điển."},
    {"Put StarDict folders in", "Chép thư mục StarDict vào"},
    {"Links and footnotes", "Liên kết, chú thích"},
    {"No links on this page", "Trang này không có liên kết"},
    {"Wheel: pick  Center: go", "Xoay: chọn  Giữa: đi"},
    {"Back: close   Center: jump", "Back: đóng   Giữa: tới"},
    {"Back: selected text", "Back: chữ đã chọn"},
    {"Offline dictionary", "Từ điển"},
    {"No match in the dictionaries", "Không tìm thấy trong từ điển"},
    {"Wheel: more  Back: selected text", "Xoay: thêm  Back: chữ đã chọn"},
    {"Footnote", "Chú thích"},
    {"No bookmarks yet", "Chưa có dấu trang"},
    {"No chapters", "Không có mục lục"},
    {"Wheel: select  Center: go  Back: settings", "Xoay: chọn  Giữa: đi  Back: cài đặt"},
    {"Wheel: word   Center: start", "Xoay: từ   Giữa: bắt đầu"},
    {"Wheel: extend  Center: actions", "Xoay: mở rộng  Giữa: thao tác"},
    {"Highlight", "Tô sáng"},
    {"Define", "Tra từ"},
    {"Remove", "Xóa"},
    {"Cancel", "Hủy"},
    {"Page text unavailable", "Không đọc được chữ"},
    // Shelf
    {"Home", "Trang chủ"},
    {"Current book", "Sách đang đọc"},
    {"No books yet", "Chưa có sách"},
    {"Continue reading", "Đọc tiếp"},
    {"Browse files", "Duyệt sách"},
    {"Sort: ", "Xếp: "},
    {"Title A-Z", "Tên A-Z"},
    {"Recently read", "Mới đọc"},
    {"Recently added", "Mới thêm"},
    {"Online", "Mạng"},
    {"Search | Online", "Tìm | Mạng"},
    {"OK: open   Back: exit", "OK: mở   Back: thoát"},
    {"OK: open   Back: home", "OK: mở   Back: về"},
    {"OK: open/get   Back: prev", "OK: mở/tải   Back: lùi"},
    {"OK: press   Back: close", "OK: nhấn   Back: đóng"},
    {"No EPUB books found", "Không có sách"},
    {"No matching books", "Không có sách phù hợp"},
    {"No EPUBs or folders", "Không có sách hay thư mục"},
    {"Back: leave catalog", "Back: rời thư viện"},
    {"Search library", "Tìm sách"},
    {"Search catalog", "Tìm trong thư viện"},
    {"Done: search", "Done: tìm"},
    {"Wheel: key   Centre: press   Again: next letter", "Xoay: phím   Giữa: nhấn   Lại: chữ kế"},
    {"Search this catalog", "Tìm trong thư viện này"},
    {"More results", "Thêm kết quả"},
    {"Loading catalog...", "Đang tải thư viện..."},
    {"Downloading EPUB...", "Đang tải sách..."},
    {"EPUB downloaded to Books/Online", "Đã tải vào Books/Online"},
    {"Searching...", "Đang tìm..."},
    {"No results", "Không có kết quả"},
    {"Opening EPUB...", "Đang mở sách..."},
    {"Cannot read this EPUB", "Không đọc được sách này"},
    {"Set .books/connected.conf", "Cần .books/connected.conf"},
    {"Type a search first", "Hãy nhập từ cần tìm"},
    {"This catalog has no search", "Thư viện không có tìm kiếm"},
    {"Connected library", "Thư viện"},
    // Catalog servers (M4)
    {"Catalog servers", "Máy chủ thư viện"},
    {"Choose a server to edit", "Chọn máy chủ để sửa"},
    {"New server", "Máy chủ mới"},
    {"Edit server", "Sửa máy chủ"},
    {"+ Add server", "+ Thêm máy chủ"},
    {"Edit a server", "Sửa máy chủ"},
    {"Name", "Tên"},
    {"User", "Tài khoản"},
    {"Password", "Mật khẩu"},
    {"Save", "Lưu"},
    {"Delete server", "Xóa máy chủ"},
    {"Press again to delete", "Nhấn lần nữa để xóa"},
    {"(the host name)", "(tên máy chủ)"},
    {"(none)", "(không có)"},
    {"Server name", "Tên máy chủ"},
    {"Catalog URL", "Địa chỉ thư viện"},
    {"User name", "Tài khoản"},
    {"Done: keep", "Done: giữ"},
    {"Server saved", "Đã lưu máy chủ"},
    {"Server deleted", "Đã xóa máy chủ"},
    {"Add a catalog server", "Hãy thêm máy chủ"},
    {"Up to 8 servers", "Tối đa 8 máy chủ"},
    {"OK: edit   Back: cancel", "OK: sửa   Back: hủy"},
    {"Type the catalog URL", "Hãy nhập địa chỉ"},
    {"URL must start http:// or https://", "Địa chỉ phải bắt đầu http(s)://"},
    {"Password needs a user", "Cần tài khoản"},
    {"Value too long", "Quá dài"},
    {"Cannot save settings", "Không lưu được"},
    {"Offline copy", "Bản lưu"},
    // About
    {"An ebook reader for Q2 Pod", "Ứng dụng đọc sách cho Q2 Pod"},
    {"on the Shanling Q2.", "trên Shanling Q2."},
    {"A port of CrossPoint Reader:", "Chuyển từ CrossPoint Reader:"},
    {"its EPUB engine, layout and", "bộ đọc EPUB, dàn trang và"},
    {"fonts, on the Q2's Linux.", "phông chữ, trên Linux của Q2."},
    {"MIT license; see LICENSE files", "Giấy phép MIT; xem tệp LICENSE"},
    // Sync
    {"Enable sync in connected.conf", "Bật đồng bộ trong connected.conf"},
    {"Progress uploaded", "Đã gửi tiến độ"},
    {"No remote progress", "Máy chủ chưa có tiến độ"},
    {"Remote position applied", "Đã lấy vị trí"},
    {"Remote ahead. Pull first.", "Máy chủ mới hơn. Lấy về trước."},
};

inline const char* tr(const char* english) {
  if (!english || uiLanguage() == UiLanguage::English) return english;
  for (const auto& t : kVietnamese)
    if (std::strcmp(t.en, english) == 0) return t.vi;
  return english;
}
inline std::string tr(const std::string& english) { return tr(english.c_str()); }

}  // namespace books
