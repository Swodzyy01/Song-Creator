#pragma once
#include <string>

#include "Decor.hpp"
#include "Gen.hpp"

namespace ais {

// Locale-independent number formatting (at most 2 decimals, no trailing zeros): 12, 0.5, 0.35
std::string fmtNum(double v);

// The raw, human-readable GD level string: header (colours + start settings) followed by "1,<id>,2,<x>,3,<y>...;" objects.
std::string buildLevelString(Level const& lv, Palette const& pal);

// GD stores level strings as base64url(gzip(raw)). Implemented here (fixed-Huffman deflate + LZ77) so the
// result does not depend on any game API and can be checked against zlib in the tests.
std::string gzipBase64(std::string const& raw);

}  // namespace ais
