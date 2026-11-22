// SPDX-License-Identifier: MIT
#include "ConnectedHttp.hpp"
#include "ConnectedConfig.hpp"
#include "OpdsCatalog.hpp"
#include "PortableMd5.hpp"
#include "KoreaderSync.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

int main() {
  using namespace books;
  assert(md5String("")=="d41d8cd98f00b204e9800998ecf8427e");
  assert(md5String("abc")=="900150983cd24fb0d6963f7d28e17f72");
  assert(md5String("message digest")=="f96b697d7cb7938d525a2f31aaf161d0");
  assert(parseHttpUrl("https://host/api").valid);
  assert(parseHttpUrl("https://host/api").secure);
  assert(parseHttpUrl("https://host/api").port=="443");
  assert(parseHttpUrl("https://host:8443/api").port=="8443");
  assert(absoluteHttpUrl(parseHttpUrl("https://host:443/a"))=="https://host/a");
  assert(absoluteHttpUrl(parseHttpUrl("https://host:8443/a"))==
         "https://host:8443/a");
  assert(resolveHttpHref("https://host/api/root","../books")==
         "https://host/books");
  assert(resolveHttpHref("https://host/api","https://host/b")==
         "https://host/b");
  assert(resolveHttpHref("https://host/api","http://host/b").empty());
  assert(resolveHttpHref("http://host/api","https://host/b").empty());
  assert(resolveHttpHref("https://host/api","https://different/b").empty());
  assert(resolveHttpHref("https://host:8443/api","https://host/api").empty());
  assert(resolveHttpHref("https://host/a","//evil.example/b").empty());
  assert(!parseHttpUrl("http://host:0/a").valid);
  assert(!parseHttpUrl("http://user@host/a").valid);
  assert(!parseHttpUrl("http://host/a\r\nInjected: x").valid);
  assert(resolveHttpHref("http://127.0.0.1:5150/api/opds/key/root",
                         "../books")==
         "http://127.0.0.1:5150/api/opds/books");
  assert(resolveHttpHref("http://127.0.0.1:5150/api/opds/key/root",
                         "/api/opds/key/series")==
         "http://127.0.0.1:5150/api/opds/key/series");
  assert(resolveHttpHref("http://host/a","https://host/b").empty());
  assert(resolveHttpHref("http://host/a","http://different/b").empty());
  assert(resolveHttpHref("http://host/a","//different/b").empty());
  assert(resolveHttpHref("http://host/a","../../admin").empty());
  assert(resolveHttpHref("http://host/a","%2e%2e/admin").empty());
  const std::string opds=R"xml(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
 <title>My Kavita &amp; Other Books</title>
 <entry><id>series/12</id><title>Science &amp; Fiction</title>
   <link rel="subsection" type="application/atom+xml;profile=opds-catalog"
         href="/api/opds/key/series/12"/></entry>
 <entry><id>book/34</id><title>Tiếng Việt &amp; English</title>
   <author><name>Nguyễn Văn An</name></author>
   <link rel="http://opds-spec.org/acquisition"
         type="application/epub+zip" href="books/34/download.epub"/></entry>
 <entry><title>External malware</title>
   <link rel="http://opds-spec.org/acquisition"
         type="application/epub+zip" href="http://evil.example/thing.epub"/></entry>
</feed>)xml";
  const auto feed=parseOpdsFeed("http://host/api/opds/key/root",opds);
  assert(feed.error.empty());
  assert(feed.title=="My Kavita & Other Books");
  assert(feed.entries.size()==2);
  assert(feed.entries[0].kind==OpdsEntry::Kind::Catalog);
  assert(feed.entries[0].title=="Science & Fiction");
  assert(feed.entries[1].kind==OpdsEntry::Kind::Epub);
  assert(feed.entries[1].author=="Nguyễn Văn An");
  assert(feed.entries[1].href=="http://host/api/opds/key/books/34/download.epub");
  assert(parseOpdsFeed("http://host/a","<feed><entry>").error.size()>0);
  // M4: feed-level paging and search links; entry links are not feed links.
  {
    const std::string paged=R"xml(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
 <title>Page 1</title>
 <link rel="next" type="application/atom+xml" href="?page=2"/>
 <link rel="search" type="application/opensearchdescription+xml" href="/opds/search.xml"/>
 <link rel="search" type="application/atom+xml" href="/opds/find?q={searchTerms}&amp;p={startPage?}"/>
 <entry><title>Inside</title><link rel="next" href="/wrong"/>
   <link rel="subsection" type="application/atom+xml" href="/opds/sub"/></entry>
</feed>)xml";
    const auto f=parseOpdsFeed("http://host/opds/root",paged);
    assert(f.error.empty());
    assert(f.nextHref=="http://host/opds/root?page=2");
    assert(f.searchDescription=="http://host/opds/search.xml");
    assert(f.searchTemplate=="http://host/opds/find?q={searchTerms}&p={startPage?}");
    assert(f.entries.size()==1 && f.entries[0].href=="http://host/opds/sub");
    std::string error;
    assert(opdsSearchUrl(f,"Tiếng Việt & co",error)==
           "http://host/opds/find?q=Ti%E1%BA%BFng%20Vi%E1%BB%87t%20%26%20co&p=1");
    assert(opdsSearchUrl(f,"",error).empty());
    OpdsFeed none;
    assert(opdsSearchUrl(none,"x",error).empty() && error=="This catalog has no search");
    // Cross-origin templates and next links are refused, as entry links are.
    const auto evil=parseOpdsFeed("http://host/a",R"xml(<feed xmlns="http://www.w3.org/2005/Atom">
 <link rel="next" href="http://evil.example/next"/>
 <link rel="search" type="application/atom+xml" href="http://evil.example/s?q={searchTerms}"/></feed>)xml");
    assert(evil.nextHref.empty() && evil.searchTemplate.empty());
    const std::string osd=R"xml(<?xml version="1.0"?>
<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">
 <Url type="text/html" template="/html?q={searchTerms}"/>
 <Url type="application/atom+xml;profile=opds-catalog" template="/opds/search/{searchTerms}?count={count?}"/>
</OpenSearchDescription>)xml";
    const auto tpl=parseOpenSearchTemplate("http://host/opds/search.xml",osd);
    assert(tpl=="http://host/opds/search/{searchTerms}?count={count?}");
    assert(fillOpdsSearchTemplate(tpl,"a b")=="http://host/opds/search/a%20b?count=20");
    assert(fillOpdsSearchTemplate("http://host/x?q={searchTerms}&z={unknown}","q")==
           "http://host/x?q=q&z=");
    assert(fillOpdsSearchTemplate("http://host/x?q={searchTerms","q").empty());
  }
  assert(jsonQuoted("abc\n\"hello")=="\"abc\\n\\\"hello\"");
  std::string position;
  assert(jsonStringValue("{\"progress\":\"/body/DocFragment[2]/body/p[3]/text().7\"}",
                         "progress",position));
  assert(position=="/body/DocFragment[2]/body/p[3]/text().7");
  float percent=0;
  assert(jsonFloatValue("{\"percentage\":0.375}","percentage",percent));
  assert(percent>0.374f && percent<0.376f);
  assert(!jsonFloatValue("{\"percentage\":1.5}","percentage",percent));
  assert(validDocumentDigest(md5String("book")));
  assert(!validDocumentDigest("not-a-digest"));
  char dir[]="/tmp/books-opds-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string configFile=connectedConfigPath();
  FILE* conf=std::fopen(configFile.c_str(),"wb");
  assert(conf);
  assert(std::fputs(
    "opds_url=http://127.0.0.1:5150/api/opds/abc\n"
    "sync_url=http://127.0.0.1:5150/api/koreader/abc\n"
    "sync_username=test\n"
    "sync_password=secret\n"
    "enable_sync=1\n",conf)>0);
  assert(std::fclose(conf)==0);
  const auto cfg=loadConnectedConfig();
  assert(cfg.configured());
  assert(cfg.syncConfigured());
  assert(koReaderHeaders(cfg)["x-auth-key"]==md5String("secret"));
  const OpdsEntry book{OpdsEntry::Kind::Epub,"My EPUB","Author","id",
      "http://127.0.0.1:5150/api/opds/abc/download.epub"};
  const auto destination=opdsDestination(dir,book);
  assert(destination.find("/Online/My-EPUB-")!=std::string::npos);
  assert(destination.substr(destination.size()-5)==".epub");
  assert(opdsDestination(dir,{OpdsEntry::Kind::Catalog,"Folder","","",
      "http://127.0.0.1:5150/a"}).empty());
  std::puts("Books OPDS and KOSync transport contracts: PASS");
  return 0;
}
