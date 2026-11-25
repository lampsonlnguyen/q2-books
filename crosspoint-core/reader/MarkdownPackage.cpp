// SPDX-License-Identifier: MIT
#include "MarkdownPackage.hpp"

#include "TxtPackage.hpp"
#include <md4c.h>
extern "C" {
#include <entity.h>
}

#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <map>

namespace books {
namespace {
constexpr size_t maxSource = 1024 * 1024;
constexpr size_t chapterTarget = 32 * 1024;
constexpr size_t maxChapter = 256 * 1024;
constexpr size_t maxMarkup = 4 * 1024 * 1024;
constexpr size_t maxImage = 512 * 1024;
constexpr size_t maxImageBytes = 2 * 1024 * 1024;
constexpr size_t maxImages = 32;
constexpr size_t maxHeadings = 1024;
constexpr size_t maxLinks = 4096;
constexpr unsigned packageVersion = 1;

struct FileDescriptor {
  int fd;
  explicit FileDescriptor(int value) : fd(value) {}
  ~FileDescriptor() { if (fd >= 0) ::close(fd); }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
};

bool readRegularFile(int fd, size_t limit, std::string& data) {
  struct stat st{};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      static_cast<uint64_t>(st.st_size) > limit) return false;
  data.resize(static_cast<size_t>(st.st_size));
  for (size_t offset = 0; offset < data.size();) {
    const ssize_t count = ::read(fd, data.data() + offset, data.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    offset += static_cast<size_t>(count);
  }
  char extra;
  ssize_t count;
  do { count = ::read(fd, &extra, 1); } while (count < 0 && errno == EINTR);
  return count == 0;
}

bool xmlCodepoint(unsigned value) {
  return value == 9 || value == 10 || value == 13 ||
         (value >= 0x20 && value <= 0xD7FF) ||
         (value >= 0xE000 && value <= 0xFFFD) ||
         (value >= 0x10000 && value <= 0x10FFFF);
}

void appendCodepoint(std::string& out, unsigned value) {
  if (!xmlCodepoint(value)) value = 0xFFFD;
  if (value < 0x80) {
    out.push_back(static_cast<char>(value));
    return;
  }
  const unsigned tail = value < 0x800 ? 1 : value < 0x10000 ? 2 : 3;
  const unsigned prefix[] = {0, 0xC0, 0xE0, 0xF0};
  out.push_back(static_cast<char>(prefix[tail] | (value >> (6 * tail))));
  for (unsigned i = tail; i > 0; --i)
    out.push_back(static_cast<char>(0x80 | ((value >> (6 * (i - 1))) & 0x3F)));
}

std::string cleanUtf8(std::string_view input) {
  if (input.substr(0, 3) == "\xEF\xBB\xBF") input.remove_prefix(3);
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    const unsigned char first = input[i];
    const size_t len = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
                       first >= 0xE0 && first <= 0xEF ? 3 :
                       first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    const unsigned minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    const unsigned masks[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
    unsigned value = first & masks[len];
    size_t consumed = 1;
    while (consumed < len && i + consumed < input.size()) {
      const unsigned char next = input[i + consumed];
      if ((next & 0xC0) != 0x80) break;
      value = (value << 6) | (next & 0x3F);
      ++consumed;
    }
    appendCodepoint(out, len && len == consumed && value >= minimum[len] ? value : 0xFFFD);
    i += consumed;
  }
  return out;
}

std::string decodeEntity(std::string_view input) {
  if (input.size() > 3 && input[1] == '#') {
    auto digits = input.substr(2, input.size() - 3);
    const bool hex = !digits.empty() && (digits.front() == 'x' || digits.front() == 'X');
    if (hex) digits.remove_prefix(1);
    unsigned value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
    std::string out;
    appendCodepoint(out, parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() ? value : 0xFFFD);
    return out;
  }
  if (const ENTITY* entity = entity_lookup(input.data(), input.size())) {
    std::string out;
    appendCodepoint(out, entity->codepoints[0]);
    if (entity->codepoints[1]) appendCodepoint(out, entity->codepoints[1]);
    return out;
  }
  return std::string(input);
}

std::string attribute(const MD_ATTRIBUTE& attr) {
  if (attr.size > 1024) return {};
  std::string out;
  for (size_t i = 0; attr.size && attr.substr_offsets[i] < attr.size; ++i) {
    const std::string_view part(attr.text + attr.substr_offsets[i],
                                attr.substr_offsets[i + 1] - attr.substr_offsets[i]);
    if (attr.substr_types[i] == MD_TEXT_ENTITY) out += decodeEntity(part);
    else out += part;
  }
  return out;
}

std::string decodeUrl(std::string_view raw) {
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    unsigned char ch = raw[i];
    if (ch == '%') {
      if (i + 2 >= raw.size()) return {};
      const auto end = raw.data() + i + 3;
      unsigned value = 0;
      const auto converted = std::from_chars(raw.data() + i + 1, end, value, 16);
      if (converted.ptr != end || converted.ec != std::errc{}) return {};
      ch = static_cast<unsigned char>(value);
      i += 2;
    }
    if (ch < 32 || ch == 127) return {};
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

std::string slug(std::string_view label) {
  std::string out;
  for (unsigned char ch : label) {
    if (ch >= 'A' && ch <= 'Z') out.push_back(static_cast<char>(ch + ('a' - 'A')));
    else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
             ch == '_' || ch == '-' || ch >= 0x80) out.push_back(static_cast<char>(ch));
    else if (ch == ' ' || ch == '\t') out += '-';
  }
  return out;
}

std::string chapterFile(size_t index) { return "m" + std::to_string(index) + ".xhtml"; }

bool readImage(int root, std::string_view path, std::string& data) {
  if (path.empty() || path.size() > 512 || path.find_first_of(":\\?#") != path.npos) return false;
  FileDescriptor directory(::dup(root));
  if (directory.fd < 0) return false;
  for (size_t depth = 0; depth < 8; ++depth) {
    const auto slash = path.find('/');
    const std::string component(path.substr(0, slash));
    if (component.empty() || component.front() == '.') return false;
    if (slash == path.npos) {
      const FileDescriptor image(::openat(directory.fd, component.c_str(),
                                           O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
      return readRegularFile(image.fd, maxImage, data);
    }
    const int next = ::openat(directory.fd, component.c_str(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0) return false;
    ::close(directory.fd);
    directory.fd = next;
    path.remove_prefix(slash + 1);
  }
  return false;
}

struct Markdown {
  struct Link { size_t offset; std::string target; };
  struct Chapter { std::string html; std::vector<Link> links; };
  struct Heading { size_t chapter; std::string id, label; };
  struct Image { std::string name, type, data; };

  explicit Markdown(int root) : root(root) {}
  int root;
  size_t depth = 0, markupBytes = 0, imageBytes = 0, headingCount = 0, linkCount = 0;
  unsigned imageDepth = 0;
  size_t headingLevel = 0;
  std::string headingLabel, imagePath, alt;
  std::vector<Chapter> chapters{1};
  std::vector<Heading> headings;
  std::map<std::string, size_t> anchors;
  std::map<std::string, size_t> imagePaths;
  std::vector<Image> images;

  bool append(std::string_view html) {
    auto& chapter = chapters.back().html;
    if (chapter.size() + html.size() > maxChapter || markupBytes + html.size() > maxMarkup) return false;
    chapter += html;
    markupBytes += html.size();
    return true;
  }

  void splitChapter(bool startsSection) {
    const size_t size = chapters.back().html.size();
    if (size >= chapterTarget || (startsSection && size && chapters.size() < maxHeadings))
      chapters.emplace_back();
  }

  std::string addImage(const std::string& path) {
    constexpr size_t missing = maxImages;
    if (auto found = imagePaths.find(path); found != imagePaths.end())
      return found->second == missing ? std::string{} : images[found->second].name;
    if (images.size() >= maxImages || imagePaths.size() >= maxImages * 2 ||
        imageBytes >= maxImageBytes) return {};
    imagePaths.emplace(path, missing);
    const bool png = hasBookSuffix(path, ".png");
    const bool jpeg = hasBookSuffix(path, ".jpg") || hasBookSuffix(path, ".jpeg");
    std::string data;
    if ((!png && !jpeg) || !readImage(root, path, data) || data.size() > maxImageBytes - imageBytes) return {};
    if (png && data.compare(0, 8, "\x89PNG\r\n\x1A\n", 8) != 0) return {};
    if (jpeg && data.compare(0, 3, "\xFF\xD8\xFF", 3) != 0) return {};
    const std::string name = "image" + std::to_string(images.size()) + (png ? ".png" : ".jpg");
    imagePaths[path] = images.size();
    imageBytes += data.size();
    images.push_back({name, png ? "image/png" : "image/jpeg", std::move(data)});
    return name;
  }

  static std::string_view blockTag(MD_BLOCKTYPE type) {
    switch (type) {
      case MD_BLOCK_QUOTE: return "blockquote";
      case MD_BLOCK_UL: return "ul";
      case MD_BLOCK_OL: return "ol";
      case MD_BLOCK_LI: return "li";
      case MD_BLOCK_P: return "p";
      default: return {};
    }
  }

  int enterBlock(MD_BLOCKTYPE type, void* detail) {
    if (++depth > 64) return 1;
    const size_t level = type == MD_BLOCK_H ? static_cast<MD_BLOCK_H_DETAIL*>(detail)->level : 0;
    if (depth == 2) splitChapter(level && level <= 2);
    if (level) {
      headingLevel = level;
      headingLabel.clear();
      const std::string heading = "h" + std::to_string(level);
      const std::string id = headingCount < maxHeadings ? " id=\"h" + std::to_string(headingCount) + "\"" : "";
      return !append("<" + heading + id + ">");
    }
    if (type == MD_BLOCK_HR) return !append("<hr/>");
    if (type == MD_BLOCK_CODE) return !append("<pre><code>");
    const auto tag = blockTag(type);
    if (tag.empty()) return 0;
    std::string start;
    if (type == MD_BLOCK_OL) {
      const auto value = static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start;
      if (value != 1) start = " start=\"" + std::to_string(value) + "\"";
    }
    return !append("<" + std::string(tag) + start + ">");
  }

  int leaveBlock(MD_BLOCKTYPE type) {
    --depth;
    if (type == MD_BLOCK_H) {
      if (headingCount < maxHeadings) {
        const auto id = "h" + std::to_string(headingCount++);
        const auto base = slug(headingLabel);
        if (!base.empty()) {
          auto key = base;
          for (size_t n = 1; anchors.count(key); ++n) key = base + "-" + std::to_string(n);
          anchors.emplace(std::move(key), headings.size());
        }
        headings.push_back({chapters.size() - 1, id,
                            headingLabel.empty() ? "Section" : cleanUtf8(headingLabel)});
      }
      return !append("</h" + std::to_string(std::exchange(headingLevel, 0)) + ">");
    }
    if (type == MD_BLOCK_CODE) return !append("</code></pre>");
    const auto tag = blockTag(type);
    return !tag.empty() && !append("</" + std::string(tag) + ">");
  }

  static std::string_view spanTag(MD_SPANTYPE type) {
    switch (type) {
      case MD_SPAN_EM: return "em";
      case MD_SPAN_STRONG: return "strong";
      case MD_SPAN_CODE: return "code";
      case MD_SPAN_A: return "a";
      default: return {};
    }
  }

  int enterSpan(MD_SPANTYPE type, void* detail) {
    if (type == MD_SPAN_IMG) {
      if (imageDepth++ == 0) {
        imagePath = decodeUrl(attribute(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src));
        while (imagePath.compare(0, 2, "./") == 0) imagePath.erase(0, 2);
        alt.clear();
      }
      return 0;
    }
    if (imageDepth) return 0;
    const auto tag = spanTag(type);
    if (tag.empty()) return 0;
    if (type == MD_SPAN_A && linkCount < maxLinks) {
      const auto target = attribute(static_cast<MD_SPAN_A_DETAIL*>(detail)->href);
      if (!target.empty() && target.front() == '#') {
        auto decoded = decodeUrl(std::string_view(target).substr(1));
        if (!decoded.empty()) {
          ++linkCount;
          auto& chapter = chapters.back();
          chapter.links.push_back({chapter.html.size() + 2, std::move(decoded)});
        }
      }
    }
    return !append("<" + std::string(tag) + ">");
  }

  int leaveSpan(MD_SPANTYPE type) {
    if (type == MD_SPAN_IMG) {
      if (--imageDepth) return 0;
      const auto name = addImage(imagePath);
      if (name.empty()) return !append(txtpkg::xmlEscape(cleanUtf8(alt)));
      return !append("<img src=\"" + name + "\" alt=\"" + txtpkg::xmlEscape(cleanUtf8(alt)) + "\"/>");
    }
    if (imageDepth) return 0;
    const auto tag = spanTag(type);
    return !tag.empty() && !append("</" + std::string(tag) + ">");
  }

  int text(MD_TEXTTYPE type, std::string_view text) {
    std::string entity;
    if (type == MD_TEXT_ENTITY) {
      entity = decodeEntity(text);
      text = entity;
    } else if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
      text = " ";
    }
    if (headingLevel && headingLabel.size() < 160)
      headingLabel.append(text.substr(0, 160 - headingLabel.size()));
    if (imageDepth) {
      if (alt.size() < 512) alt.append(text.substr(0, 512 - alt.size()));
      return 0;
    }
    if (type == MD_TEXT_BR) return !append("<br/>");
    return !append(txtpkg::xmlEscape(text));
  }

  bool parse(const std::string& text) {
    const MD_PARSER parser{
      0, MD_FLAG_NOHTML,
      [](MD_BLOCKTYPE t, void* d, void* u) { return static_cast<Markdown*>(u)->enterBlock(t, d); },
      [](MD_BLOCKTYPE t, void*, void* u) { return static_cast<Markdown*>(u)->leaveBlock(t); },
      [](MD_SPANTYPE t, void* d, void* u) { return static_cast<Markdown*>(u)->enterSpan(t, d); },
      [](MD_SPANTYPE t, void*, void* u) { return static_cast<Markdown*>(u)->leaveSpan(t); },
      [](MD_TEXTTYPE t, const MD_CHAR* p, MD_SIZE n, void* u) {
        return static_cast<Markdown*>(u)->text(t, {p, n});
      },
      nullptr, nullptr
    };
    return md_parse(text.data(), static_cast<MD_SIZE>(text.size()), &parser, this) == 0;
  }

  std::string epub(std::string_view title) {
    txtpkg::ZipWriter zip;
    txtpkg::startEpub(zip);
    std::string manifest, spine, nav;
    size_t navIndex = 0, linkBytes = 0;
    const auto addNav = [&](const std::string& target, const std::string& label) {
      const auto id = std::to_string(++navIndex);
      nav += "<navPoint id=\"n" + id + "\" playOrder=\"" + id + "\"><navLabel><text>" +
             txtpkg::xmlEscape(label) + "</text></navLabel><content src=\"" + target + "\"/></navPoint>";
    };
    size_t nextHeading = 0;
    for (size_t i = 0; i < chapters.size(); ++i) {
      auto& chapter = chapters[i];
      const auto file = chapterFile(i);
      const auto id = "m" + std::to_string(i);
      std::string html = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>" +
                         txtpkg::xmlEscape(title) + "</title></head><body>";
      size_t offset = 0;
      for (const auto& link : chapter.links) {
        const auto found = anchors.find(link.target);
        if (found == anchors.end()) continue;
        const auto& dest = headings[found->second];
        const std::string href = " href=\"" + chapterFile(dest.chapter) + "#" + dest.id + "\"";
        linkBytes += href.size();
        if (markupBytes + linkBytes > maxMarkup) return {};
        html.append(chapter.html, offset, link.offset - offset);
        html += href;
        offset = link.offset;
      }
      html.append(chapter.html, offset, std::string::npos);
      if (chapter.html.empty()) html += "<p> </p>";
      html += "</body></html>";
      zip.add("OEBPS/" + file, html);
      manifest += "<item id=\"" + id + "\" href=\"" + file + "\" media-type=\"application/xhtml+xml\"/>";
      spine += "<itemref idref=\"" + id + "\"/>";
      if (nextHeading == headings.size() || headings[nextHeading].chapter != i)
        addNav(file, "Part " + std::to_string(i + 1));
      while (nextHeading < headings.size() && headings[nextHeading].chapter == i) {
        const auto& heading = headings[nextHeading++];
        addNav(file + "#" + heading.id, heading.label);
      }
    }
    for (const auto& image : images) {
      zip.add("OEBPS/" + image.name, image.data);
      manifest += "<item id=\"" + image.name + "\" href=\"" + image.name +
                  "\" media-type=\"" + image.type + "\"/>";
    }
    txtpkg::finishEpub(zip, title, "books-markdown", "und", manifest, spine, nav);
    return zip.finish();
  }
};

std::string cacheHash(std::string_view data) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : data) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return std::to_string(hash);
}

bool cachedPackage(const std::string& path) {
  struct stat st{};
  return ::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

bool writePackage(const std::string& path, const std::string& payload) {
  std::string temporary = path + ".partXXXXXX";
  const int fd = ::mkstemp(temporary.data());
  if (fd < 0) return false;
  FILE* file = ::fdopen(fd, "wb");
  bool complete = false;
  if (file) {
    const bool written = std::fwrite(payload.data(), 1, payload.size(), file) == payload.size();
    complete = std::fclose(file) == 0 && written;
  } else {
    ::close(fd);
  }
  if (complete && ::rename(temporary.c_str(), path.c_str()) == 0) return true;
  std::remove(temporary.c_str());
  return false;
}
}  // namespace

std::string packageMarkdownBook(const std::string& path, const std::string& cacheDir,
                                std::string& error) {
  const FileDescriptor source(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  std::string raw;
  if (!readRegularFile(source.fd, maxSource, raw)) {
    error = "could not read a Markdown file up to 1 MiB";
    return {};
  }
  const auto slash = path.find_last_of('/');
  const auto filename = slash == path.npos ? std::string_view(path) : std::string_view(path).substr(slash + 1);
  if (!isMarkdownFilename(filename)) return {};
  const auto title = cleanUtf8(filename.substr(0, filename.size() - 3));
  const auto folder = slash == path.npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
  const FileDescriptor directory(::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  const bool mayHaveImages = raw.find("![") != std::string::npos;
  const std::string base = cacheDir + "/md-v" + std::to_string(packageVersion) + "-" +
                           cacheHash(path) + "-" + cacheHash(raw);
  const std::string simplePackage = base + ".epub";
  if (!mayHaveImages && cachedPackage(simplePackage)) return simplePackage;
  Markdown markdown(directory.fd);
  if (!markdown.parse(cleanUtf8(raw))) {
    error = "Markdown exceeded conversion limits or could not be parsed";
    return {};
  }
  const auto payload = markdown.epub(title);
  if (payload.empty()) {
    error = "Markdown exceeded the package size limit";
    return {};
  }
  // Recheck image references when opening the source, and keep the cached ZIP
  // and layout identity tied to the actual embedded bytes.
  const std::string out = mayHaveImages ? base + "-" + cacheHash(payload) + ".epub" :
                                          simplePackage;
  if (cachedPackage(out)) return out;
  ::mkdir(cacheDir.c_str(), 0755);
  if (writePackage(out, payload)) return out;
  error = "could not write the Markdown cache";
  return {};
}
}  // namespace books
