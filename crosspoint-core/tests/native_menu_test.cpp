// SPDX-License-Identifier: MIT
#include "NativeMenu.hpp"
#include <cassert>
#include <cstdlib>
#include <string>

int main() {
  setenv("BOOKS_LANG","en",1);
  const std::string header="Q2SPOT/1\n123\tok\t\t\n";
  const std::string rows="screen\tbrowse\tBrowse files\t\t\t\n"
      "screen\thome\tBooks Home\t\t\t\n"
      "screen\toptions\tOptions\t\t\t\n";
  assert(books::nativeMenuReply("Q2SPOT/1\n123\tmenu\n",true,false)==header+rows);
  const auto recent=books::nativeMenuReply("Q2SPOT/1\n123\tmenu\n",true,true);
  assert(recent==header+"screen\tresume\tContinue reading\t\t\t\n"+rows);
  const auto older=books::nativeMenuReply("Q2SPOT/1\n91\tmenu\n",false,true);
  assert(older=="Q2SPOT/1\n91\tok\t\t\n"
      "info\t\tUpdate Q2 Pod to open Books\t\t\t\n");
  assert(books::nativeMenuReply("Q2SPOT/1\n123\tmenu",true,true)=="Q2SPOT/1\n123\tbad\t\t\n");
  assert(books::nativeMenuReply("Q2SPOT/1\nabc\tmenu\n",true,true)=="Q2SPOT/1\n0\tbad\t\t\n");
  assert(books::nativeMenuReply("Q2SPOT/1\n123\tstop\n",true,true)=="Q2SPOT/1\n123\tbad\t\t\n");
}
