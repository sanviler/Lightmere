#pragma once
// Dialogue texts (txt*.epf). Three consecutive entries per language:
//   [3k]   stream of u16 BE tokens (first word = number of dictionary words)
//   [3k+1] dictionary: words separated by NUL, CP437 encoding
//   [3k+2] index table
// Token: < $8000 word (index = w & $3FFF; bit $4000 = end of message after the word); $80xx separator (character xx); bit $4000 = end of message.
// The game font builds m/w/M/W from two glyphs: "%&"=m "#$"=w "^_"=M "\]"=W.
// Languages: 0=EN 1=FR 2=DE 3=IT.
#include "formats/common.h"
#include <map>

namespace dm {

enum class Lang { EN = 0, FR = 1, DE = 2, IT = 3 };

// Messages in UTF-8 with the wide glyphs already replaced.
std::vector<std::string> decodeMessages(const Bytes& stream, const Bytes& dictionary);

std::string cp437ToUtf8(const std::string& s);

// Place names per room (as in the panel, routines $0114C/$060DE).
// The index table [3k+2] holds lists of records [type][id][word: position in the stream][...][$FF];
// room names are the records of type 1 (id = room number) of the list that holds the most of them.
std::map<int, std::string> roomNames(const Bytes& stream, const Bytes& dictionary, const Bytes& index);

} // namespace dm
