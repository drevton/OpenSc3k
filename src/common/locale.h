#ifndef SC3_COMMON_LOCALE_H
#define SC3_COMMON_LOCALE_H

#include <set>

#include "common/api.h"
#include "common/services_module.h"

// Return the observed Windows CRT code page for a game language, or zero for an unavailable CRT locale.
u32 ScLanguageCrtCodePage(u32 language);
// Return the evidenced stock Western UI-font byte mapping, or zero for an unsupported active font context.
// This selects a display mapping for untagged legacy fields, without claiming their archive author's encoding.
u32 ScInstalledWesternTextPage(u32 language);

// Convert one character at an evidenced legacy resource boundary; failure leaves the output unchanged.
// Decode returns the consumed byte count. Encode returns bytes written into a caller-owned four-byte buffer.
u32 ScDecodeLegacyCharacter(u32 page, const char *bytes, u32 length, u32 *scalar);
u32 ScEncodeLegacyCharacter(u32 page, u32 scalar, char *out);
// Transcode complete text only at resource/save boundaries. Preserve embedded NUL and leave output unchanged on failure.
bool ScDecodeLegacyText(u32 page, const ScStringView &source, ScString *out);
bool ScEncodeLegacyText(u32 page, const ScStringView &source, ScString *out);

#if !defined(_WIN32)
struct SDL_Locale;
// Portable platform boundaries share the established regional host-language mapping.
bool ScHostExactLanguageId(const SDL_Locale *locale, u32 *id);
u32 ScHostLanguageId(const SDL_Locale *locale);
#endif

// Concrete services-module locale state, with the original standalone reference count and default US locale.
class ScLocaleUtility : public ScLanguageUtility {
public:
  ScLocaleUtility();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GetDayName(u8, ScStringView *) override;
  bool GetAbbrDayName(u8, ScStringView *) override;
  bool GetMonthName(u8, ScStringView *) override;
  bool GetAbbrMonthName(u8, ScStringView *) override;
  bool MakeTimeString(i32, i32, i32, ScStringView *) override;
  bool MakeDateString(i32, i32, u32, ScStringView *, DateFormat) override;
  bool GetCurrencySymbol(ScStringView *) override;
  bool DoesCurrencySymbolPrecedeAmount() override;
  bool IsSpaceBetweenCurrencySymbolAndAmount() override;
  bool GetThousandSeparator(ScStringView *) override;
  bool MakeMoneyString(i64 value, ScStringView *out) override;
  bool MakeNumberString(i64 value, ScStringView *out) override;
  void ConvertToLowerCase(ScStringView *) override;
  void ConvertToUpperCase(ScStringView *) override;
  void ConvertToLowerCase(char *) override;
  void ConvertToUpperCase(char *) override;
  bool IsCharLowerA(char) override;
  bool IsCharUpperA(char) override;
  bool IsCharAlphaA(char) override;
  bool IsCharNumeric(char) override;
  bool IsCharAlphaNumericA(char) override;
  bool IsCharPrintable(char) override;
  bool CanCharEndLine(char) override;
  bool CanCharStartLine(char) override;
  bool IsCharPunct(char) override;
  u32 GetCharacterSetFromLanguage() override;
  bool DoesLanguageUseCharacterSet(i32) override;
  bool GetLanguageRoadDrivingSide() override;
  u32 GetMeasurementSystem() override;
  bool AreFirstAndLastNamesReversed() override;
  bool DoesLanguageUseInputMethodEditing() override;
  bool DoesLanguageUseMultiByteCharacters() override;
  virtual ~ScLocaleUtility();

  friend class ScLanguageService;

protected:
  u32 references, language, country;
  u8 localCurrency;
};

// Actual language-manager primary identity and module-aware secondary service, with the original ordered availability set.
class ScLanguageService : public ScLanguageManager, public ScServicesBase {
public:
  ScLanguageService();
  ~ScLanguageService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void AddAvailableLanguage(u32) override;
  void RemoveAvailableLanguage(u32) override;
  u32 ConvertLanguageIDToLanguageType(u32) override;
  u32 GetCurrentLanguage() override;
  bool SetCurrentLanguage(u32) override;
  bool CanSwitchToLanguage(u32) override;
  u32 GetCurrentSystemLanguage() override;
  bool GetLanguageRuntimeLibraryName(ScStringView *, u32) override;
  bool GetNextAvailableLanguage(u32 *) override;
  bool GetLanguageDirectoryName(ScStringView *out, u32 language) override;
  bool GetLanguageEnglishName(ScStringView *, u32) override;
  bool GetLanguageLocalName(ScStringView *, u32) override;
  bool GetLanguageIDFromLanguageEnglishName(ScStringView *, u32 *) override;
  bool GetLanguageIDFromLanguageLocalName(ScStringView *, u32 *) override;
  bool GetLanguageIDFromEitherEnglishOrLocalName(ScStringView *, u32 *) override;
  bool GetLanguageEnglishNameFromLanguageID(ScStringView *, u32) override;
  bool GetLanguageLocalNameFromLanguageID(ScStringView *, u32) override;
  u32 GetLanguageIDAlias(u32) override;
  bool GetISO639LanguageCodeFromLanguageID(ScStringView *, u32) override;
  bool GetISO639LanguageIDFromLanguageCode(ScStringView *, u32 *) override;
  bool GetISO3166CountryCodeFromCountryID(ScStringView *out, u32 country) override;
  bool GetISO3166CountryIDFromCountryCode(ScStringView *, u32 *) override;
  u32 GetCurrentCountry() override;
  bool SetCurrentCountry(u32) override;
  u32 GetPrimaryLanguageForCountry(u32) override;
  u32 GetDefaultCountryForLanguage(u32) override;
  u32 GetCurrentSystemCountry() override;
  bool CanSwitchToCountry(i32) override;
  bool GetCountryEnglishName(ScStringView *, u32) override;
  bool GetCountryLocalName(ScStringView *, u32) override;
  bool GetCountryIDFromCountryEnglishName(ScStringView *, u32 *) override;
  bool GetCountryIDFromCountryLocalName(ScStringView *, u32 *) override;
  ScLanguageUtility *GetLanguageUtility(u32 language) override;
  ScLanguageUtility *GetNewLanguageUtility(u32) override;
  f32 GetExchangeRateForLocale(u32) override;
  void SetExchangeRateForLocale(f32, u32) override;
  bool ConvertMoneyFromBaseToLocale(i32, i32 *, u32) override;
  bool ConvertMoneyFromBaseToLocale(i64, i64 *, u32) override;
  bool ConvertMoneyFromLocaleToLocale(i32, u32, i32 *, u32) override;
  bool ConvertMoneyFromLocaleToLocale(i64, u32, i64 *, u32) override;

protected:
  u32 state;
  std::set<u32> available;
  u32 language, country;
  ScLocaleUtility utility;
};

#endif
