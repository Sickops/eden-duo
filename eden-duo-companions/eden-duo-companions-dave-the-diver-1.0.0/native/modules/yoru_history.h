// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "yoru_reader.h"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yoru {
// Marker CollapseSoftBreaks leaves where a soft wrap was joined. The English patch stores two
// Latin characters per codepoint, so the spaces around a break are only visible after decoding;
// JoinSoftBreaks then turns each marker into exactly one space (or nothing for raw text).
inline constexpr char SoftBreak = '\x1f';
// 2026-10-15 polish: script records hard-wrap a single LF for the MAIN screen's own window
// width, which is narrower than the companion's -- displayed/measured verbatim, that reads as
// ragged ("...barely / any light to begin / with, it's..."). A lone '\n' (or "\r\n", '\r' is
// dropped as a presentational CRLF partner, never counted on its own) is therefore collapsed
// into one space so the companion rewraps the paragraph at its own width; two or more
// consecutive newlines are a genuine paragraph break and collapse to exactly one '\n' instead.
// Display/measurement only -- the reader's own Row::text is never touched by this.
inline std::string CollapseSoftBreaks(std::string_view text) {
  std::string stripped;
  stripped.reserve(text.size());
  for (char c : text)
    if (c != '\r')
      stripped.push_back(c);
  std::string out;
  out.reserve(stripped.size());
  size_t i = 0;
  while (i < stripped.size()) {
    if (stripped[i] == '\n') {
      size_t j = i;
      while (j < stripped.size() && stripped[j] == '\n')
        ++j;
      if (j - i >= 2) {
        out.push_back('\n');
      } else {
        // A soft wrap joins with exactly one space: the patch ends lines with a space and starts
        // narration continuation lines with a U+3000 indent, which would otherwise leave a
        // three-space gap mid-sentence.
        while (!out.empty() && out.back() == ' ')
          out.pop_back();
        out.push_back(SoftBreak);
        while (j < stripped.size()) {
          if (stripped[j] == ' ')
            ++j;
          else if (stripped.compare(j, 3, "\xE3\x80\x80") == 0)
            j += 3;
          else
            break;
        }
      }
      i = j;
    } else {
      out.push_back(stripped[i]);
      ++i;
    }
  }
  return out;
}
// Decoded text: each SoftBreak becomes one space, swallowing the spaces on both sides of it.
inline std::u32string JoinSoftBreaks(const std::u32string &text) {
  std::u32string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != char32_t(SoftBreak)) {
      out.push_back(text[i]);
      continue;
    }
    while (!out.empty() && out.back() == U' ')
      out.pop_back();
    while (i + 1 < text.size() && text[i + 1] == U' ')
      ++i;
    if (!out.empty() && i + 1 < text.size())
      out.push_back(U' ');
  }
  return out;
}
// Greedy wrap of decoded English text (one char32 per Latin character) that keeps the
// source's own whitespace: runs of spaces and U+3000 indents (decoded as two spaces) survive
// inside a line, so letter-spaced lines such as "g i v e   i t   b a c k" keep their wider word
// gaps. Whitespace is dropped only where a line breaks. '\n' forces a break, '\r' is ignored.
// `fits(chars)` says whether a line of that many decoded characters fits the width.
template <typename Fits>
std::vector<std::u32string> WrapKeepingSpaces(const std::u32string &text, Fits fits) {
  std::vector<std::u32string> lines;
  std::u32string line, gap;
  size_t i = 0;
  auto end_line = [&] {
    lines.push_back(line);
    line.clear();
    gap.clear();
  };
  while (i < text.size()) {
    const char32_t c = text[i];
    if (c == U'\r') {
      ++i;
    } else if (c == U'\n') {
      end_line();
      ++i;
    } else if (c == U' ') {
      gap.push_back(c);
      ++i;
    } else {
      size_t j = i;
      while (j < text.size() && text[j] != U' ' && text[j] != U'\n' &&
             text[j] != U'\r')
        ++j;
      const std::u32string word = text.substr(i, j - i);
      if (!line.empty() && !fits(line.size() + gap.size() + word.size()))
        end_line(); // The gap at the break point is dropped.
      line += gap;  // A leading indent on the first line is kept.
      line += word;
      gap.clear();
      i = j;
    }
  }
  if (!line.empty())
    lines.push_back(line);
  return lines;
}
// Native rows stay chronological and include the current main-screen utterance. Companion
// history excludes that row while it is on screen and includes it once the text window has been
// cleared (Snapshot::text_visible false). Offset 0 is the newest past row; increasing offsets
// move down into older history.
inline size_t PastCount(const Snapshot &s) {
  if (!s.ready || s.rows.empty())
    return 0;
  return s.text_visible ? s.rows.size() - 1 : s.rows.size();
}
inline std::optional<size_t> PastIndex(const Snapshot &s, int offset) {
  if (offset < 0 || size_t(offset) >= PastCount(s))
    return std::nullopt;
  return PastCount(s) - 1 - size_t(offset);
}
} // namespace yoru
