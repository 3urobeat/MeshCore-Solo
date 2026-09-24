#pragma once
// Keyboard data shared by every frontend: the long-press variants offered for
// a base key (phone-style: hold a letter to get its accented forms) and UTF-8
// case mapping for everything the keyboards can produce. Scripts are picked by
// NodePrefs::keyboard_main_alphabet / keyboard_alt_alphabet (Latin, Cyrillic,
// Greek); Latin diacritics never need a page of their own -- they're all
// reachable by holding the base letter.
//
// Standalone header (no MeshCore / display dependencies).

#include <stdint.h>
#include <string.h>

namespace kbd {

// ── Long-press variants ──────────────────────────────────────────────────────
// One UTF-8 string of concatenated variants per base key, lowercase. Latin
// covers the union of the European languages written in it (Polish, Czech,
// Slovak, German, French, Spanish, Portuguese, Italian, Nordic, Icelandic,
// Hungarian, Romanian, Turkish, Baltic, …). Ligature / non-diacritic letters
// are filed under their conventional key, as on a phone: ß → s, œ → o,
// æ → a, ð → d, þ → t.
static const char LATIN_BASES[] = "acdegiklnorstuyz";
static const char* const LATIN_VARIANTS[] = {
  "áàâãäåąăāæ",  // a
  "çćč",         // c
  "ďð",          // d
  "éèêëěęēė",    // e
  "ğģ",          // g
  "íîïīį",       // i
  "ķ",           // k
  "łĺľļ",        // l
  "ñńňņ",        // n
  "óòôõöøœő",    // o
  "řŕ",          // r
  "śšşșß",       // s
  "ťțþ",         // t
  "úùûüůűūų",    // u
  "ýÿ",          // y
  "źżž",         // z
};
static const int LATIN_COUNT = sizeof(LATIN_BASES) - 1;

// Cyrillic, on a Russian ЙЦУКЕН base: Ukrainian / Belarusian / Serbian /
// Macedonian letters under the key they sit on (or sound like).
struct Variant { const char* base; const char* variants; };
static const Variant CYRILLIC_VARIANTS[] = {
  { "е", "ёє" }, { "и", "ії" }, { "г", "ґѓ" }, { "у", "ў" }, { "д", "ђ" },
  { "й", "ј" },  { "л", "љ" },  { "н", "њ" },  { "ч", "ћ" }, { "ц", "џ" },
  { "к", "ќ" },  { "з", "ѕ" },  { "ь", "ъ" },
};
static const Variant GREEK_VARIANTS[] = {
  { "α", "ά" }, { "ε", "έ" }, { "η", "ή" }, { "ι", "ίϊΐ" }, { "ο", "ό" },
  { "υ", "ύϋΰ" }, { "ω", "ώ" }, { "σ", "ς" },
};
// Punctuation behind the period key (any script).
static const char PERIOD_VARIANTS[] = ",?!'\"-:;()";

// Latin index by ASCII base letter (for callers that key on a char), -1 if none.
static inline int latinGroup(char base) {
  for (int i = 0; i < LATIN_COUNT; i++) if (LATIN_BASES[i] == base) return i;
  return -1;
}

// Variants for a base key given as UTF-8 (lowercase), or nullptr.
static inline const char* variantsFor(const char* base) {
  if (!base || !base[0]) return nullptr;
  if (strcmp(base, ".") == 0) return PERIOD_VARIANTS;
  if (base[1] == '\0') {
    int g = latinGroup(base[0]);
    return g >= 0 ? LATIN_VARIANTS[g] : nullptr;
  }
  for (const Variant& v : CYRILLIC_VARIANTS) if (strcmp(v.base, base) == 0) return v.variants;
  for (const Variant& v : GREEK_VARIANTS)    if (strcmp(v.base, base) == 0) return v.variants;
  return nullptr;
}

// ── UTF-8 ────────────────────────────────────────────────────────────────────
static inline uint32_t decode(const uint8_t*& p) {
  uint32_t c = *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) { uint32_t r = (c & 0x1F) << 6; if (*p) r |= (*p++ & 0x3F); return r; }
  if ((c & 0xF0) == 0xE0) {
    uint32_t r = (c & 0x0F) << 12;
    if (*p) r |= (*p++ & 0x3F) << 6;
    if (*p) r |= (*p++ & 0x3F);
    return r;
  }
  return '?';
}

static inline int encode(uint32_t cp, char* out) {
  if (cp < 0x80)  { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  out[0] = (char)(0xE0 | (cp >> 12));
  out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[2] = (char)(0x80 | (cp & 0x3F));
  return 3;
}

// Uppercase of one codepoint, for every letter the keyboards produce. Each
// block pairs its cases differently:
//  - ASCII a-z, Cyrillic а-я, Greek α-ω: flat -0x20. Exceptions: ё → Ё, the
//    Cyrillic ѐ-џ extras (Ukrainian / Serbian …, U+0450-045F) are -0x50, ς has
//    no uppercase of its own and maps to Σ, and the tonos vowels ά έ ή ί ό ύ ώ
//    sit outside α-ω.
//  - Latin-1 à-þ: flat -0x20 (÷ excluded); ÿ → Ÿ (U+0178) is special.
//  - Latin Extended-A: adjacent pairs, lowercase = uppercase + 1, but the
//    parity flips around the unpaired ĸ (U+0138), ŉ (U+0149) and Ÿ (U+0178).
//  - ș ț (U+0219 / U+021B): same pair rule. Cyrillic ґ (U+0491): pair rule.
//  - ß → ẞ (U+1E9E).
static inline uint32_t toUpper(uint32_t cp) {
  if (cp == 0x0451)                                     return 0x0401;   // ё
  if (cp >= 0x0450 && cp <= 0x045F)                     return cp - 0x50; // ѐ-џ
  if (cp == 0x0491)                                     return 0x0490;   // ґ
  if (cp == 0x03C2)                                     return 0x03A3;   // ς → Σ
  if (cp == 0x03AC)                                     return 0x0386;   // ά
  if (cp >= 0x03AD && cp <= 0x03AF)                     return cp - 0x25; // έ ή ί
  if (cp == 0x03CC)                                     return 0x038C;   // ό
  if (cp == 0x03CD || cp == 0x03CE)                     return cp - 0x3F; // ύ ώ
  if (cp == 0x03CA || cp == 0x03CB)                     return cp - 0x20; // ϊ ϋ
  if (cp == 0x00FF)                                     return 0x0178;   // ÿ
  if (cp == 0x00DF)                                     return 0x1E9E;   // ß
  if (cp >= 0x0430 && cp <= 0x044F)                     return cp - 0x20;
  if (cp >= 0x03B1 && cp <= 0x03C9)                     return cp - 0x20;
  if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7)     return cp - 0x20;
  if (cp >= 0x0100 && cp <= 0x0137 && (cp & 1) == 1)    return cp - 1;
  if (cp >= 0x0139 && cp <= 0x0148 && (cp & 1) == 0)    return cp - 1;
  if (cp >= 0x014A && cp <= 0x0177 && (cp & 1) == 1)    return cp - 1;
  if (cp >= 0x0179 && cp <= 0x017E && (cp & 1) == 0)    return cp - 1;
  if (cp == 0x0219 || cp == 0x021B)                     return cp - 1;
  if (cp >= 'a' && cp <= 'z')                           return cp - 0x20;
  return cp;
}

// Uppercase a UTF-8 string into `out` (codepoint by codepoint).
static inline void toUpperUtf8(const char* in, char* out, size_t out_size) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 4 < out_size) o += encode(toUpper(decode(p)), out + o);
  out[o] = '\0';
}

// Number of codepoints / the idx-th codepoint (as its own UTF-8 string).
static inline int utf8Len(const char* s) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s;
  while (*p) { decode(p); n++; }
  return n;
}
static inline void utf8At(const char* s, int idx, char* out /* >= 5 bytes */) {
  const uint8_t* p = (const uint8_t*)s;
  for (int i = 0; *p; i++) {
    const uint8_t* start = p;
    decode(p);
    if (i == idx) { memcpy(out, start, p - start); out[p - start] = '\0'; return; }
  }
  out[0] = '\0';
}

}  // namespace kbd
