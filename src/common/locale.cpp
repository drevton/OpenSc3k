#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "common/locale.h"

#include <assert.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <SDL3/SDL_locale.h>
#endif
#include <SDL3/SDL_stdinc.h>

#include "common/api.h"
#include "common/sc3_error.h"
#include "common/utf8.h"

// Retain the original registered service priority and its startup/shutdown ordering.
enum ScLanguagePriority : i32 {
  kScLanguageServicePriority = -3000000,
};

// Map installed language runtime names to the code pages returned by the Windows CRT locale selection.
// Tagalog has no available CRT locale in the observed runtime and leaves the previous selection unchanged.
u32 ScLanguageCrtCodePage(u32 language) {
  static const u16 pages[] = {1252, 1252, 1252, 1252, 1252, 1252, 1252, 1252, 1252, 1252, 1252,
      1255, 1251, 1252, 932, 1250, 936, 950, 874, 949, 1252, 1256, 1251, 1251, 1251, 1250, 1253,
      1250, 1252, 1250, 1252, 1250, 1250, 1254, 1252, 1252, 1252, 1252, 1252, 1252, 1252, 0, 1258};
  return language >= kScLanguageEnglish && language <= 43 ? pages[language - 1] : 0;
}

// The installed MAIN9/MAIN10/SYSTEM9 sets have the same CP1252 glyph mapping across these Western themes.
u32 ScInstalledWesternTextPage(u32 language) {
  switch (language) {
  case kScLanguageEnglish:
  case kScLanguageBritishEnglish:
  case kScLanguageFrench:
  case kScLanguageGerman:
  case kScLanguageItalian:
  case kScLanguageDutch:
  case kScLanguageSwedish:
    return 1252;
  default:
    return 0;
  }
}

// Decode one legacy resource character without accepting incomplete or invalid byte sequences.
// The Windows conversion uses strict code-page validation; other hosts use their iconv implementation.
u32 ScDecodeLegacyCharacter(u32 page, const char *bytes, u32 length, u32 *scalar) {
  if (!length)
    return 0;
  if (page == 65001)
    return ScUtf8Decode(bytes, length, scalar);
#ifdef _WIN32
  for (u32 count = 1; count <= 2 && count <= length; ++count) {
    wchar_t wide[2];
    int units = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(count), wide, 2);
    if (units == 1 && !(wide[0] >= 0xd800 && wide[0] <= 0xdfff)) {
      if (scalar)
        *scalar = static_cast<u32>(wide[0]);
      return count;
    }
    if (units == 2 && wide[0] >= 0xd800 && wide[0] <= 0xdbff && wide[1] >= 0xdc00 && wide[1] <= 0xdfff) {
      if (scalar)
        *scalar = 0x10000 + ((static_cast<u32>(wide[0]) - 0xd800) << 10) + static_cast<u32>(wide[1]) - 0xdc00;
      return count;
    }
  }
#else
  char encoding[16];
  SDL_snprintf(encoding, sizeof(encoding), "CP%u", page);
  for (u32 count = 1; count <= 2 && count <= length; ++count) {
    SDL_iconv_t converter = SDL_iconv_open("UTF-32LE", encoding);
    if (converter == reinterpret_cast<SDL_iconv_t>(-1))
      return 0;
    const char *input = bytes;
    size_t input_left = count, output_left = 4;
    char decoded[4] = {}, *output = decoded;
    size_t result = SDL_iconv(converter, &input, &input_left, &output, &output_left);
    // CP1255/1258 converters may retain the final scalar until the input sequence is flushed.
    if (!input_left && output_left && result != SDL_ICONV_ERROR && result != SDL_ICONV_E2BIG &&
        result != SDL_ICONV_EILSEQ && result != SDL_ICONV_EINVAL)
      result = SDL_iconv(converter, nullptr, nullptr, &output, &output_left);
    SDL_iconv_close(converter);
    if (result != SDL_ICONV_ERROR && result != SDL_ICONV_E2BIG && result != SDL_ICONV_EILSEQ &&
        result != SDL_ICONV_EINVAL && !input_left && !output_left) {
      const u8 *value = reinterpret_cast<const u8 *>(decoded);
      u32 code = value[0] | (static_cast<u32>(value[1]) << 8) | (static_cast<u32>(value[2]) << 16) |
          (static_cast<u32>(value[3]) << 24);
      if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        return 0;
      if (scalar)
        *scalar = code;
      return count;
    }
  }
#endif
  return 0;
}

// Encode one Unicode scalar only when the selected legacy resource page can represent it exactly.
// Validate the decoded result as well, rejecting platform best-fit substitutions.
u32 ScEncodeLegacyCharacter(u32 page, u32 scalar, char *out) {
  char utf8[4];
  if (!ScUtf8Encode(scalar, utf8))
    return 0;
  if (page == 65001)
    return ScUtf8Encode(scalar, out);
  char encoded[4] = {};
  u32 count = 0;
#ifdef _WIN32
  wchar_t wide[2];
  int units = 1;
  if (scalar < 0x10000)
    wide[0] = static_cast<wchar_t>(scalar);
  else {
    u32 value = scalar - 0x10000;
    wide[0] = static_cast<wchar_t>(0xd800 + (value >> 10));
    wide[1] = static_cast<wchar_t>(0xdc00 + (value & 0x3ff));
    units = 2;
  }
  BOOL substituted = FALSE;
  int written = WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, wide, units, encoded, sizeof(encoded), nullptr, &substituted);
  if (written <= 0 || substituted)
    return 0;
  count = static_cast<u32>(written);
#else
  char encoding[16];
  SDL_snprintf(encoding, sizeof(encoding), "CP%u", page);
  SDL_iconv_t converter = SDL_iconv_open(encoding, "UTF-8");
  if (converter == reinterpret_cast<SDL_iconv_t>(-1))
    return 0;
  const char *input = utf8;
  size_t input_left = ScUtf8Encode(scalar, utf8), output_left = sizeof(encoded);
  char *output = encoded;
  size_t result = SDL_iconv(converter, &input, &input_left, &output, &output_left);
  SDL_iconv_close(converter);
  if (result == SDL_ICONV_ERROR || result == SDL_ICONV_E2BIG || result == SDL_ICONV_EILSEQ ||
      result == SDL_ICONV_EINVAL || input_left)
    return 0;
  count = static_cast<u32>(sizeof(encoded) - output_left);
#endif
  u32 decoded;
  if (!count || ScDecodeLegacyCharacter(page, encoded, count, &decoded) != count || decoded != scalar)
    return 0;
  memcpy(out, encoded, count);
  return count;
}

// Build the entire converted value before publication, so malformed/unrepresentable input cannot expose a partial string.
// A known boundary page is required; the generic stream and internal game strings retain their own byte contracts.
static bool ScConvertLegacyText(u32 page, const ScStringView &source, ScString *out, bool encode) {
  if (!page)
    return false;
  const char *bytes = source.Data();
  u32 length = source.Strlen();
  ScString converted;
  for (u32 offset = 0; offset < length;) {
    u32 scalar, consumed = encode ? ScUtf8Decode(bytes + offset, length - offset, &scalar)
                                 : ScDecodeLegacyCharacter(page, bytes + offset, length - offset, &scalar);
    if (!consumed)
      return false;
    char encoded[4];
    u32 written = encode ? ScEncodeLegacyCharacter(page, scalar, encoded) : ScUtf8Encode(scalar, encoded);
    if (!written)
      return false;
    converted.Append(encoded, written);
    offset += consumed;
  }
  out->FromChar(converted.begin, converted.ScString::Strlen());
  return true;
}

bool ScDecodeLegacyText(u32 page, const ScStringView &source, ScString *out) {
  return ScConvertLegacyText(page, source, out, false);
}

bool ScEncodeLegacyText(u32 page, const ScStringView &source, ScString *out) {
  return ScConvertLegacyText(page, source, out, true);
}


static bool ScLanguageNamesEqual(const ScString &first, const ScString &second) {
  usize length = static_cast<usize>(first.end - first.begin);
  return length == static_cast<usize>(second.end - second.begin) && !memcmp(first.begin, second.begin, length);
}

bool ScLanguageService::GetLanguageLocalName(ScStringView *out, u32 value) {
  static const char *const kLanguageNames[] = {"English", "English",
      "Fran\303\247ais",
      "Deutsch", "Italiano", "Espa\xc3\xb1ol", "Nederlands", "Dansk", "Svensk", "Untranslated", "Suomi", "Untranslated",
      "Untranslated", "Portugues", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "Untranslated", "Untranslated",
      "Untranslated", "Untranslated", "Untranslated"};
  u32 selected = value ? value : language;
  if (selected < 1 || selected > 20) {
    out->FromChar("Untranslated");
    return false;
  }
  out->FromChar(kLanguageNames[selected - 1]);
  return true;
}

// Resolve zero through the current language and assign the original installed byte-string mapping.
bool ScLanguageService::GetISO639LanguageCodeFromLanguageID(ScStringView *out, u32 value) {
  static const char *const names[] = {"en", "uk", "fr", "de", "it", "es", "nl", "da", "sv", "no", "fi", "iw", "ru",
      "pt", "ja", "pl", "zh", "zh", "th", "ko", "hi", "ar", "bg", "ru", "uk", "cs", "el", "hu", "is", "ro", "la", "sk",
      "al", "tr", "fr", "fr", "fr", "gr", "it", "nl", "sp", "tl", "vi"};
  u32 selected = value ? value : language;
  if (selected < 1 || selected > 43) {
    out->FromChar("");
    return false;
  }
  out->FromChar(names[selected - 1]);
  return true;
}

// Resolve zero through the current country and assign the original installed byte-string mapping.
bool ScLanguageService::GetISO3166CountryCodeFromCountryID(ScStringView *out, u32 value) {
  static const char *const names[] = {"al", "ar", "au", "at", "be", "br", "bg", "ca", "cl", "cn", "co", "cu", "cz",
      "dk", "eg", "fi", "fr", "ge", "de", "gr", "", "hk", "hu", "is", "in", "ir", "iq", "ie", "il", "it", "jp", "kr",
      "mx", "nl", "nz", "no", "ph", "pl", "pt", "ro", "ru", "sg", "sk", "si", "es", "se", "ch", "sy", "tw", "th", "tr",
      "ua", "gb", "us", "vn", "yu", ""};
  u32 selected = value ? value : country;
  if (selected < 1 || selected > 57 || selected == 21 || selected == 57) {
    out->FromChar("");
    return false;
  }
  out->FromChar(names[selected - 1]);
  return true;
}

// Resolve zero through the current country and assign the original installed byte-string mapping.
bool ScLanguageService::GetCountryEnglishName(ScStringView *out, u32 value) {
  static const char *const names[] = {"Albania", "Argentina", "Australia", "Austria", "Belgium", "Brazil", "Bulgaria",
      "Canada", "Chile", "China", "Colombia", "Cuba", "Czech Republic", "Denmark", "Egypt", "Finland", "France",
      "Georgia", "Germany", "Greece", "Holy See", "Hong Kong", "Hungray", "Iceland", "India", "Iran", "Iraq", "Ireland",
      "Israel", "Italy", "Japan", "Korea", "Mexico", "Netherlands", "New Zealand", "Norway", "Philippines", "Poland",
      "Portugal", "Romania", "Russia", "Singapore", "Slovakia", "Slovenia", "Spain", "Swedem", "Switzerland", "Syria",
      "Taiwan", "Thailand", "Turkey", "Ukraine", "United Kingdom", "United States", "Vietnam", "Yugoslavia",
      "UnitedStates"};
  u32 selected = value ? value : country;
  if (selected < 1 || selected > 57 || selected == 57) {
    out->FromChar("UnitedStates");
    return false;
  }
  out->FromChar(names[selected - 1]);
  return true;
}

// Normalize zero through current language, then collapse only the original regional language aliases.
u32 ScLanguageService::GetLanguageIDAlias(u32 value) {
  u32 selected = value ? value : language;
  switch (selected) {
  case 2:
    return 1;
  case 35:
  case 36:
  case 37:
    return 3;
  case 38:
    return 4;
  case 39:
    return 5;
  case 40:
    return 7;
  case 41:
    return 6;
  case 23:
  case 24:
  case 25:
    return 13;
  default:
    return selected;
  }
}

// Resolve country zero through current state and retain the original primary-language table, including its zero entries.
u32 ScLanguageService::GetPrimaryLanguageForCountry(u32 value) {
  static const u32 languages[] = {0, 6, 2, 4, 37, 14, 23, 1, 6, 17, 6, 6, 26, 8, 1, 11, 0, 13, 4, 27, 0, 18, 28, 29, 1,
      22, 22, 2, 12, 5, 15, 20, 41, 7, 2, 10, 42, 16, 14, 30, 13, 17, 32, 1, 6, 9, 2, 1, 18, 19, 0, 25, 2, 1, 43, 32};
  u32 selected = value ? value : country;
  return selected >= 1 && selected <= 56 ? languages[selected - 1] : 0;
}

// Resolve language zero through current state and return the original default country or US fallback54.
u32 ScLanguageService::GetDefaultCountryForLanguage(u32 value) {
  static const u32 countries[] = {54, 53, 17, 19, 30, 45, 34, 14, 46, 36, 16, 29, 41, 39, 31, 38, 10, 22, 50, 32, 25,
      26, 7, 41, 52, 13, 20, 23, 24, 40, 30, 43, 1, 51, 47, 8, 5, 47, 47, 5, 33, 37, 55};
  u32 selected = value ? value : language;
  return selected >= 1 && selected <= 43 ? countries[selected - 1] : 54;
}

bool ScLanguageService::GetCountryLocalName(ScStringView *out, u32 value) {
  return GetCountryEnglishName(out, value);
}

bool ScLanguageService::GetLanguageIDFromLanguageEnglishName(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 44; ++candidateId) {
    if (GetLanguageEnglishName(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      utility.ConvertToLowerCase(&candidate);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 1;
  return false;
}

bool ScLanguageService::GetLanguageIDFromLanguageLocalName(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 44; ++candidateId) {
    if (GetLanguageLocalName(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 1;
  return false;
}

bool ScLanguageService::GetISO639LanguageIDFromLanguageCode(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 44; ++candidateId) {
    if (GetISO639LanguageCodeFromLanguageID(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      utility.ConvertToLowerCase(&candidate);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 1;
  return false;
}

bool ScLanguageService::GetISO3166CountryIDFromCountryCode(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 57; ++candidateId) {
    if (GetISO3166CountryCodeFromCountryID(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      utility.ConvertToLowerCase(&candidate);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 54;
  return false;
}

bool ScLanguageService::GetCountryIDFromCountryEnglishName(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 57; ++candidateId) {
    if (GetCountryEnglishName(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      utility.ConvertToLowerCase(&candidate);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 54;
  return false;
}

bool ScLanguageService::GetCountryIDFromCountryLocalName(ScStringView *name, u32 *out) {
  ScString candidate, input(*name);
  utility.ConvertToLowerCase(&input);
  for (u32 candidateId = 1; candidateId <= 57; ++candidateId) {
    if (GetCountryLocalName(&candidate, candidateId)) {
      utility.ConvertToLowerCase(&input);
      if (ScLanguageNamesEqual(input, candidate)) {
        *out = candidateId;
        return true;
      }
    }
  }
  *out = 54;
  return false;
}

// Convert only the original recognized Windows locale IDs; other values fall back to US English1.
u32 ScLanguageService::ConvertLanguageIDToLanguageType(u32 value) {
  switch (value) {
  case 0x0809:
  case 0x0c09:
  case 0x1409:
  case 0x1809:
  case 0x1c09:
  case 0x2009:
  case 0x2409:
  case 0x2809:
  case 0x2c09:
  case 0x3009:
  case 0x3409:
    return kScLanguageBritishEnglish;
  case 0x040c:
  case 0x080c:
  case 0x0c0c:
  case 0x100c:
  case 0x140c:
  case 0x180c:
    return kScLanguageFrench;
  case 0x0407:
  case 0x0807:
  case 0x0c07:
  case 0x1007:
  case 0x1407:
    return kScLanguageGerman;
  case 0x0410:
  case 0x0810:
    return kScLanguageItalian;
  case 0x040a:
  case 0x0c0a:
  case 0x100a:
  case 0x140a:
  case 0x180a:
  case 0x1c0a:
  case 0x200a:
  case 0x240a:
  case 0x280a:
  case 0x2c0a:
  case 0x300a:
  case 0x340a:
  case 0x380a:
  case 0x3c0a:
  case 0x400a:
  case 0x440a:
  case 0x480a:
  case 0x4c0a:
  case 0x500a:
    return kScLanguageSpanish;
  case 0x0413:
  case 0x0813:
    return kScLanguageDutch;
  case 0x0406:
    return kScLanguageDanish;
  case 0x041d:
  case 0x081d:
    return kScLanguageSwedish;
  case 0x0414:
  case 0x0814:
    return kScLanguageNorwegian;
  case 0x040b:
    return kScLanguageFinnish;
  case 0x040d:
    return kScLanguageHebrew;
  case 0x0419:
    return kScLanguageRussian;
  case 0x0416:
  case 0x0816:
    return kScLanguagePortuguese;
  case 0x0411:
    return kScLanguageJapanese;
  case 0x0415:
    return kScLanguagePolish;
  case 0x0804:
  case 0x1004:
  case 0x1404:
    return kScLanguageSimplifiedChinese;
  case 0x0404:
  case 0x0c04:
    return kScLanguageTraditionalChinese;
  case 0x041e:
    return kScLanguageThai;
  case 0x0412:
  case 0x0812:
    return kScLanguageKorean;
  case 0x0439:
    return 21;
  case 0x0401:
  case 0x0801:
  case 0x0c01:
  case 0x1001:
  case 0x1401:
  case 0x1801:
  case 0x1c01:
  case 0x2001:
  case 0x2401:
  case 0x2801:
  case 0x2c01:
  case 0x3001:
  case 0x3401:
  case 0x3801:
  case 0x3c01:
  case 0x4001:
    return 22;
  case 0x0402:
    return 23;
  case 0x0422:
    return 25;
  case 0x0405:
    return 26;
  case 0x0408:
    return 27;
  case 0x040e:
    return 28;
  case 0x040f:
    return 29;
  case 0x0418:
    return 30;
  case 0x041b:
    return 32;
  case 0x041c:
    return 33;
  case 0x041f:
    return 34;
  case 0x080a:
    return 41;
  case 0x042a:
    return 43;
  default:
    return kScLanguageEnglish;
  }
}


#if !defined(_WIN32)

bool ScHostExactLanguageId(const SDL_Locale *locale, u32 *id) {
  if (!locale || !locale->language || !locale->country)
    return false;
  struct Region { const char *language, *country; u16 id; };
  static const Region regions[] = {
#include "common/host_locale_ids.inc"
  };
  for (const Region &region : regions) {
    if (!SDL_strcasecmp(locale->language, region.language) && !SDL_strcasecmp(locale->country, region.country)) {
      *id = region.id;
      return true;
    }
  }
  return false;
}

// Translate supported SDL host locales through the existing Windows language-ID mapping.
// Unknown regional variants retain the existing family default for language-manager normalization.
u32 ScHostLanguageId(const SDL_Locale *locale) {
  u32 exact;
  if (ScHostExactLanguageId(locale, &exact))
    return exact;
  if (!locale || !locale->language)
    return 0x0409;
  const char *language = locale->language, *country = locale->country;
  if (!SDL_strcasecmp(language, "en")) {
    static const char *const british[] = {"GB", "AU", "NZ", "IE", "ZA", "JM", "BZ", "TT", "ZW", "PH"};
    if (country)
      for (const char *region : british)
        if (!SDL_strcasecmp(country, region))
          return 0x0809;
    return 0x0409;
  }
  if (!SDL_strcasecmp(language, "zh") || !SDL_strncasecmp(language, "zh-", 3))
    return !SDL_strcasecmp(language, "zh-Hant") || (country && (!SDL_strcasecmp(country, "Hant") ||
        !SDL_strcasecmp(country, "TW") ||
        !SDL_strcasecmp(country, "HK") ||
        !SDL_strcasecmp(country, "MO"))) ? 0x0404 : 0x0804;
  struct ScHostLanguage { const char *name; u32 id; };
  static const ScHostLanguage languages[] = {{"fr", 0x040c}, {"de", 0x0407}, {"it", 0x0410}, {"es", 0x0c0a},
      {"nl", 0x0413}, {"da", 0x0406}, {"sv", 0x041d}, {"no", 0x0414}, {"nb", 0x0414}, {"nn", 0x0814},
      {"fi", 0x040b}, {"he", 0x040d}, {"iw", 0x040d}, {"ru", 0x0419}, {"pt", 0x0416}, {"ja", 0x0411},
      {"pl", 0x0415}, {"th", 0x041e}, {"ko", 0x0412}, {"hi", 0x0439}, {"ar", 0x0401}, {"bg", 0x0402},
      {"uk", 0x0422}, {"cs", 0x0405}, {"el", 0x0408}, {"hu", 0x040e}, {"is", 0x040f}, {"ro", 0x0418},
      {"sk", 0x041b}, {"sq", 0x041c}, {"tr", 0x041f}, {"vi", 0x042a}};
  for (const ScHostLanguage &candidate : languages)
    if (!SDL_strcasecmp(language, candidate.name))
      return candidate.id;
  return 0x0409;
}
#endif


ScLanguageService::ScLanguageService()
    : ScServicesBase(kScLanguageManagerServiceId, kScLanguageServicePriority), state(0), language(1), country(54) {
}

ScLanguageService::~ScLanguageService() {
  Shutdown();
}

// Publish only the original manager/service identities and retain through the complete manager's live AddRef.
bool ScLanguageService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  HANDLE_QUERY_INTERFACE(IID_ScLanguageManager, ScLanguageManager);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScLanguageService::AddRef() {
  return ScServicesBase::AddRef();
}

u32 ScLanguageService::Release() {
  return ScServicesBase::Release();
}

bool ScLanguageService::Init() {
  return true;
}

bool ScLanguageService::Shutdown() {
  return true;
}

void ScLanguageService::AddAvailableLanguage(u32 value) {
  if (value < 44)
    available.insert(value);
}

void ScLanguageService::RemoveAvailableLanguage(u32 value) {
  available.erase(value);
}

u32 ScLanguageService::GetCurrentLanguage() {
  return language;
}

bool ScLanguageService::CanSwitchToLanguage(u32 value) {
  return available.find(value) != available.end();
}

// Convert the operating system user locale through the manager live conversion method.
u32 ScLanguageService::GetCurrentSystemLanguage() {
#if defined(_WIN32)
  return ConvertLanguageIDToLanguageType(static_cast<u16>(GetUserDefaultLCID()));
#else
  SDL_Locale **locales = SDL_GetPreferredLocales(nullptr);
  u32 id = ScHostLanguageId(locales ? locales[0] : nullptr);
  SDL_free(locales);
  return ConvertLanguageIDToLanguageType(id);
#endif
}

bool ScLanguageService::GetLanguageDirectoryName(ScStringView *out, u32 value) {
  return GetLanguageEnglishName(out, value);
}

// Dispatch through the live manager with a copied name, copying its normalized text back only on success.
bool ScLanguageService::GetLanguageIDFromEitherEnglishOrLocalName(ScStringView *, u32 *) {
  // The installed default table re-enters this same slot with no lookup of its own, so the original never terminates.
  // Nothing in the game calls it; refuse explicitly instead of reproducing unbounded recursion.
  ScNotImpl("GetLanguageIDFromEitherEnglishOrLocalName: the original default re-enters itself without a lookup");
}

bool ScLanguageService::GetLanguageEnglishNameFromLanguageID(ScStringView *out, u32 value) {
  return GetLanguageEnglishName(out, value);
}

bool ScLanguageService::GetLanguageLocalNameFromLanguageID(ScStringView *out, u32 value) {
  return GetLanguageLocalName(out, value);
}

u32 ScLanguageService::GetCurrentCountry() {
  return country;
}

bool ScLanguageService::SetCurrentCountry(u32 value) {
  ScString temporary;
  country = value ? value : 54;
  utility.country = country;
  return true;
}

u32 ScLanguageService::GetCurrentSystemCountry() {
  return UINT32_MAX;
}

bool ScLanguageService::CanSwitchToCountry(i32) {
  return true;
}

// Return the borrowed embedded utility regardless of the requested language, without acquiring a reference.
ScLanguageUtility *ScLanguageService::GetLanguageUtility(u32) {
  return &utility;
}

// Create an independently referenced utility and apply the original concrete language store.
ScLanguageUtility *ScLanguageService::GetNewLanguageUtility(u32 value) {
  ScLocaleUtility *result = new ScLocaleUtility;
  result->AddRef();
  result->language = value;
  return result;
}

f32 ScLanguageService::GetExchangeRateForLocale(u32) {
  return 1.0f;
}

void ScLanguageService::SetExchangeRateForLocale(f32, u32) {
}

bool ScLanguageService::ConvertMoneyFromBaseToLocale(i32 value, i32 *out, u32) {
  *out = value;
  return true;
}

bool ScLanguageService::ConvertMoneyFromBaseToLocale(i64 value, i64 *out, u32) {
  *out = value;
  return true;
}

bool ScLanguageService::ConvertMoneyFromLocaleToLocale(i32 value, u32, i32 *out, u32) {
  *out = value;
  return true;
}

bool ScLanguageService::ConvertMoneyFromLocaleToLocale(i64 value, u32, i64 *out, u32) {
  *out = value;
  return true;
}

// Normalize zero to English, then update selected state before asking for its CRT locale name. SDL production does not
// select a process-wide byte code page.
bool ScLanguageService::SetCurrentLanguage(u32 value) {
  ScString locale;
  u32 selected = value ? value : 1;
  if (selected == language)
    return true;
  if (!CanSwitchToLanguage(selected))
    return false;
  language = selected;
  utility.language = selected;
  if (!GetLanguageRuntimeLibraryName(&locale, selected))
    return false;
  return true;
}

bool ScLanguageService::GetNextAvailableLanguage(u32 *value) {
  u32 initial = *value;
  if (initial >= 44)
    return false;
  do {
    ++*value;
    if (*value >= 44)
      *value = 1;
    if (CanSwitchToLanguage(*value))
      break;
  } while (*value != initial);
  return initial != *value;
}

// Resolve zero through current state and assign the original directory literal; unknown IDs clear the output.
bool ScLanguageService::GetLanguageEnglishName(ScStringView *out, u32 value) {
  static const char *const names[] = {"", "English", "English-UK", "French", "German", "Italian", "Spanish", "Dutch",
      "Danish", "Swedish", "Norwegian", "Finnish", "Hebrew", "Russian", "Portuguese", "Japanese", "Polish",
      "SimplifiedChinese", "TraditionalChinese", "Thai", "Korean"};
  u32 selected = value ? value : language;
  bool found = selected >= 1 && selected <= 20;
  out->FromChar(found ? names[selected] : "");
  return found;
}

// Return the original MSVCRT locale identifier for IDs1..43; an unknown ID leaves output untouched.
bool ScLanguageService::GetLanguageRuntimeLibraryName(ScStringView *out, u32 value) {
  static const char *const names[] = {"", "english-us", "english-uk", "french", "german", "italian", "spanish", "dutch",
      "danish", "swedish", "norwegian", "finnish", "hebrew", "russian", "portuguese", "japanese", "polish",
      "chinese-simplified", "chinese-traditional", "thai", "korean", "hindi", "arabic", "russian", "russian", "russian",
      "czech", "greek", "hungarian", "icelandic", "romanian", "latin", "slovak", "albanian", "turkish", "french-swiss",
      "french-canadian", "french-belgian", "german-swiss", "italian-swiss", "dutch-belgian", "spanish-mexican",
      "tagalog", "vietnamese"};
  u32 selected = value ? value : language;
  if (selected < 1 || selected > 43)
    return false;
  out->FromChar(names[selected]);
  return true;
}

bool ScLocaleUtility::GetDayName(u8 index, ScStringView *out) {
  static const char *const kDayNames[20][7] = {
      {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"},
      {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"},
      {"Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"},
      {"Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"},
      {"Luned\xc3\xac", "Marted\xc3\xac", "Mercoled\xc3\xac", "Gioved\xc3\xac", "Venerd\xc3\xac", "Sabato", "Domenica"},
      {"Lunes", "Martes", "Mi\xc3\xa9rcoles", "Jueves", "Viernes",
          "S\xc3\xa1"
          "bado",
          "Domingo"},
      {"Maandag", "Dinsdag", "Woensdag", "Donderdag", "Vrijdag", "Zaterdag", "Zondag"},
      {"mandag", "tirsdag", "onsdag", "torsdag", "fredag", "l\xc3\xb8rdag", "s\xc3\xb8ndag"},
      {"M\xc3\xa5ndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "L\xc3\xb6rdag", "S\xc3\xb6ndag"},
      {"mandag", "tirsdag", "onsdag", "torsdag", "fredag", "l\xc3\xb8rdag", "s\xc3\xb8ntag"},
      {"Maanantai", "Tiistai", "Keskiviikko", "Torstai", "Perjantai", "Lauantai", "Sunnuntai"},
      {"\xc2\xb4\xd7\x91\xd7\x9d\xd7\x95\xd7\x99", "\xc2\xb4\xd7\x92\xd7\x9d\xd7\x95\xd7\x99",
          "\xc2\xb4\xd7\x93\xd7\x9d\xd7\x95\xd7\x99", "\xc2\xb4\xd7\x94\xd7\x9d\xd7\x95\xd7\x99",
          "\xc2\xb4\xd7\x95\xd7\x9d\xd7\x95\xd7\x99", "\xd7\xaa\xd7\x91\xd7\xa9",
          "\xc2\xb4\xd7\x90\xd7\x9d\xd7\x95\xd7\x99"},
      {"\xd0\xbf\xd0\xbe\xd0\xbd\xd0\xb5\xd0\xb4\xd0\xb5\xd0\xbb\xd1\x8c\xd0\xbd\xd0\xb8\xd0\xba",
          "\xd0\xb2\xd1\x82\xd0\xbe\xd1\x80\xd0\xbd\xd0\xb8\xd0\xba", "\xd1\x81\xd1\x80\xd0\xb5\xd0\xb4\xd0\xb0",
          "\xd1\x87\xd0\xb5\xd1\x82\xd0\xb2\xd0\xb5\xd1\x80\xd0\xb3",
          "\xd0\xbf\xd1\x8f\xd1\x82\xd0\xbd\xd0\xb8\xd1\x86\xd0\xb0",
          "\xd1\x81\xd1\x83\xd0\xb1\xd0\xb1\xd0\xbe\xd1\x82\xd0\xb0",
          "\xd0\xb2\xd0\xbe\xd1\x81\xd0\xba\xd1\x80\xd0\xb5\xd1\x81\xd0\xb5\xd0\xbd\xd1\x8c\xd0\xb5"},
      {"Segunda-feira",
          "Ter\xc3\xa7"
          "a-feira",
          "Quarta-feira", "Quinta-feira", "Sexta-feira",
          "S\xc3\xa1"
          "bado",
          "Domingo"},
      {"\xe6\x9c\x88\xe6\x9b\x9c", "\xe7\x81\xab\xe6\x9b\x9c", "\xe6\xb0\xb4\xe6\x9b\x9c", "\xe6\x9c\xa8\xe6\x9b\x9c",
          "\xe9\x87\x91\xe6\x9b\x9c", "\xe5\x9c\x9f\xe6\x9b\x9c", "\xe6\x97\xa5\xe6\x9b\x9c"},
      {"Poniedzia\xc5\x82"
       "ek",
          "Wtorek", "\xc5\x9aroda", "Czwartek", "Pi\xc4\x85tek", "Sobota", "Niedziela"},
      {"\xe6\x98\x9f\xe6\x9c\x9f\xe4\xb8\x80", "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xba\x8c",
          "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xb8\x89", "\xe6\x98\x9f\xe6\x9c\x9f\xe5\x9b\x9b",
          "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xba\x94", "\xe6\x98\x9f\xe6\x9c\x9f\xe5\x85\xad",
          "\xe6\x98\x9f\xe6\x9c\x9f\xe6\x97\xa5"},
      {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"},
      {"\xe0\xb8\x88", "\xe0\xb8\xad", "\xe0\xb8\x9e", "\xe0\xb8\x9e\xe0\xb8\xa4", "\xe0\xb8\xa8", "\xe0\xb8\xaa",
          "\xe0\xb8\xad\xe0\xb8\xb2"},
      {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"}};
  if (language < kScLanguageEnglish || language > kScLanguageKorean) {
    out->FromChar("untranslated");
    return false;
  }
  if (index >= 1 && index <= 7)
    out->FromChar(kDayNames[language - 1][index - 1]);
  return true;
}

bool ScLocaleUtility::GetAbbrDayName(u8 index, ScStringView *out) {
  static const char *const kAbbrDayNames[20][7] = {{"Mon", "Tue", "Wed", "Thur", "Fri", "Sat", "Sun"},
      {"Mon", "Tue", "Wed", "Thur", "Fri", "Sat", "Sun"}, {"lun", "mar", "mer", "jeu", "ven", "sam", "dim"},
      {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"}, {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"},
      {"L", "M", "X", "J", "V", "S", "D"}, {"Ma", "Di", "Wo", "Do", "Vr", "Za", "Zo"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"M\xc3\xa5n", "Tis", "Ons", "Tor", "Fre", "L\xc3\xb6r", "S\xc3\xb6n"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"ma", "ti", "ke", "to", "pe", "la", "su"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"\xd0\x9f\xd0\xbd", "\xd0\x92\xd1\x82", "\xd0\xa1\xd1\x80", "\xd0\xa7\xd1\x82", "\xd0\x9f\xd1\x82",
          "\xd0\xa1\xd0\xb1", "\xd0\x92\xd1\x81"},
      {"Seg", "Ter", "Qua", "Qui", "Sex", "Sab", "Dom"},
      {"\xe6\x9c\x88", "\xe7\x81\xab", "\xe6\xb0\xb4", "\xe6\x9c\xa8", "\xe9\x87\x91", "\xe5\x9c\x9f", "\xe6\x97\xa5"},
      {"Pn", "Wt", "\xc5\x9ar", "Cz", "Pt", "So", "N"},
      {"\xe4\xb8\x80", "\xe4\xba\x8c", "\xe4\xb8\x89", "\xe5\x9b\x9b", "\xe4\xba\x94", "\xe5\x85\xad", "\xe6\x97\xa5"},
      {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"},
      {"\xe0\xb8\x88", "\xe0\xb8\xad", "\xe0\xb8\x9e", "\xe0\xb8\x9e\xe0\xb8\xa4", "\xe0\xb8\xa8", "\xe0\xb8\xaa",
          "\xe0\xb8\xad\xe0\xb8\xb2"},
      {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}};
  if (language < kScLanguageEnglish || language > kScLanguageKorean || language == kScLanguageDanish ||
      language == kScLanguageNorwegian || language == kScLanguageHebrew) {
    out->FromChar("untranslated");
    return false;
  }
  if (index >= 1 && index <= 7)
    out->FromChar(kAbbrDayNames[language - 1][index - 1]);
  return true;
}

bool ScLocaleUtility::GetMonthName(u8 index, ScStringView *out) {
  static const char *const kMonthNames[20][12] = {{"January", "February", "March", "April", "May", "June", "July",
                                                      "August", "September", "October", "November", "December"},
      {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November",
          "December"},
      {"janvier", "f\xc3\xa9vrier", "mars", "avril", "mai", "juin", "juillet", "ao\xc3\xbbt", "septembre", "octobre",
          "novembre",
          "d\xc3\xa9"
          "cembre"},
      {"Januar", "Februar", "M\xc3\xa4rz", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November",
          "Dezember"},
      {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre",
          "Novembre", "Dicembre"},
      {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre",
          "Diciembre"},
      {"Januari", "Februari", "Maart", "April", "Mei", "Juni", "Juli", "Augustus", "September", "Oktober", "November",
          "December"},
      {"januar", "februar", "marts", "april", "maj", "juni", "juli", "august", "september", "oktober", "november",
          "december"},
      {"Januari", "Februari", "Mars", "April", "Maj", "Juni", "Juli", "Augusti", "September", "Oktober", "November",
          "December"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"tamikuu", "helmikuu", "maaliskuu", "huhtikuu", "toukokuu", "kes\xc3\xa4kuu", "hein\xc3\xa4kuu", "elokuu",
          "syyskuu", "lokakuu", "marraskuu", "joulukuu"},
      {"\xd7\x99\xd7\x9a\xd7\xa9\xd7\xaa", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated"},
      {"\xd1\x8f\xd0\xbd\xd0\xb2\xd0\xb0\xd1\x80\xd1\x8c", "\xd1\x84\xd0\xb5\xd0\xb2\xd1\x80\xd0\xb0\xd0\xbb\xd1\x8c",
          "\xd0\xbc\xd0\xb0\xd1\x80\xd1\x82", "\xd0\xb0\xd0\xbf\xd1\x80\xd0\xb5\xd0\xbb\xd1\x8c",
          "\xd0\xbc\xd0\xb0\xd0\xb9", "\xd0\xb8\xd1\x8e\xd0\xbd\xd1\x8c", "\xd0\xb8\xd1\x8e\xd0\xbb\xd1\x8c",
          "\xd0\xb0\xd0\xb2\xd0\xb3\xd1\x83\xd1\x81\xd1\x82",
          "\xd1\x81\xd0\xb5\xd0\xbd\xd1\x82\xd1\x8f\xd0\xb1\xd1\x80\xd1\x8c",
          "\xd0\xbe\xd0\xba\xd1\x82\xd1\x8f\xd0\xb1\xd1\x80\xd1\x8c",
          "\xd0\xbd\xd0\xbe\xd1\x8f\xd0\xb1\xd1\x80\xd1\x8c",
          "\xd0\xb4\xd0\xb5\xd0\xba\xd0\xb0\xd0\xb1\xd1\x80\xd1\x8c"},
      {"Janeiro", "Fevereiro", "Mar\xc3\xa7o", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro", "Outubro",
          "Novembro", "Dezembro"},
      {"1\xe6\x9c\x88", "2\xe6\x9c\x88", "3\xe6\x9c\x88", "4\xe6\x9c\x88", "5\xe6\x9c\x88", "6\xe6\x9c\x88",
          "7\xe6\x9c\x88", "8\xe6\x9c\x88", "9\xe6\x9c\x88", "10\xe6\x9c\x88", "11\xe6\x9c\x88", "12\xe6\x9c\x88"},
      {"Stycze\xc5\x84", "Luty", "Marzec", "Kwiecie\xc5\x84", "Maj", "Czerwiec", "Lipiec", "Sierpie\xc5\x84",
          "Wrzesie\xc5\x84",
          "Pa\xc5\xba"
          "dziernik",
          "Listopad", "Grudzie\xc5\x84"},
      {"1\xe6\x9c\x88", "2\xe6\x9c\x88", "3\xe6\x9c\x88", "4\xe6\x9c\x88", "5\xe6\x9c\x88", "6\xe6\x9c\x88",
          "7\xe6\x9c\x88", "8\xe6\x9c\x88", "9\xe6\x9c\x88", "10\xe6\x9c\x88", "11\xe6\x9c\x88", "12\xe6\x9c\x88"},
      {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November",
          "December"},
      {"\xe0\xb8\xa1\xe0\xb8\x81\xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x81\xe0\xb8\xb8\xe0\xb8\xa1\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\x9e\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\x98"
          "\xe0\xb9\x8c",
          "\xe0\xb8\xa1\xe0\xb8\xb5\xe0\xb8\x99\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb9\x80\xe0\xb8\xa1\xe0\xb8\xa9\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x9e\xe0\xb8\xa4\xe0\xb8\xa9\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\xa1\xe0\xb8\xb4\xe0\xb8\x96\xe0\xb8\xb8\xe0\xb8\x99\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x81\xe0\xb8\xa3\xe0\xb8\x81\xe0\xb8\x8e\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\xaa\xe0\xb8\xb4\xe0\xb8\x87\xe0\xb8\xab\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x81\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\xa2\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x95\xe0\xb8\xb8\xe0\xb8\xa5\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x9e\xe0\xb8\xa4\xe0\xb8\xa8\xe0\xb8\x88\xe0\xb8\xb4\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8"
          "\x99",
          "\xe0\xb8\x98\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\xa7\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1"},
      {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November",
          "December"}};
  if (language < kScLanguageEnglish || language > kScLanguageKorean || language == kScLanguageNorwegian) {
    out->FromChar("untranslated");
    return false;
  }
  if (index >= 1 && index <= 12)
    out->FromChar(kMonthNames[language - 1][index - 1]);
  return true;
}

bool ScLocaleUtility::GetAbbrMonthName(u8 index, ScStringView *out) {
  static const char *const kAbbrMonthNames[20][12] = {
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"jan", "f\xc3\xa9v", "mar", "Avr", "mai", "juin", "juil", "ao\xc3\xbbt", "sept", "oct", "nov",
          "d\xc3\xa9"
          "c"},
      {"Jan", "Feb", "M\xc3\xa4r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"},
      {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"},
      {"En", "Feb", "Mar", "Abr", "Mayo", "Jun", "Jul", "Ag", "Sep", "Oct", "Nov", "Dic"},
      {"Jan", "Feb", "Mrt", "Apr", "Mei", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dec"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"Jan", "Feb", "Mar", "Apr", "Maj", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dec"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated", "untranslated",
          "untranslated", "untranslated", "untranslated", "untranslated", "untranslated"},
      {"\xd1\x8f\xd0\xbd\xd0\xb2", "\xd1\x84\xd0\xb5\xd0\xb2", "\xd0\xbc\xd0\xb0\xd1\x80", "\xd0\xb0\xd0\xbf\xd1\x80",
          "\xd0\xbc\xd0\xb0\xd0\xb9", "\xd0\xb8\xd1\x8e\xd0\xbd", "\xd0\xb8\xd1\x8e\xd0\xbb",
          "\xd0\xb0\xd0\xb2\xd0\xb3", "\xd1\x81\xd0\xb5\xd0\xbd", "\xd0\xbe\xd0\xba\xd1\x82",
          "\xd0\xbd\xd0\xbe\xd1\x8f", "\xd0\xb4\xd0\xb5\xd0\xba"},
      {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez"},
      {"1\xe6\x9c\x88", "2\xe6\x9c\x88", "3\xe6\x9c\x88", "4\xe6\x9c\x88", "5\xe6\x9c\x88", "6\xe6\x9c\x88",
          "7\xe6\x9c\x88", "8\xe6\x9c\x88", "9\xe6\x9c\x88", "10\xe6\x9c\x88", "11\xe6\x9c\x88", "12\xe6\x9c\x88"},
      {"Sty", "Lut", "Mar", "Kwi", "Maj", "Cze", "Lip", "Sie", "Wrz", "Pa\xc5\xba", "Lis", "Gru"},
      {"1\xe6\x9c\x88", "2\xe6\x9c\x88", "3\xe6\x9c\x88", "4\xe6\x9c\x88", "5\xe6\x9c\x88", "6\xe6\x9c\x88",
          "7\xe6\x9c\x88", "8\xe6\x9c\x88", "9\xe6\x9c\x88", "10\xe6\x9c\x88", "11\xe6\x9c\x88", "12\xe6\x9c\x88"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
      {"\xe0\xb8\xa1\xe0\xb8\x81\xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x81\xe0\xb8\xb8\xe0\xb8\xa1\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\x9e\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\x98"
          "\xe0\xb9\x8c",
          "\xe0\xb8\xa1\xe0\xb8\xb5\xe0\xb8\x99\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb9\x80\xe0\xb8\xa1\xe0\xb8\xa9\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x9e\xe0\xb8\xa4\xe0\xb8\xa9\xe0\xb8\xa0\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\xa1\xe0\xb8\xb4\xe0\xb8\x96\xe0\xb8\xb8\xe0\xb8\x99\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x81\xe0\xb8\xa3\xe0\xb8\x81\xe0\xb8\x8e\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\xaa\xe0\xb8\xb4\xe0\xb8\x87\xe0\xb8\xab\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x81\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\xa2\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8\x99",
          "\xe0\xb8\x95\xe0\xb8\xb8\xe0\xb8\xa5\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1",
          "\xe0\xb8\x9e\xe0\xb8\xa4\xe0\xb8\xa8\xe0\xb8\x88\xe0\xb8\xb4\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa2\xe0\xb8"
          "\x99",
          "\xe0\xb8\x98\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\xa7\xe0\xb8\xb2\xe0\xb8\x84\xe0\xb8\xa1"},
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}};
  if (language < kScLanguageEnglish || language > kScLanguageKorean || language == kScLanguageDanish ||
      language == kScLanguageNorwegian || language == kScLanguageFinnish || language == kScLanguageHebrew) {
    out->FromChar("untranslated");
    return false;
  }
  if (index >= 1 && index <= 12)
    out->FromChar(kAbbrMonthNames[language - 1][index - 1]);
  return true;
}

// Append the original left-to-right groups, retiring each copied digit slice before appending its separator.
// This is native String storage plumbing; it does not introduce another game formatting contract.
static void ScAppendDigitGroups(const ScString &digits, const ScString &separator, ScString *out) {
  u32 length = static_cast<u32>(digits.end - digits.begin), remaining = length;
  if (length > 3) {
    do {
      u32 count = remaining % 3;
      if (!count)
        count = 3;
      {
        ScString part = digits.Mid(length - remaining, count);
        out->Append(part.begin, static_cast<u32>(part.end - part.begin));
      }
      out->Append(separator.begin, static_cast<u32>(separator.end - separator.begin));
      remaining -= count;
    } while (remaining > 3);
  }
  ScString part = digits.Right(remaining);
  out->Append(part.begin, static_cast<u32>(part.end - part.begin));
}

// Format grouped magnitude before the live currency queries, then apply the original language-specific negative branch.
// Danish, Norwegian and Hebrew negative paths transiently publish the symbol before replacing it with sign/digit text.
bool ScLocaleUtility::MakeMoneyString(i64 value, ScStringView *out) {
  ScString digits, separator(","), grouped, decorated, symbol, minus("-"), result;
  bool negative = value < 0;
  u64 magnitude = negative ? u64(0) - static_cast<u64>(value) : static_cast<u64>(value);
  digits.Sprintf("%" SC3_PRId64, static_cast<i64>(magnitude));
  GetThousandSeparator(&separator);
  ScAppendDigitGroups(digits, separator, &grouped);
  if (DoesCurrencySymbolPrecedeAmount()) {
    GetCurrencySymbol(&decorated);
    if (IsSpaceBetweenCurrencySymbolAndAmount())
      decorated.Append(" ", 1);
    decorated.Append(grouped.begin, static_cast<u32>(grouped.end - grouped.begin));
  } else {
    decorated = grouped;
    if (IsSpaceBetweenCurrencySymbolAndAmount())
      decorated.Append(" ", 1);
    GetCurrencySymbol(&symbol);
    decorated.Append(symbol.begin, static_cast<u32>(symbol.end - symbol.begin));
  }
  if (!negative) {
    result = decorated;
  } else {
    u32 selected = language;
    if (selected == 8 || selected == 10 || selected == 12) {
      GetCurrencySymbol(out);
      if (selected != 10)
        result.Append(" ", 1);
      result.Append(minus.begin, static_cast<u32>(minus.end - minus.begin));
      result.Append(grouped.begin, static_cast<u32>(grouped.end - grouped.begin));
    } else {
      result = minus;
      result.Append(decorated.begin, static_cast<u32>(decorated.end - decorated.begin));
    }
  }
  out->FromChar(result.begin);
  return true;
}

// Group the original signed magnitude and preserve the unusual Finnish trailing minus and unsupported negative empties.
// Retain the original unused temporary's construction order alongside the separator, grouped text and minus.
bool ScLocaleUtility::MakeNumberString(i64 value, ScStringView *out) {
  ScString digits, separator(","), grouped, unused, minus("-"), result;
  bool negative = value < 0;
  u64 magnitude = negative ? u64(0) - static_cast<u64>(value) : static_cast<u64>(value);
  digits.Sprintf("%" SC3_PRId64, static_cast<i64>(magnitude));
  GetThousandSeparator(&separator);
  ScAppendDigitGroups(digits, separator, &grouped);
  if (!negative) {
    result = grouped;
  } else {
    u32 selected = language;
    if (selected == 11) {
      result = grouped;
      result.Append(minus.begin, static_cast<u32>(minus.end - minus.begin));
    } else if (selected >= 1 && selected <= 20 && selected != 14) {
      result = minus;
      result.Append(grouped.begin, static_cast<u32>(grouped.end - grouped.begin));
    }
  }
  out->FromChar(result.begin);
  return true;
}


ScLocaleUtility::ScLocaleUtility() : references(0), language(1), country(54), localCurrency(0) {
}

ScLocaleUtility::~ScLocaleUtility() {
}

bool ScLocaleUtility::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScLocaleUtility, ScLocaleUtility);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScLocaleUtility);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScLocaleUtility::AddRef() {
  return ++references;
}

// Remove a reference and destroy the complete utility only on the transition from one to zero.
u32 ScLocaleUtility::Release() {
  u32 previous = references--;
  if (previous == 1) {
    delete this;
    return 0;
  }
  return references;
}

bool ScLocaleUtility::Init() {
  return true;
}

bool ScLocaleUtility::Shutdown() {
  return true;
}

// Format the original twelve-hour or twenty-four-hour clock, ignoring seconds and retaining its language-specific colon.
bool ScLocaleUtility::MakeTimeString(i32 hour, i32 minute, i32, ScStringView *out) {
  ScString text;
  if (language == kScLanguageEnglish || language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageThai) {
    const char *period = hour < 12 ? "am" : "pm";
    i32 displayed = hour == 0 ? 12 : hour > 12 ? hour - 12 : hour;
    text.Sprintf("%2u:%.2u %s", static_cast<u32>(displayed), static_cast<u32>(minute), period);
  } else {
    switch (language) {
    case kScLanguageItalian:
    case kScLanguageDanish:
    case kScLanguageSwedish:
    case kScLanguageFinnish:
    case 28:
    case 35:
    case 38:
    case 39:
      text.Sprintf("%2u%.2u", static_cast<u32>(hour), static_cast<u32>(minute));
      break;
    default:
      text.Sprintf("%2u:%.2u", static_cast<u32>(hour), static_cast<u32>(minute));
      break;
    }
  }
  out->FromChar(text.begin);
  return true;
}

// Fetch both month-name forms before checking the format, then compose the original language-specific date layout.
// Failed month lookups do not prevent formatting; an unknown format leaves the output untouched.
bool ScLocaleUtility::MakeDateString(i32 month, i32 day, u32 year, ScStringView *out, DateFormat format) {
  ScString text, fullName, shortName;
  GetMonthName(static_cast<u8>(month), &fullName);
  GetAbbrMonthName(static_cast<u8>(month), &shortName);
  u32 selected = language, first = static_cast<u32>(month), second = static_cast<u32>(day);
  bool japanese = selected == 15, yearFirst = selected == 17 || selected == 18 || selected == 20;
  bool dayFirst = selected == 2 || (selected >= 3 && selected <= 11) || selected == 13 || selected == 14 ||
      selected == 16 || selected == 19;
  char separator = '/';
  if (selected == 7 || selected == 8 || selected == 14)
    separator = '-';
  else if (selected == 3 || selected == 4 || selected == 9 || selected == 10 || selected == 11 || selected == 16)
    separator = '.';
  switch (format) {
  case DateFormat::NumericMonthDayYear:
    if (japanese)
      text.Sprintf("%4u/%u/%u", year, first, second);
    else if (yearFirst)
      text.Sprintf("%4u-%u-%u", year, first, second);
    else {
      const char *pattern = separator == '-' ? "%u-%u-%4u" : separator == '.' ? "%u.%u.%4u" : "%u/%u/%4u";
      text.Sprintf(pattern, dayFirst ? second : first, dayFirst ? first : second, year);
    }
    break;
  case DateFormat::NumericMonthYear:
    if (japanese)
      text.Sprintf("%4u/%u", year, first);
    else if (yearFirst)
      text.Sprintf("%4u-%u", year, first);
    else {
      const char *pattern = separator == '-' ? "%u-%4u" : separator == '.' ? "%u.%4u" : "%u/%4u";
      text.Sprintf(pattern, first, year);
    }
    break;
  case DateFormat::AlphaMonthDayYear:
    if (japanese)
      text.Sprintf("%4u.%s.%u", year, fullName.begin, second);
    else if (yearFirst)
      text.Sprintf("%4u %s %u", year, fullName.begin, second);
    else if (dayFirst)
      text.Sprintf("%u %s, %4u", second, fullName.begin, year);
    else
      text.Sprintf("%s %u, %4u", fullName.begin, second, year);
    break;
  case DateFormat::AlphaMonthYear:
    if (japanese)
      text.Sprintf("%4u.%s", year, fullName.begin);
    else if (yearFirst)
      text.Sprintf("%4u %s", year, fullName.begin);
    else
      text.Sprintf("%s %4u", fullName.begin, year);
    break;
  case DateFormat::AbbrevAlphaMonthDayYear:
    if (japanese)
      text.Sprintf("%4u.%s.%u", year, shortName.begin, second);
    else if (yearFirst)
      text.Sprintf("%4u %s %u", year, shortName.begin, second);
    else if (dayFirst)
      text.Sprintf("%u %s., %4u", second, shortName.begin, year);
    else
      text.Sprintf("%s. %u, %4u", shortName.begin, second, year);
    break;
  case DateFormat::AbbrevAlphaMonthYear:
    if (japanese)
      text.Sprintf("%4u.%s", year, shortName.begin);
    else if (yearFirst)
      text.Sprintf("%4u %s", year, shortName.begin);
    else
      text.Sprintf("%s. %4u", shortName.begin, year);
    break;
  default:
    return false;
  }
  out->FromChar(text.begin);
  return true;
}

// Use the default dollar symbol unless local currency is enabled; publish complete UTF-8 currency text.
bool ScLocaleUtility::GetCurrencySymbol(ScStringView *out) {
  const char *symbol = "$";
  if (localCurrency) {
    switch (language) {
    case kScLanguageEnglish:
      break;
    case kScLanguageBritishEnglish:
      symbol = "\xc2\xa3";
      break;
    case kScLanguageFrench:
    case kScLanguageDutch:
      symbol = "F";
      break;
    case kScLanguageGerman:
      symbol = "DM";
      break;
    case kScLanguageItalian:
      symbol = "L.";
      break;
    case kScLanguageSpanish:
      symbol = "Pts";
      break;
    case kScLanguageDanish:
    case kScLanguageSwedish:
    case kScLanguageNorwegian:
    case 29:
      symbol = "kr";
      break;
    case kScLanguageFinnish:
      symbol = "mk";
      break;
    case kScLanguageHebrew:
      symbol = "NIS";
      break;
    case kScLanguageRussian:
      symbol = "py";
      break;
    case kScLanguagePortuguese:
      symbol = "Esc.";
      break;
    case kScLanguageJapanese:
      // CP932 U+FF65 selects the original raw glyph slot0xA5; its asset-specific visual meaning is unverified.
      symbol = "\xef\xbd\xa5";
      break;
    case kScLanguagePolish:
      symbol = "Z";
      break;
    case kScLanguageSimplifiedChinese:
    case kScLanguageTraditionalChinese:
      symbol = "RMB";
      break;
    case kScLanguageThai:
      symbol = "\xe0\xb8\xbf";
      break;
    case kScLanguageKorean:
      symbol = "\\";
      break;
    case 26:
      symbol = "K";
      break;
    case 27:
      symbol = "Drs";
      break;
    case 28:
      symbol = "FT";
      break;
    case 30:
      symbol = "Lei";
      break;
    case 33:
      symbol = "Lek";
      break;
    case 34:
      symbol = "TL";
      break;
    default:
      out->FromChar("untranslated");
      return false;
    }
  }
  out->FromChar(symbol);
  return true;
}

// Select the original symbol placement independently of whether local currency symbols are enabled.
bool ScLocaleUtility::DoesCurrencySymbolPrecedeAmount() {
  return language != kScLanguageFrench && language != kScLanguageGerman && language != kScLanguageSwedish &&
      language != kScLanguageFinnish && language != kScLanguagePortuguese && language != kScLanguagePolish &&
      language != 29;
}

// Return the original language-specific currency spacing rule without consulting the symbol-enable flag.
bool ScLocaleUtility::IsSpaceBetweenCurrencySymbolAndAmount() {
  return language != kScLanguageEnglish && language != kScLanguageBritishEnglish && language != kScLanguageNorwegian &&
      language != kScLanguageHebrew && language != kScLanguageJapanese && language != kScLanguageSimplifiedChinese &&
      language != kScLanguageTraditionalChinese && language != kScLanguageThai && language != kScLanguageKorean;
}

// Assign the original comma, dot or space separator; unsupported languages receive untranslated and return false.
bool ScLocaleUtility::GetThousandSeparator(ScStringView *out) {
  const char *separator;
  switch (language) {
  case kScLanguageEnglish:
  case kScLanguageBritishEnglish:
  case kScLanguageHebrew:
  case kScLanguageJapanese:
  case kScLanguageSimplifiedChinese:
  case kScLanguageTraditionalChinese:
  case kScLanguageThai:
  case kScLanguageKorean:
    separator = ",";
    break;
  case kScLanguageFrench:
  case kScLanguageGerman:
  case kScLanguageItalian:
  case kScLanguageSpanish:
  case kScLanguageDutch:
  case kScLanguageDanish:
  case kScLanguageSwedish:
  case kScLanguageRussian:
  case kScLanguagePortuguese:
    separator = ".";
    break;
  case kScLanguageNorwegian:
  case kScLanguageFinnish:
  case kScLanguagePolish:
    separator = " ";
    break;
  default:
    out->FromChar("untranslated");
    return false;
  }
  out->FromChar(separator);
  return true;
}

// Snapshot the input before mapping Unicode lowercase scalars, allowing encoded byte widths to change in owned storage.
void ScLocaleUtility::ConvertToLowerCase(ScStringView *text) {
  ScString copy(*text);
  copy.MakeLower();
  text->FromChar(copy.begin);
}

// Snapshot the input before mapping Unicode uppercase scalars, without expanding one scalar into multiple characters.
void ScLocaleUtility::ConvertToUpperCase(ScStringView *text) {
  ScString copy(*text);
  copy.MakeUpper();
  text->FromChar(copy.begin);
}

// UTF-8 sequence bytes remain unchanged.
void ScLocaleUtility::ConvertToLowerCase(char *character) {
  if (static_cast<u8>(*character) < 0x80)
    *character = static_cast<char>(SDL_tolower(*character));
}

// UTF-8 sequence bytes remain unchanged.
void ScLocaleUtility::ConvertToUpperCase(char *character) {
  if (static_cast<u8>(*character) < 0x80)
    *character = static_cast<char>(SDL_toupper(*character));
}

// Classify an ASCII lowercase character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharLowerA(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_islower(character) != 0;
}

// Classify an ASCII uppercase character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharUpperA(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_isupper(character) != 0;
}

// Classify an ASCII alphabetic character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharAlphaA(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_isalpha(character) != 0;
}

// Classify an ASCII decimal digit character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharNumeric(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_isdigit(character) != 0;
}

// Classify an ASCII alphanumeric character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharAlphaNumericA(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_isalnum(character) != 0;
}

// Classify an ASCII printable character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharPrintable(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_isprint(character) != 0;
}

bool ScLocaleUtility::CanCharEndLine(char) {
  return true;
}

bool ScLocaleUtility::CanCharStartLine(char) {
  return true;
}

// Classify an ASCII punctuation character through the original one-byte contract, excluding UTF-8 sequence bytes.
bool ScLocaleUtility::IsCharPunct(char character) {
  return static_cast<u8>(character) < 0x80 && SDL_ispunct(character) != 0;
}

bool ScLocaleUtility::GetLanguageRoadDrivingSide() {
  return language != kScLanguageBritishEnglish && language != kScLanguageJapanese;
}

u32 ScLocaleUtility::GetMeasurementSystem() {
  return country != 54;
}

bool ScLocaleUtility::AreFirstAndLastNamesReversed() {
  return language == kScLanguageJapanese ||
      (language > kScLanguagePolish && (language <= kScLanguageTraditionalChinese || language == kScLanguageKorean));
}

bool ScLocaleUtility::DoesLanguageUseInputMethodEditing() {
  return language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean;
}

bool ScLocaleUtility::DoesLanguageUseMultiByteCharacters() {
  return language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
}

u32 ScLocaleUtility::GetCharacterSetFromLanguage() {
  const i32 candidates[] = {1252, 1250, 874, 932, 936, 950, 949, 1251, 1251, 1253, 1254, 1255, 1256, 1257, 1257};
  for (i32 candidate : candidates)
    if (DoesLanguageUseCharacterSet(candidate))
      return static_cast<u32>(candidate);
  return 1252;
}

// Match the installed language-to-code-page table; overlapping languages can support more than one character set.
bool ScLocaleUtility::DoesLanguageUseCharacterSet(i32 characterSet) {
  switch (characterSet) {
  case 1252:
    return language == kScLanguageEnglish || language == kScLanguageBritishEnglish || language == kScLanguageFrench ||
        language == kScLanguageGerman || language == kScLanguageItalian || language == kScLanguageDanish ||
        language == kScLanguageSwedish || language == kScLanguageDutch || language == kScLanguageNorwegian ||
        language == kScLanguagePortuguese || language == kScLanguageFinnish || language == kScLanguageSpanish ||
        language == 28 || language == 29 || language == 30 || language == 31 || language == 41 || language == 35 ||
        language == 36 || language == 37 || language == 38 || language == 39 || language == 40;
  case 1250:
    return language == kScLanguagePolish || language == kScLanguageRussian || language == 33 || language == 26 ||
        language == 28 || language == 30 || language == 32;
  case 932:
    return language == kScLanguageJapanese;
  case 949:
    return language == kScLanguageKorean;
  case 936:
    return language == kScLanguageSimplifiedChinese;
  case 950:
    return language == kScLanguageTraditionalChinese;
  case 874:
    return language == kScLanguageThai;
  case 1251:
    return language == kScLanguageRussian || language == 24 || language == 23;
  case 1253:
    return language == 27;
  case 1254:
    return language == 34;
  case 1255:
    return language == kScLanguageHebrew;
  case 1256:
    return language == 22;
  default:
    return false;
  }
}
