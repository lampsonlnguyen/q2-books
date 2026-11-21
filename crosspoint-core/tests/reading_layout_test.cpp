// SPDX-License-Identifier: MIT
#include "ReadingLayout.hpp"
#include <ReaderRenderSpec.h>
#include <cassert>
#include <cstdint>
int main() {
  crosspod::ReaderPreferences prefs{};
  const auto original=crosspod::makeReaderRenderSpec(prefs,100,375,290);
  assert(original.fontId==100);
  assert(original.viewportWidth==345 && original.viewportHeight==290);
  assert(original.lineCompression==1.0f);
  assert(original.embeddedStyle && !original.hyphenationEnabled);
  auto changed=prefs;
  changed.margin=30;
  changed.linePercent=140;
  changed.alignment=1;
  changed.hyphenation=true;
  changed.embeddedStyle=false;
  const auto updated=crosspod::makeReaderRenderSpec(changed,104,375,290);
  assert(updated.viewportWidth==313);
  assert(updated.lineCompression==1.4f);
  assert(updated.paragraphAlignment==1);
  assert(updated.hyphenationEnabled && !updated.embeddedStyle);
  assert(readerRenderSpecSignature(updated)!=readerRenderSpecSignature(original));
  // These values are layout options; the fixed-size UI/menu font never
  // appears in the Section spec.
  assert(updated.fontId==104);
  return 0;
}
