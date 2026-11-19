#include <cassert>
#include <cstring>
#include <string>
#include "ContainerParser.h"

int main() {
  const std::string xml=R"(<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>)";
  ContainerParser parser(xml.size());
  assert(parser.setup());
  const size_t half=xml.size()/2;
  assert(parser.write(reinterpret_cast<const uint8_t*>(xml.data()),half)==half);
  assert(parser.write(reinterpret_cast<const uint8_t*>(xml.data()+half),xml.size()-half)==xml.size()-half);
  assert(parser.fullPath=="OEBPS/content.opf");
  return 0;
}
