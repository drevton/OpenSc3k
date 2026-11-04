#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "application/application_core.h"
#include <assert.h>
#include <ctype.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#include <string.h>

// Cache the borrowed sound system after its real framework lookup, releasing the acquired reference after publication.
AuSoundDevice *ApServices::SS() {
  if (!ApSounds) {
    AuSoundDevice *sound = nullptr;
    ApGetFramework()->GetSystemService(kAuSoundDeviceServiceId, IID_AuSoundDevice, reinterpret_cast<void **>(&sound));
    ApSounds = sound;
    if (sound)
      sound->Release();
  }
  return ApSounds;
}

ApNewCityPrefs::ApNewCityPrefs() : ready(0), difficulty(0), city_size(0), start_date(0) {
  memset(mayor_name, 0, sizeof(mayor_name));
}

ApPreferencesObject::ApPreferencesObject()
    : video{}, sound{}, simulation{0, 1, 1, 0, 0, 0}, visibility{}, tips{0, 0}, load{0, 0}, maps{0, 0, 0, 0, 0},
      query{0, 0, 0, 0}, ordinances{0, 0, 0}, neighbors{0, 0, 0, 0}, budget{0, 0, 0},
      advisors{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, catalog{0, 0, 0, {0, 0, 0, 0, 0}}, charts{0, 0},
      replacement{0, 0, 0, {}, {}}, edit_buildings{0, {}}, database(nullptr) {
  RsFileSystem *files = ApGetFileSystem();
  if (files) {
    const char *directory = files->AppDirectory();
    if (directory) {
      ScString value(directory);
      path = value;
    }
  }
  path.Append("SC3.cfg");
  ready = false;
  language = country = -1;
  intelli_scroll = false;
  video.resolution_x = video.resolution_y = 0;
  video.use_hardware_acceleration = 0;
  memset(video.driver_identifier, -1, sizeof(video.driver_identifier));
  sound.enable_3d_sound = 0;
  sound.music = sound.sound_effects = sound.ambience = 1;
  sound.music_volume = sound.sound_effects_volume = sound.ambience_volume = 1024;
  simulation.auto_goto = 1;
  u32 power = static_cast<u32>(static_cast<i32>(ApGetGameApp()->GetSystemPerformanceInfo()->processor_power));
  if (power >= 5)
    visibility = {3, 2, 1};
  else if (power >= 4)
    visibility = {2, 2, 1};
  else if (power >= 3)
    visibility = {2, 1, 1};
  else if (power >= 2)
    visibility = {2, 1, 0};
  else
    visibility = {1, 1, 0};
  tips.enabled = 1;
  tips.last_tip = 0;
  memset(miscellaneous.values, 0, sizeof(miscellaneous.values));
  for (u32 i = 0; i < 5; ++i)
    catalog.selections[i] = 0;
  new_city.ready = load.ready = maps.ready = query.enabled = ordinances.ready = neighbors.ready = budget.ready = 0;
  advisors.ready = catalog.enabled = charts.ready = 0;
  graphs.ready = false;
}

ApPreferencesObject::~ApPreferencesObject() {
}

// Publish the primary Unknown/preferences view before its virtual retain; unsupported identifiers clear the output.
bool ApPreferencesObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ApPreferences);
  HANDLE_QUERY_INTERFACE(IID_ApPreferences, ApPreferences);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ApPreferencesObject::AddRef() {
  return ApRuntime::ScRefCount::AddRef();
}

u32 ApPreferencesObject::Release() {
  return ApRuntime::ScRefCount::Release();
}

// Borrow the current radio call-sign String without copying or retaining it.
ScStringView *ApPreferencesObject::GetRadioCallSign() {
  return &radio_call_sign;
}

void ApPreferencesObject::SetRadioCallSign(const ScStringView &value) {
  ScString copy(value);
  radio_call_sign = copy;
}

// Borrow the current load/save path String without adding a reference.
ScStringView *ApPreferencesObject::GetLoadSavePath() {
  return &load_save_path;
}

void ApPreferencesObject::SetLoadSavePath(ScStringView *value) {
  ScString copy(*value);
  load_save_path = copy;
}

// Borrow the mutable resolution, driver identifier and display flags without retaining the owner.
ApVideoSettings *ApPreferencesObject::VideoPrefs() {
  return &video;
}

// Borrow the mutable sound flags and signed volume settings without making a copy.
ApSoundSettings *ApPreferencesObject::SoundPrefs() {
  return &sound;
}

// Borrow the mutable simulation flags and resource-set selections.
ApSimSettings *ApPreferencesObject::SimPrefs() {
  return &simulation;
}

// Borrow the three animation visibility levels selected from processor performance at construction.
ApVisibilitySettings *ApPreferencesObject::VisibilityPrefs() {
  return &visibility;
}

// Borrow the tip enable byte and last displayed tip identifier.
ApTipPrefs *ApPreferencesObject::TODPrefs() {
  return &tips;
}

// Borrow the complete sixteen-by-thirty-two miscellaneous window preference table.
ApMiscWindowPrefs *ApPreferencesObject::MiscWindowPrefs() {
  return &miscellaneous;
}

// Borrow readiness, new-city selections and the fixed mayor-name buffer.
ApNewCityPrefs *ApPreferencesObject::NewCityPrefs() {
  return &new_city;
}

// Borrow the two independent load-dialog readiness and city-statistics flags.
ApLoadPrefs *ApPreferencesObject::LoadPrefs() {
  return &load;
}

// Borrow map-view selection, city-view visibility and saved window position.
ApMapsPrefs *ApPreferencesObject::MapsWindowPrefs() {
  return &maps;
}

// Borrow the query-dialog readiness, retained word and window position.
ApQueryPrefs *ApPreferencesObject::QueryDialogPrefs() {
  return &query;
}

// Borrow the ordinance window readiness and saved coordinates.
ApOrdinancesPrefs *ApPreferencesObject::OrdinancesWindowPrefs() {
  return &ordinances;
}

// Borrow the selected neighbor and saved dialog coordinates.
ApNeighborsPrefs *ApPreferencesObject::NeighborsDialogPrefs() {
  return &neighbors;
}

// Borrow the budget dialog readiness and saved coordinates.
ApBudgetPrefs *ApPreferencesObject::BudgetDialogPrefs() {
  return &budget;
}

// Borrow the advisor window positions and selected main page.
ApAdvisorPrefs *ApPreferencesObject::AdvMgrPrefs() {
  return &advisors;
}

// Borrow catalog readiness, position and five category selections.
ApCatalogPrefs *ApPreferencesObject::SelectABuildingPrefs() {
  return &catalog;
}

// Borrow chart readiness and its radio-button selection.
ApChartsPrefs *ApPreferencesObject::ChartsPrefs() {
  return &charts;
}

// Borrow the actual graph-settings interface rather than exposing an integer token.
ApGraphsPrefs *ApPreferencesObject::GraphsPrefs() {
  return &graphs;
}

// Borrow the two replacement filters and complete selected building/slide resource keys.
ApReplacementPrefs *ApPreferencesObject::BldgReplacementPrefs() {
  return &replacement;
}

// Borrow the building-editor readiness and owned directory String.
ApEditBldgPrefs *ApPreferencesObject::EditBATBldgListPrefs() {
  return &edit_buildings;
}

i32 ApPreferencesObject::GetLanguage() {
  return language;
}

void ApPreferencesObject::SetLanguage(i32 value) {
  language = value;
}

i32 ApPreferencesObject::GetCountry() {
  return country;
}

void ApPreferencesObject::SetCountry(i32 value) {
  country = value;
}

bool ApPreferencesObject::GetIntelliScroll() {
  return intelli_scroll;
}

void ApPreferencesObject::SetIntelliScroll(bool value) {
  intelli_scroll = value;
}

bool ApPreferencesObject::IsSongEnabled(u32 identifier) {
  auto found = songs.find(identifier);
  return found == songs.end() || found->second;
}

// Insert a missing song preference or overwrite its existing Boolean without modifying other selections.
void ApPreferencesObject::EnableDisableSong(u32 identifier, bool enabled) {
  auto found = songs.find(identifier);
  if (found == songs.end())
    songs.insert({identifier, enabled});
  else
    found->second = enabled;
}

// Ask the message server for packed screen bounds, then apply the original title-strip visibility margins.
// The fourth argument is genuinely unused; arithmetic retains the source's defined DWORD wrapping.
bool ApPreferencesObject::IsWindowVisible(i32 x, i32 y, i32 width, i32) {
  ScMessage bounds = {kWnMsg_QueryUiClientArea, 0, 0, 0};
  ApGetMessageServer()->MessageSend(bounds);
  u32 top_left = static_cast<u32>(bounds.data1), bottom_right = static_cast<u32>(bounds.data2);
  i32 left = x - 4, top = y - 4;
  i32 bottom = y + 13;
  i32 right = x + width - 4;
  return top >= static_cast<i32>(top_left & 0xffff) && bottom <= static_cast<i32>(bottom_right & 0xffff) &&
      left >= static_cast<i32>(top_left >> 16) && right <= static_cast<i32>(bottom_right >> 16);
}

u32 ApGraphSettings::CheckboxCount() {
  return static_cast<u32>(checkboxes.size());
}

bool ApGraphSettings::FindCheckbox(u32 identifier) {
  return checkboxes.find(identifier) != checkboxes.end();
}

bool ApGraphSettings::GetCheckboxState(u32 identifier) {
  auto found = checkboxes.find(identifier);
  return found != checkboxes.end() && found->second;
}

bool ApGraphSettings::AddCheckbox(u32 identifier, bool state) {
  checkboxes[identifier] = state;
  return true;
}

// Walk ascending checkbox identifiers and publish both outputs only after finding the selected entry.
bool ApGraphSettings::GetCheckboxByIndex(u32 index, u32 *identifier, bool *state) {
  auto found = checkboxes.begin();
  for (u32 current = 0; current < index; ++current) {
    ++found;
    if (found == checkboxes.end())
      return false;
  }
  assert(found != checkboxes.end());
  *identifier = found->first;
  *state = found->second;
  return true;
}

void ApGraphSettings::SetReady(bool value) {
  ready = value;
}

bool ApGraphSettings::IsReady() {
  return ready;
}

void ApGraphSettings::SetRadioSelection(u32 value) {
  selection = value;
}

u32 ApGraphSettings::GetRadioSelection() {
  return selection;
}

// Open the saved preference database and read its fifteen record types in the original order.
// Individual record failures are ignored; close the opened database before returning the original true result.
bool ApPreferencesObject::Load() {
  if (database.OpenRead(path)) {
    TryToReadRecord(kApGeneralPreferencesRecordType, kApGeneralPreferencesRecordInstance);
    TryToReadRecord(kApMusicSelectionPreferencesRecordType, kApMusicSelectionPreferencesRecordInstance);
    TryToReadRecord(kApNewCityPreferencesRecordType, kApNewCityPreferencesRecordInstance);
    TryToReadRecord(kApLoadCityPreferencesRecordType, kApLoadCityPreferencesRecordInstance);
    TryToReadRecord(kApMapsPreferencesRecordType, kApMapsPreferencesRecordInstance);
    TryToReadRecord(kApQueryPreferencesRecordType, kApQueryPreferencesRecordInstance);
    TryToReadRecord(kApOrdinancesPreferencesRecordType, kApOrdinancesPreferencesRecordInstance);
    TryToReadRecord(kApNeighborsPreferencesRecordType, kApNeighborsPreferencesRecordInstance);
    TryToReadRecord(kApBudgetPreferencesRecordType, kApBudgetPreferencesRecordInstance);
    TryToReadRecord(kApAdvisorsPreferencesRecordType, kApAdvisorsPreferencesRecordInstance);
    TryToReadRecord(kApCatalogPreferencesRecordType, kApCatalogPreferencesRecordInstance);
    TryToReadRecord(kApChartsPreferencesRecordType, kApChartsPreferencesRecordInstance);
    TryToReadRecord(kApGraphsPreferencesRecordType, kApGraphsPreferencesRecordInstance);
    TryToReadRecord(kApReplacementPreferencesRecordType, kApReplacementPreferencesRecordInstance);
    TryToReadRecord(kApEditBuildingsPreferencesRecordType, kApEditBuildingsPreferencesRecordInstance);
    database.Shutdown();
  }
  return true;
}

// Remove the existing preference file before opening the replacement database and writing its fifteen records.
// Ignore the removal and individual record results, close an opened database and always return true.
bool ApPreferencesObject::Save() {
  RsFile::Delete(path);
  if (database.OpenWrite(path)) {
    TryToWriteRecord(kApGeneralPreferencesRecordType, kApGeneralPreferencesRecordInstance);
    TryToWriteRecord(kApMusicSelectionPreferencesRecordType, kApMusicSelectionPreferencesRecordInstance);
    TryToWriteRecord(kApNewCityPreferencesRecordType, kApNewCityPreferencesRecordInstance);
    TryToWriteRecord(kApLoadCityPreferencesRecordType, kApLoadCityPreferencesRecordInstance);
    TryToWriteRecord(kApMapsPreferencesRecordType, kApMapsPreferencesRecordInstance);
    TryToWriteRecord(kApQueryPreferencesRecordType, kApQueryPreferencesRecordInstance);
    TryToWriteRecord(kApOrdinancesPreferencesRecordType, kApOrdinancesPreferencesRecordInstance);
    TryToWriteRecord(kApNeighborsPreferencesRecordType, kApNeighborsPreferencesRecordInstance);
    TryToWriteRecord(kApBudgetPreferencesRecordType, kApBudgetPreferencesRecordInstance);
    TryToWriteRecord(kApAdvisorsPreferencesRecordType, kApAdvisorsPreferencesRecordInstance);
    TryToWriteRecord(kApCatalogPreferencesRecordType, kApCatalogPreferencesRecordInstance);
    TryToWriteRecord(kApChartsPreferencesRecordType, kApChartsPreferencesRecordInstance);
    TryToWriteRecord(kApGraphsPreferencesRecordType, kApGraphsPreferencesRecordInstance);
    TryToWriteRecord(kApReplacementPreferencesRecordType, kApReplacementPreferencesRecordInstance);
    TryToWriteRecord(kApEditBuildingsPreferencesRecordType, kApEditBuildingsPreferencesRecordInstance);
    database.Shutdown();
  }
  return true;
}

// Read each original preference record using its actual database stream and partial-write ordering.
// Close the record after releasing the queried stream, including failed reads and unsupported type/instance pairs.
bool ApPreferencesObject::TryToReadRecord(u32 type, u32 instance) {
  RsKey key(type, kApPreferencesRecordGroup, instance);
  RsRecord *record;
  if (!database.OpenRecord(key, &record))
    return false;
  RsStream *stream;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    database.CloseRecord(&record);
    return false;
  }
  bool result = false;
  u8 value = static_cast<u8>(instance >> 24);
  if (type == kApGeneralPreferencesRecordType && instance == kApGeneralPreferencesRecordInstance) {
    result = stream->ReadString(&radio_call_sign) && stream->ReadString(&load_save_path) && stream->ReadI32(&language);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        intelli_scroll = value != 0;
    }
    if (result)
      result = stream->ReadU32(&video.resolution_x) && stream->ReadU32(&video.resolution_y);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        video.use_hardware_acceleration = value != 0;
    }
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        video.windowed = value != 0;
    }
    if (result)
      result = stream->ReadBlock(video.driver_identifier, 32);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        sound.enable_3d_sound = value != 0;
    }
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        sound.music = value != 0;
    }
    if (result)
      result = stream->ReadI32(&sound.music_volume);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        sound.sound_effects = value != 0;
    }
    if (result)
      result = stream->ReadI32(&sound.sound_effects_volume);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        sound.ambience = value != 0;
    }
    if (result)
      result = stream->ReadI32(&sound.ambience_volume);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        simulation.auto_budget = value != 0;
    }
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        simulation.enable_disasters = value != 0;
    }
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        simulation.auto_goto = value != 0;
    }
    if (result)
      result = stream->ReadU32(&simulation.landscape_id) && stream->ReadU32(&simulation.flora_set_id) &&
          stream->ReadU32(&simulation.building_set_id) && stream->ReadI32(&visibility.traffic_visibility) &&
          stream->ReadI32(&visibility.sims_visibility) && stream->ReadI32(&visibility.other_animations_visibility);
    if (result) {
      result = stream->ReadU8(&value);
      if (result)
        tips.enabled = value != 0;
    }
    if (result)
      result = stream->ReadU32(&tips.last_tip);
  } else if (type == kApMusicSelectionPreferencesRecordType && instance == kApMusicSelectionPreferencesRecordInstance) {
    u32 count = 0, identifier = 0;
    result = stream->ReadU32(&count);
    if (result) {
      for (u32 i = 0; i < count; ++i) {
        bool enabled = false;
        result = stream->ReadU32(&identifier);
        if (result) {
          result = stream->ReadU8(&value);
          if (result)
            enabled = value != 0;
        }
        songs[identifier] = enabled;
      }
    }
  } else if (type == kApLoadCityPreferencesRecordType && instance == kApLoadCityPreferencesRecordInstance) {
    result = stream->ReadU8(&value);
    if (result) {
      load.ready = 1;
      load.show_city_stats = value != 0;
    }
  } else if (type == kApMapsPreferencesRecordType && instance == kApMapsPreferencesRecordInstance) {
    result = stream->ReadU32(&maps.selected_view);
    if (result) {
      result = stream->ReadU8(&value);
      if (result) {
        maps.show_in_city = value != 0;
        result = stream->ReadU32(&maps.x) && stream->ReadU32(&maps.y);
        if (result)
          maps.ready = 1;
      }
    }
  } else if (type == kApOrdinancesPreferencesRecordType && instance == kApOrdinancesPreferencesRecordInstance) {
    result = stream->ReadU32(&ordinances.x) && stream->ReadU32(&ordinances.y);
    if (result)
      ordinances.ready = 1;
  } else if (type == kApNeighborsPreferencesRecordType && instance == kApNeighborsPreferencesRecordInstance) {
    result =
        stream->ReadU32(&neighbors.selected_neighbor) && stream->ReadU32(&neighbors.x) && stream->ReadU32(&neighbors.y);
    if (result)
      neighbors.ready = 1;
  } else if (type == kApBudgetPreferencesRecordType && instance == kApBudgetPreferencesRecordInstance) {
    result = stream->ReadU32(&budget.x) && stream->ReadU32(&budget.y);
    if (result)
      budget.ready = 1;
  } else if (type == kApAdvisorsPreferencesRecordType && instance == kApAdvisorsPreferencesRecordInstance) {
    result = stream->ReadU32(&advisors.meeting_x) && stream->ReadU32(&advisors.meeting_y) &&
        stream->ReadU32(&advisors.main_x) && stream->ReadU32(&advisors.main_y) &&
        stream->ReadU32(&advisors.main_page) && stream->ReadU32(&advisors.topic_x) &&
        stream->ReadU32(&advisors.topic_y) && stream->ReadU32(&advisors.petition_x) &&
        stream->ReadU32(&advisors.petition_y) && stream->ReadU32(&advisors.ordinance_x) &&
        stream->ReadU32(&advisors.ordinance_y);
    if (result)
      advisors.ready = 1;
  } else if (type == kApChartsPreferencesRecordType && instance == kApChartsPreferencesRecordInstance) {
    result = stream->ReadU32(&charts.selection);
    if (result)
      charts.ready = 1;
  } else if (type == kApGraphsPreferencesRecordType && instance == kApGraphsPreferencesRecordInstance) {
    u32 count = 0;
    result = stream->ReadU32(&graphs.selection) && stream->ReadU32(&count);
    for (u32 i = 0; result && i < count; ++i) {
      u32 identifier = 0;
      result = stream->ReadU32(&identifier) && stream->ReadU8(&value);
      if (result)
        result = graphs.AddCheckbox(identifier, value != 0);
    }
    if (result)
      graphs.ready = true;
  } else if (type == kApReplacementPreferencesRecordType && instance == kApReplacementPreferencesRecordInstance) {
    result = stream->ReadU32(&replacement.type_filter) && stream->ReadU32(&replacement.size_filter) &&
        stream->ReadKey(&replacement.selected_building) && stream->ReadKey(&replacement.selected_slide);
    replacement.ready = result;
  } else if (type == kApEditBuildingsPreferencesRecordType && instance == kApEditBuildingsPreferencesRecordInstance) {
    result = stream->ReadString(&edit_buildings.directory_path);
    edit_buildings.ready = result;
  } else if (type == kApNewCityPreferencesRecordType && instance == kApNewCityPreferencesRecordInstance) {
    result = stream->ReadU32(&new_city.difficulty) && stream->ReadU32(&new_city.city_size) &&
        stream->ReadU32(&new_city.start_date);
    if (result) {
      stream->ReadBlock(new_city.mayor_name, sizeof(new_city.mayor_name));
      new_city.mayor_name[127] = 0;
      for (u32 i = 0; new_city.mayor_name[i]; ++i) {
        if (static_cast<i8>(new_city.mayor_name[i]) < 32) {
          new_city.mayor_name[0] = 0;
          break;
        }
      }
      new_city.ready = 1;
    }
  } else if (type == kApQueryPreferencesRecordType && instance == kApQueryPreferencesRecordInstance) {
    result = stream->ReadU32(&query.word4) && stream->ReadU32(&query.x) && stream->ReadU32(&query.y);
    if (result)
      query.enabled = 1;
  } else if (type == kApCatalogPreferencesRecordType && instance == kApCatalogPreferencesRecordInstance) {
    result = stream->ReadU32(&catalog.x) && stream->ReadU32(&catalog.y);
    for (u32 i = 0; result && i < 5; ++i)
      result = stream->ReadU32(&catalog.selections[i]);
    if (result)
      catalog.enabled = 1;
  }
  stream->Release();
  database.CloseRecord(&record);
  return result;
}

// Write the original preference records with their exact readiness checks, stream order and close/abort policy.
// Failed writes abort the record after releasing its stream; a failed stream query follows the original close path.
bool ApPreferencesObject::TryToWriteRecord(u32 type, u32 instance) {
  RsKey key(type, kApPreferencesRecordGroup, instance);
  RsRecord *record;
  bool result =
      database.TestForRecord(key) ? database.OpenRecordForWrite(key, &record) : database.CreateNewRecord(key, &record);
  if (!result)
    return false;
  RsStream *stream;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    database.CloseRecord(&record);
    return result;
  }
  result = false;
  if (type == kApGeneralPreferencesRecordType && instance == kApGeneralPreferencesRecordInstance) {
    result = stream->WriteString(ApGetPreferences()->GetRadioCallSign());
    result = result && stream->WriteString(ApGetPreferences()->GetLoadSavePath()) &&
        stream->WriteI32(ApGetPreferences()->GetLanguage());
    bool scrolling = ApGetPreferences()->GetIntelliScroll() == 1;
    result = result && stream->WriteU8(scrolling) &&
        stream->WriteU32(video.resolution_x) && stream->WriteU32(video.resolution_y) &&
        stream->WriteU8(video.use_hardware_acceleration == 1) &&
        stream->WriteU8(video.windowed == 1) && stream->WriteBlock(video.driver_identifier, 32) &&
        stream->WriteU8(sound.enable_3d_sound == 1) &&
        stream->WriteU8(sound.music == 1) && stream->WriteI32(sound.music_volume) &&
        stream->WriteU8(sound.sound_effects == 1) && stream->WriteI32(sound.sound_effects_volume) &&
        stream->WriteU8(sound.ambience == 1) && stream->WriteI32(sound.ambience_volume) &&
        stream->WriteU8(simulation.auto_budget == 1) && stream->WriteU8(simulation.enable_disasters == 1);
    if (result)
      result = stream->WriteU8(simulation.auto_goto == 1) && stream->WriteU32(simulation.landscape_id) &&
          stream->WriteU32(simulation.flora_set_id) && stream->WriteU32(simulation.building_set_id) &&
          stream->WriteI32(visibility.traffic_visibility) && stream->WriteI32(visibility.sims_visibility) &&
          stream->WriteI32(visibility.other_animations_visibility);
    if (result)
      result = stream->WriteU8(tips.enabled == 1) && stream->WriteU32(tips.last_tip);
  } else if (type == kApMusicSelectionPreferencesRecordType && instance == kApMusicSelectionPreferencesRecordInstance) {
    result = stream->WriteU32(static_cast<u32>(songs.size()));
    for (auto it = songs.begin(); it != songs.end(); ++it)
      if (result)
        result = stream->WriteU32(it->first) && stream->WriteU8(it->second == 1);
  } else if (type == kApLoadCityPreferencesRecordType && instance == kApLoadCityPreferencesRecordInstance) {
    result = load.ready == 1 && stream->WriteU8(load.show_city_stats == 1);
  } else if (type == kApMapsPreferencesRecordType && instance == kApMapsPreferencesRecordInstance) {
    result = maps.ready == 1 && stream->WriteU32(maps.selected_view) && stream->WriteU8(maps.show_in_city == 1) &&
        stream->WriteU32(maps.x) && stream->WriteU32(maps.y);
  } else if (type == kApOrdinancesPreferencesRecordType && instance == kApOrdinancesPreferencesRecordInstance) {
    result = ordinances.ready == 1 && stream->WriteU32(ordinances.x) && stream->WriteU32(ordinances.y);
  } else if (type == kApNeighborsPreferencesRecordType && instance == kApNeighborsPreferencesRecordInstance) {
    result = neighbors.ready == 1 && stream->WriteU32(neighbors.selected_neighbor) && stream->WriteU32(neighbors.x) &&
        stream->WriteU32(neighbors.y);
  } else if (type == kApBudgetPreferencesRecordType && instance == kApBudgetPreferencesRecordInstance) {
    result = budget.ready == 1 && stream->WriteU32(budget.x) && stream->WriteU32(budget.y);
  } else if (type == kApAdvisorsPreferencesRecordType && instance == kApAdvisorsPreferencesRecordInstance) {
    result = advisors.ready == 1 && stream->WriteU32(advisors.meeting_x) && stream->WriteU32(advisors.meeting_y) &&
        stream->WriteU32(advisors.main_x) && stream->WriteU32(advisors.main_y) &&
        stream->WriteU32(advisors.main_page) && stream->WriteU32(advisors.topic_x) &&
        stream->WriteU32(advisors.topic_y) && stream->WriteU32(advisors.petition_x) &&
        stream->WriteU32(advisors.petition_y) && stream->WriteU32(advisors.ordinance_x) &&
        stream->WriteU32(advisors.ordinance_y);
  } else if (type == kApChartsPreferencesRecordType && instance == kApChartsPreferencesRecordInstance) {
    result = charts.ready == 1 && stream->WriteU32(charts.selection);
  } else if (type == kApGraphsPreferencesRecordType && instance == kApGraphsPreferencesRecordInstance) {
    result = graphs.ready == 1;
    if (result)
      result = stream->WriteU32(graphs.selection);
    u32 count = graphs.CheckboxCount();
    if (result)
      result = stream->WriteU32(count);
    for (u32 i = 0; result && i < count; ++i) {
      u32 identifier;
      bool state;
      result = graphs.GetCheckboxByIndex(i, &identifier, &state);
      if (result)
        result = stream->WriteU32(identifier) && stream->WriteU8(state == 1);
    }
  } else if (type == kApReplacementPreferencesRecordType && instance == kApReplacementPreferencesRecordInstance) {
    result = replacement.ready != 0 && stream->WriteU32(replacement.type_filter) &&
        stream->WriteU32(replacement.size_filter) && stream->WriteKey(&replacement.selected_building) &&
        stream->WriteKey(&replacement.selected_slide);
  } else if (type == kApEditBuildingsPreferencesRecordType && instance == kApEditBuildingsPreferencesRecordInstance) {
    result = edit_buildings.ready != 0 && stream->WriteString(&edit_buildings.directory_path);
  } else if (type == kApNewCityPreferencesRecordType && instance == kApNewCityPreferencesRecordInstance) {
    result = new_city.ready == 1;
    new_city.mayor_name[127] = 0;
    if (result)
      result = stream->WriteU32(new_city.difficulty) && stream->WriteU32(new_city.city_size) &&
          stream->WriteU32(new_city.start_date) && stream->WriteBlock(new_city.mayor_name, sizeof(new_city.mayor_name));
  } else if (type == kApQueryPreferencesRecordType && instance == kApQueryPreferencesRecordInstance) {
    result =
        query.enabled == 1 && stream->WriteU32(query.word4) && stream->WriteU32(query.x) && stream->WriteU32(query.y);
  } else if (type == kApCatalogPreferencesRecordType && instance == kApCatalogPreferencesRecordInstance) {
    result = catalog.enabled == 1 && stream->WriteU32(catalog.x) && stream->WriteU32(catalog.y);
    for (u32 i = 0; result && i < 5; ++i)
      result = stream->WriteU32(catalog.selections[i]);
  }
  stream->Release();
  if (result)
    database.CloseRecord(&record);
  else
    database.AbortRecord(&record);
  return result;
}

ApStateController::ApStateController() : current_state(0), current_target(nullptr), message_server(nullptr) {
}

ApStateController::~ApStateController() {
  Shutdown();
}

bool ApStateController::Init(ScMessageServer *server) {
  message_server = server;
  return true;
}

bool ApStateController::Shutdown() {
  return true;
}

u32 ApStateController::GetCurrentUIState() {
  return current_state;
}

// Borrow the target resolved by the last state transition. Registration changes do not update this cached pointer.
ScMessageTarget *ApStateController::GetCurrentStateTarget() {
  return current_target;
}

// Publish a changed state and its mapped target before sending the original state-change message.
// Looking up an unregistered state inserts a null target; selecting the same state sends nothing.
bool ApStateController::SetCurrentUIState(u32 state) {
  if (current_state != state) {
    current_state = state;
    current_target = states[state];
    ScMessage message(kApMsgUiStateChanged, current_state, reinterpret_cast<uptr>(current_target), 0);
    message_server->MessageSend(message);
  }
  return true;
}

// Insert a borrowed state target without replacing an existing mapping, then subscribe the supplied target.
// Duplicate identifiers still perform the original notification registration.
bool ApStateController::RegisterUIState(u32 state, ScMessageTarget *target) {
  states.insert({state, target});
  message_server->AddNotification(target, kApMsgUiStateChanged);
  return true;
}

bool ApStateController::UnregisterUIState(u32 state) {
  states.erase(state);
  return true;
}

bool ApStateController::UnregisterAllUIStates() {
  states.clear();
  return true;
}

// Send the original enumeration message directly to the caller's target for each registered state.
// The state table is an unordered collection, as in the original hash_map.
bool ApStateController::EnumerateUIStates(ScMessageTarget *target) {
  ScMessage message(kApMsgEnumerateUiState);
  for (const auto &entry : states) {
    message.data1 = entry.first;
    message.data2 = reinterpret_cast<uptr>(entry.second);
    target->DoMessage(&message);
  }
  return true;
}

bool ApStateController::GetFlag(u32 flag) {
  return flags.count(flag) != 0;
}

void ApStateController::SetFlag(u32 flag) {
  flags.insert(flag);
}

void ApStateController::ClearAllFlags() {
  flags.clear();
}

ApIdleDetector::ApIdleDetector() : ScServiceBase(kScVariableManagerServiceId, 0) {
}

ApIdleDetector::~ApIdleDetector() {
}

bool ApIdleDetector::OnIdle(u32) {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kApMsgIdle);
    server->MessageSend(message);
  }
  return true;
}

// Populate the localized system report with executable, user, processor, graphics and available sound information.
void ApSysInfoWindow::PutInfoIntoTheInfoListBoxWindow(WnAppInfoList *list) {
  ScVersion version;
  ScString label, value, format, cpu_name, mmx_text, feature_text;
  {
    UiTextHandle text(kRsGameApplicationNameText, kRsGameTextGroup);
    label = text.Val();
  }
  version.GetModuleName(&value);
  list->InsertStringPair(label, value, -1);
  {
    UiTextHandle text(kRsGameApplicationVersionText, kRsGameTextGroup);
    label = text.Val();
  }
  version.GetModuleVersion(&value);
  list->InsertStringPair(label, value, -1);
  {
    UiTextHandle text(kRsGameOEMIdentificationText, kRsGameTextGroup);
    label = text.Val();
  }
  version.GetDecryptedOEMString(&value);
  list->InsertStringPair(label, value, -1);
  {
    UiTextHandle text(kRsGameSimCityAppDirText, kRsGameTextGroup);
    label = text.Val();
  }
  {
    ScString directory(ApGetFileSystem()->AppDirectory());
    list->InsertStringPair(label, directory, -1);
  }
  {
    UiTextHandle text(kRsGameSystemText, kRsGameTextGroup);
    label = text.Val();
  }
  {
    ScString empty_first, empty_second;
    list->InsertStringPair(empty_first, empty_second, -1);
  }
  {
    ScString empty;
    list->InsertStringPair(label, empty, -1);
  }
  {
    UiTextHandle text(kRsGameUserNameText, kRsGameTextGroup);
    label = text.Val();
  }
  {
    UiTextHandle text(kRsCommonUnknownUserText, kRsCommonTextGroup);
    format = text.Val();
  }
  version.GetUserNameA(&value, &format);
  list->InsertStringPair(label, value, -1);
#if defined(_WIN32)
  i32 system_type = static_cast<i32>(version.GetSystemType());
  i32 major = version.GetSystemVersion(0u), minor = version.GetSystemVersion(1u);
#endif
  {
    UiTextHandle text(kRsGameOSVersionText, kRsGameTextGroup);
    label = text.Val();
  }
#if defined(_WIN32)
  {
    UiTextHandle text(system_type == 4 ? 1701 : kRsCommonWindowsUUFormatText, kRsCommonTextGroup);
    format = text.Val();
  }
  value.Sprintf(format.ToChar(), major, minor);
  if (system_type == 3) {
    value.Append(" ");
    if (major == 4 && (minor == 0 || minor == 1)) {
      UiTextHandle text(minor == 0 ? 1734 : kRsCommonWindows98Text, kRsCommonTextGroup);
      ScString suffix = text.Val();
      value.Append(suffix.Data(), suffix.Strlen());
    }
  }
#else
  if (!version.GetSystemVersion(&value))
    value.FromChar("?");
#endif
  list->InsertStringPair(label, value, -1);
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  i32 cpu_type = version.GetCPUVersion();
#endif
  {
    UiTextHandle text(kRsGameCPUTypeText, kRsGameTextGroup);
    label = text.Val();
  }
  {
    UiTextHandle text(kRsCommonCPUFormatText, kRsCommonTextGroup);
    format = text.Val();
  }
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  if (version.IsCPUClone()) {
    if (!version.GetCPUName(&cpu_name)) {
      UiTextHandle text(kRsCommonNonIntelText, kRsCommonTextGroup);
      cpu_name = text.Val();
    }
  } else {
    u32 identifier = cpu_type >= 0 && cpu_type <= 6 ? 1703u + static_cast<u32>(cpu_type) :
        cpu_type >= 7 && cpu_type <= 9 ? 1710u : cpu_type == 10 ? 1721u : 1724u;
    UiTextHandle text(identifier, kRsCommonTextGroup);
    cpu_name = text.Val();
  }
  {
    UiTextHandle text(
        (version.GetCPUFlags() & kApCpuMmxFeatureBit) ? 1727 : kRsCommonMMXAbsentText, kRsCommonTextGroup);
    mmx_text = text.Val();
  }
  {
    UiTextHandle text((version.GetCPUFlags() & 8) ? 1731 : kRsCommonTimeStampAbsentText, kRsCommonTextGroup);
    feature_text = text.Val();
  }
#else
  if (!version.GetCPUName(&cpu_name))
    cpu_name.FromChar("?");
  mmx_text.FromChar("?");
  feature_text.FromChar("?");
#endif
  value.Sprintf(format.ToChar(), cpu_name.ToChar(), mmx_text.ToChar(), feature_text.ToChar());
  list->InsertStringPair(label, value, -1);
  u32 cpu_speed = version.GetCPUSpeed();
  if (cpu_speed) {
    {
      UiTextHandle text(kRsGameCPUSpeedText, kRsGameTextGroup);
      label = text.Val();
    }
    {
      UiTextHandle text(kRsCommonUMhzFormatText, kRsCommonTextGroup);
      format = text.Val();
    }
    value.Sprintf(format.ToChar(), cpu_speed);
    list->InsertStringPair(label, value, -1);
  }
  u32 total_memory = 0, available_memory = 0;
  {
    UiTextHandle text(kRsGameSystemMemoryText, kRsGameTextGroup);
    label = text.Val();
  }
  version.GetSystemMemory(&total_memory, &available_memory);
  {
    UiTextHandle text(kRsCommonUMBFormatText, kRsCommonTextGroup);
    format = text.Val();
  }
  value.Sprintf(format.ToChar(), total_memory);
  list->InsertStringPair(label, value, -1);
  u32 driver_major = 0, driver_minor = 0;
  {
    UiTextHandle text(kRsGameDirectXVersionText, kRsGameTextGroup);
    label = text.Val();
  }
  ApGetGraphics()->GetVideoDriverVersionInformation(&driver_major, &driver_minor);
  {
    UiTextHandle text(driver_major == 5 ? 1749 : kRsCommonUUFormatText, kRsCommonTextGroup);
    format = text.Val();
  }
  value.Sprintf(format.ToChar(), driver_major, driver_minor);
  list->InsertStringPair(label, value, -1);
  {
    UiTextHandle text(kRsGameVideoAdapterText, kRsGameTextGroup);
    label = text.Val();
  }
  if (!ApGetGraphics()->GetVideoDriverInfoCurrent(&format, &value)) {
    UiTextHandle text(kRsCommonUnknownVideoCardTypeText, kRsCommonTextGroup);
    value = text.Val();
  }
  list->InsertStringPair(label, format, -1);
  label.Erase(0, label.Strlen());
  list->InsertStringPair(label, value, -1);
  GxMode screen = ApGetGraphics()->GetScreenMetrics();
  GxColorType color = ApGetGraphics()->GetScreenColorType();
  {
    UiTextHandle text(kRsGameCurrentScreenModeText, kRsGameTextGroup);
    label = text.Val();
  }
  u32 color_text;
  switch (static_cast<i32>(color.color)) {
  case 2: color_text = 1738; break;
  case 3: color_text = 1739; break;
  case 4:
  case 12: color_text = 1740; break;
  case 5:
  case 9:
  case 10: color_text = 1741; break;
  case 6: color_text = 1742; break;
  case 7: color_text = 1743; break;
  case 8: color_text = 1744; break;
  case 13: color_text = 1745; break;
  default: color_text = 1746; break;
  }
  {
    UiTextHandle text(color_text, kRsCommonTextGroup);
    format = text.Val();
  }
  {
    // The installed call passes the selected color identifier with group 1747 and keeps this value unused.
    UiTextHandle text(color_text, 1747);
    cpu_name = text.Val();
  }
  value.Sprintf(format.ToChar(), screen.width, screen.height, screen.depth, format.ToChar());
  list->InsertStringPair(label, value, -1);
  u32 offscreen_memory =
      ApGetGraphics()->VideoMemoryTotal(GxVideoMemoryType::OffscreenPlain) / kApVideoMemoryReportDivisor;
  u32 texture_memory = ApGetGraphics()->VideoMemoryTotal(GxVideoMemoryType::Texture) / kApVideoMemoryReportDivisor;
  {
    UiTextHandle text(kRsGameVideoMemoryText, kRsGameTextGroup);
    label = text.Val();
  }
  {
    UiTextHandle text(kRsCommonTotalUMBTexturesUMBFormatText, kRsCommonTextGroup);
    format = text.Val();
  }
  value.Sprintf(format.ToChar(), offscreen_memory, texture_memory);
  list->InsertStringPair(label, value, -1);
  AuSoundDevice *sound = ApServices::SS();
  if (sound && sound->GetDriverInfoCount() && sound->GetDriverInfoCurrent(&format, &value)) {
    label.FromChar("Sound adapter:");
    list->InsertStringPair(label, value, -1);
    label.Erase(0, label.Strlen());
    list->InsertStringPair(label, format, -1);
  }
}

ApSysInfoWindow::ApSysInfoWindow() : references(0) {
}

ApSysInfoWindow::~ApSysInfoWindow() {
}

// Publish the Unknown, window-procedure or system-information identity and retain it through the virtual method.
// Unsupported identifiers clear the output.
bool ApSysInfoWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ApSysInfoWindow);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, ApSysInfoWindow);
  HANDLE_QUERY_INTERFACE(IID_ApSysInfoWindow, ApSysInfoWindow);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the executable director. Return the live count after that call.
u32 ApSysInfoWindow::AddRef() {
  ++references;
  ApGetDirector()->AddRef();
  return references;
}

// Remove the director reference before deleting the final procedure identity, including an old zero count.
// Nonfinal release decrements the local count first and returns its live value after the director call.
u32 ApSysInfoWindow::Release() {
  if (references > 1) {
    --references;
    ApGetDirector()->RemoveRef();
    return references;
  }
  ApGetDirector()->RemoveRef();
  delete this;
  return 0;
}

// Configure the actual generic window and install this procedure before publishing its primary window view.
// The acquired generic-window reference transfers to the returned identity rather than being released here.
bool ApSysInfoWindow::CreateSysInfoWindow(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic = nullptr;
  if (!ApGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, true);
  generic->SetStyle(kUiGenericResizable, false);
  generic->SetStyle(kUiGenericScroller, false);
  generic->SetStyle(kUiGenericBackButton, false);
  generic->SetStyle(kUiGenericSecondButton, false);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kApSystemInfoWindowId);
  return true;
}

// Route the original generic-window lifecycle and button messages to their actual handlers.
// Return, Escape and Alt-F4 request the same completion identifier and consume the key message.
bool ApSysInfoWindow::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_Command:
    return first == 1 ? OnButtonActivated(window, static_cast<u32>(second)) : false;
  case kWnMsg_KeyDown:
    if (first != 13 && first != 27 && (first != 115 || !(second & 4)))
      return false;
    EndDialog(window, kApSystemInfoCloseButtonId);
    return true;
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  case kWnMsg_ClientConstraints:
    return OnGetMinMaxInfo(window, reinterpret_cast<ScRect *>(first));
  default:
    return false;
  }
}

// Undo the city's hidden pause when a city is present, then post the original modal completion message.
// The manager's real Boolean post result is returned directly.
bool ApSysInfoWindow::EndDialog(WnWindow *window, u32 result) {
  CtCity *city = ApGetCity();
  if (city)
    city->SimHiddenUnpause();
  return window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
}

// Pause the city, create the information window's background and list, and populate the original system details.
bool ApSysInfoWindow::OnInit(WnWindow *window) {
  WnResources *resources = ApGetUiResources();
  CtCity *city = ApGetCity();
  if (city)
    city->SimHiddenPause();
  {
    UiTextHandle title(kRsGameSystemInfoText, kRsGameTextGroup);
    ScString caption = title;
    window->SetCaption(caption);
  }
  window->SetSize(600, 400);
  WnWindow *parent = window->GetMainWindow();
  i32 y = (parent->GetH() - window->GetH()) / 2;
  i32 x = (window->GetMainWindow()->GetW() - window->GetW()) / 2;
  window->GZWinMoveTo(x, y);
  left_font = resources->GetFont(kWnFontUiSmall);
  right_font = resources->GetFont(kWnFontUiMain);
  AddImageButton(window, kApSystemInfoCloseButtonId, 550, 385, kRsStandardButtonImageId, 48);
  WnFlatControl *flat = nullptr;
  if (ApGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&flat))) {
    WnWindow *background = flat->GetWindow();
    background->SetID(kApSystemInfoBackgroundId);
    background->SetArea(10, 50, 596, 370);
    background->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
    background->SetFlag(kWnFlagReorderable, false);
    flat->SetOutlineColor(static_cast<u16>(background->MakeFillColor(126, 140, 255)));
    flat->SetSizeMode(0);
    flat->SetOutlineFlags(15);
    window->ChildAdd(background);
    flat->Release();
  }
  WnAppInfoList *list = new WnAppInfoList;
  list->AddRef();
  list->SetFonts(left_font, right_font);
  list->SetOpaque(false);
  list->SetRightColumnTextStart(196);
  PutInfoIntoTheInfoListBoxWindow(list);
  list->SetArea(10, 50, 596, 370);
  window->ChildAdd(static_cast<WnWindow *>(list));
  list->Release();
  return true;
}

// Detach the callback through the actual generic-window interface and release that temporary query reference.
// Shutdown succeeds even when the queried interface is unavailable.
bool ApSysInfoWindow::OnShutdown(WnWindow *window) {
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    generic->SetCallback(nullptr);
    generic->Release();
  }
  return true;
}

// Set the optional minimum and maximum limits to the fixed 600-by-400 information-window size.
// A missing limits output is an original accepted branch.
bool ApSysInfoWindow::OnGetMinMaxInfo(WnWindow *, ScRect *limits) {
  if (limits)
    *limits = {600, 400, 600, 400};
  return true;
}

// Close on either original dismissal button and consume only those button identifiers.
// Both paths post the same system-information completion result.
bool ApSysInfoWindow::OnButtonActivated(WnWindow *window, u32 identifier) {
  if (identifier != kApSystemInfoCloseButtonId && identifier != kUiFrameCloseButtonId)
    return false;
  EndDialog(window, kApSystemInfoCloseButtonId);
  return true;
}

// Load the requested button image, attach the created control at its original position and release the control reference.
bool ApSysInfoWindow::AddImageButton(WnWindow *window, u32 identifier, u32 x, u32 y, u32 image_id, char shortcut) {
  GxBuffer *image;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageHandle handle(image_id, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = handle.GetBuffer();
  }
  if (!image)
    return false;
  WnButton *button;
  {
    ScString caption;
    WnResources *resources = ApGetUiResources();
    const ScRect *area = image->GetBufferArea();
    button = resources->CreateButton(identifier, caption, image, area, static_cast<u32>(static_cast<i32>(shortcut)));
  }
  if (!button)
    return false;
  button->SetButtonFlag(kWnButtonShowText, false);
  button->GetWindow()->GZWinMoveTo(static_cast<i32>(x), static_cast<i32>(y));
  window->ChildAdd(button->GetWindow());
  button->Release();
  return true;
}

#pragma warning(disable : 4996) // The executable uses GetVersion to distinguish its original platform categories.

static bool ScCpuClone;
static const char ScOemKey[] = "Anthony loves to write bad code";
static const char ScOemPlaceholder[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
// Execute a CPUID leaf after the caller has established that the processor supports the instruction.
static void ScCpuReadIdentity(i32 leaf, i32 *registers) {
#if defined(_M_IX86) || defined(_M_X64)
  __cpuid(registers, leaf);
#else
  unsigned int a, b, c, d;
  __cpuid_count(static_cast<unsigned int>(leaf), 0, a, b, c, d);
  registers[0] = static_cast<i32>(a);
  registers[1] = static_cast<i32>(b);
  registers[2] = static_cast<i32>(c);
  registers[3] = static_cast<i32>(d);
#endif
}
#endif

// Test the original x86 ID flag and execute a single CPUID leaf when supported.
// The installed probe leaves the toggled ID flag in place.
static bool ScCpuIdentity([[maybe_unused]] i32 leaf, [[maybe_unused]] i32 *registers) {
#if defined(_M_IX86)
  u32 changed;
  __asm {
    pushfd
    pop eax
    mov ecx, eax
    xor eax, kApCpuIdToggleFlag
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    mov changed, eax
  }
  if (!changed)
    return false;
#elif defined(_M_X64)
  usize flags = __readeflags();
  __writeeflags(flags ^ kApCpuIdToggleFlag);
#elif defined(__i386__)
  u32 changed;
  __asm__ volatile("pushfl\n\t"
                   "popl %%eax\n\t"
                   "movl %%eax, %%ecx\n\t"
                   "xorl %1, %%eax\n\t"
                   "pushl %%eax\n\t"
                   "popfl\n\t"
                   "pushfl\n\t"
                   "popl %%eax\n\t"
                   "xorl %%ecx, %%eax"
      : "=a"(changed) : "i"(kApCpuIdToggleFlag) : "ecx", "cc", "memory");
  if (!changed)
    return false;
#elif defined(__x86_64__)
  usize flags;
  __asm__ volatile("pushfq\n\t"
                   "popq %0\n\t"
                   "xorq %1, %0\n\t"
                   "pushq %0\n\t"
                   "popfq"
      : "=&r"(flags) : "i"(kApCpuIdToggleFlag) : "cc", "memory");
#endif
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  ScCpuReadIdentity(leaf, registers);
  return true;
#else
  return false;
#endif
}

#if defined(_M_IX86) || defined(__i386__)
// Read the original 32-bit x86 flags without changing the processor's current flag state.
static u32 ScCpuLegacyReadFlags() {
#if defined(_M_IX86)
  return static_cast<u32>(__readeflags());
#else
  u32 flags;
  __asm__ volatile("pushfl; popl %0" : "=r"(flags) : : "memory");
  return flags;
#endif
}

// Restore or test x86 flags through the same user-mode POPF behavior as the installed probes.
static void ScCpuLegacyWriteFlags(u32 flags) {
#if defined(_M_IX86)
  __writeeflags(flags);
#else
  __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "cc", "memory");
#endif
}

// Toggle the alignment-check bit only while the stack is aligned, then restore the saved flags immediately.
// Keeping the entire probe in one instruction sequence prevents compiler-generated accesses with AC set.
static bool ScCpuLegacyHasAlignmentCheck() {
  u32 changed;
#if defined(_M_IX86)
  __asm {
    push ebx
    mov bx, sp
    and sp, 0xfffc
    pushfd
    pop eax
    mov ecx, eax
    xor eax, kApCpuAlignmentCheckFlag
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    push ecx
    popfd
    mov sp, bx
    mov changed, eax
    pop ebx
  }
#else
  __asm__ volatile("movl %%esp, %%ebx\n\t"
                   "andl $-4, %%esp\n\t"
                   "pushfl\n\t"
                   "popl %%eax\n\t"
                   "movl %%eax, %%ecx\n\t"
                   "xorl %1, %%eax\n\t"
                   "pushl %%eax\n\t"
                   "popfl\n\t"
                   "pushfl\n\t"
                   "popl %%eax\n\t"
                   "xorl %%ecx, %%eax\n\t"
                   "pushl %%ecx\n\t"
                   "popfl\n\t"
                   "movl %%ebx, %%esp"
      : "=a"(changed) : "i"(kApCpuAlignmentCheckFlag) : "ebx", "ecx", "cc", "memory");
#endif
  return (changed & kApCpuAlignmentCheckFlag) != 0;
}

// Classify the flag behavior of pre-CPUID x86 processors, restoring the original flags after each test.
// The separate clone test consumes undefined DIV flags and does not affect these family decisions.
static i32 ScCpuLegacyFamily() {
  u32 flags = ScCpuLegacyReadFlags();
  ScCpuLegacyWriteFlags(flags & ~kApCpuLegacyHighFlags);
  u32 cleared = ScCpuLegacyReadFlags() & kApCpuLegacyHighFlags;
  ScCpuLegacyWriteFlags(flags);
  if (cleared == kApCpuLegacyHighFlags)
    return 0;
  flags = ScCpuLegacyReadFlags();
  ScCpuLegacyWriteFlags(flags | kApCpuLegacyHighFlags);
  u32 set = ScCpuLegacyReadFlags() & kApCpuLegacyHighFlags;
  ScCpuLegacyWriteFlags(flags);
  if (!set)
    return 2;
  return ScCpuLegacyHasAlignmentCheck() ? 4 : 3;
}
#endif

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
// Exclude the original legacy clone result because its branch reads the undefined flags left by DIV.
[[noreturn]] static void ScCpuUndefinedLegacyClone() {
  assert(false && "legacy clone detection consumes undefined DIV flags");
  std::unreachable();
}
#endif

// Read the original vendor and latch its non-Intel flag before requesting the feature words.
static bool ScCpuInformation(i32 *registers) {
  i32 identity[4];
  if (!ScCpuIdentity(0, identity))
    return false;
  char vendor[13];
  std::memcpy(vendor, &identity[1], 4);
  std::memcpy(vendor + 4, &identity[3], 4);
  std::memcpy(vendor + 8, &identity[2], 4);
  vendor[12] = 0;
  if (std::memcmp(vendor, "GenuineIntel", 12))
    ScCpuClone = true;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  ScCpuReadIdentity(1, registers);
  return true;
#else
  return false;
#endif
}

ScVersion::ScVersion() : module(nullptr), version_data(nullptr), translation(nullptr) {
}

ScVersion::~ScVersion() {
  delete[] version_data;
}

// Load the Windows executable's version resource and its language/code-page translation on first use.
// Native executables have no Windows version resource, leaving the existing unavailable result intact.
void ScVersion::LoadVersionInfo() {
#if defined(_WIN32)
  if (version_data)
    return;
  module = GetModuleHandleA(nullptr);
  char path[MAX_PATH];
  GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
  DWORD handle, size = GetFileVersionInfoSizeA(path, &handle);
  if (!size)
    return;
  version_data = new u8[size];
  if (!GetFileVersionInfoA(path, handle, size, version_data))
    return;
  void *entry;
  UINT entry_size;
  if (!VerQueryValueA(version_data, "\\VarFileInfo\\Translation", &entry, &entry_size)) {
    delete[] version_data;
    version_data = nullptr;
  } else {
    translation = static_cast<const u16 *>(entry);
  }
#endif
}

// Query one original version-string field using the resource's first language and code-page pair.
bool ScVersion::ReadVersionField([[maybe_unused]] const char *name, [[maybe_unused]] ScString *out) {
#if defined(_WIN32)
  if (!version_data) {
    LoadVersionInfo();
    if (!version_data)
      return false;
  }
  char block[256];
  wsprintfA(block, "\\StringFileInfo\\%04lx%04lx\\%s", static_cast<unsigned long>(translation[0]),
      static_cast<unsigned long>(translation[1]), name);
  void *value;
  UINT length;
  bool result = VerQueryValueA(version_data, block, &value, &length) != 0;
  if (result)
    out->FromChar(static_cast<const char *>(value));
  else
    out->Erase(0, out->Strlen());
  return result;
#else
  return false;
#endif
}

// Copy the current executable's actual path if Windows supplies it.
bool ScVersion::GetModuleActualFileName(ScString *out) {
  ScString path = RsApplicationFilePath();
  if (path.begin == path.end)
    return false;
  *out = path;
  return true;
}

bool ScVersion::GetModuleName(ScString *out) {
  return ReadVersionField("FileDescription", out);
}

bool ScVersion::GetModuleVersion(ScString *out) {
  return ReadVersionField("FileVersion", out);
}

bool ScVersion::GetModuleCopyright(ScString *out) {
  return ReadVersionField("LegalCopyright", out);
}

bool ScVersion::GetModuleDebugVersion(ScString *out) {
  return ReadVersionField("Debug Version", out);
}

// Return the selected word of the fixed executable version, or minus one when unavailable.
i32 ScVersion::GetModuleVersion([[maybe_unused]] u32 index) {
#if defined(_WIN32)
  if (!version_data)
    LoadVersionInfo();
  if (!version_data)
    return -1;
  VS_FIXEDFILEINFO *fixed;
  UINT length;
  if (!VerQueryValueA(version_data, "\\", reinterpret_cast<void **>(&fixed), &length))
    return -1;
  switch (index) {
  case kScVersionMajor: return static_cast<i32>(fixed->dwFileVersionMS >> 16);
  case kScVersionMinor: return static_cast<i32>(fixed->dwFileVersionMS & 0xffff);
  case kScVersionBuild: return static_cast<i32>(fixed->dwFileVersionLS >> 16);
  case kScVersionRevision: return static_cast<i32>(fixed->dwFileVersionLS & 0xffff);
  default: return -1;
  }
#else
  return -1;
#endif
}

// XOR the OEM text with the original per-position key and terminate the destination.
void ScVersion::EncryptOEMString(const char *source, char *destination) {
  usize length = std::strlen(source);
  assert(length <= sizeof(ScOemKey));
  for (usize index = 0; index < length; ++index)
    destination[index] = static_cast<char>(source[index] ^ (ScOemKey[index] + 100));
  destination[length] = 0;
}

void ScVersion::DecryptOEMString(const char *source, char *destination) {
  EncryptOEMString(source, destination);
}

#if !defined(_WIN32)
// Read a little-endian word from a bounded executable image.
static bool ScPeWord(const u8 *image, usize size, usize offset, u16 *value) {
  if (offset > size || size - offset < 2)
    return false;
  *value = static_cast<u16>(image[offset] | (static_cast<u16>(image[offset + 1]) << 8));
  return true;
}

// Read a little-endian double word from a bounded executable image.
static bool ScPeDword(const u8 *image, usize size, usize offset, u32 *value) {
  u16 low, high;
  if (!ScPeWord(image, size, offset, &low) || !ScPeWord(image, size, offset + 2, &high))
    return false;
  *value = static_cast<u32>(low) | (static_cast<u32>(high) << 16);
  return true;
}

// Map an executable RVA through its section table to its file offset.
static bool ScPeOffset(const u8 *image, usize size, usize sections, u16 count, u32 rva, usize *offset) {
  for (u16 i = 0; i < count; ++i) {
    usize section = sections + static_cast<usize>(i) * 40;
    u32 virtual_address, raw_size, raw_offset;
    if (!ScPeDword(image, size, section + 12, &virtual_address) ||
        !ScPeDword(image, size, section + 16, &raw_size) ||
        !ScPeDword(image, size, section + 20, &raw_offset))
      return false;
    if (rva >= virtual_address && rva - virtual_address < raw_size) {
      *offset = static_cast<usize>(raw_offset) + (rva - virtual_address);
      return *offset < size;
    }
  }
  return false;
}

static bool ScPeResourceEntry(const u8 *image, usize size, usize root, u32 directory, u32 id, u32 *entry) {
  usize offset = root + directory;
  u16 named, numbered;
  if (offset < root || !ScPeWord(image, size, offset + 12, &named) ||
      !ScPeWord(image, size, offset + 14, &numbered))
    return false;
  for (u32 i = 0; i < numbered; ++i) {
    usize item = offset + 16 + (static_cast<usize>(named) + i) * 8;
    u32 name, target;
    if (!ScPeDword(image, size, item, &name) || !ScPeDword(image, size, item + 4, &target))
      return false;
    if (name == id) {
      *entry = target;
      return true;
    }
  }
  return false;
}

// Ignore characters after the first usable terminator or beyond the destination byte capacity.
static bool ScPeEquivalentResources(const u8 *image, usize size, usize root, usize sections, u16 section_count,
    u32 directory, u16 named, u16 count, usize capacity, u32 *selected) {
  usize first_data = 0, first_size = 0, offset = root + directory;
  if (offset < root || offset > size || size - offset < 16 ||
      static_cast<u32>(named) + count > (size - offset - 16) / 8)
    return false;
  for (u32 i = 0; i < count; ++i) {
    u32 target, rva, length;
    usize data;
    if (!ScPeDword(image, size, offset + 20 + (static_cast<usize>(named) + i) * 8, &target) || (target & 0x80000000u))
      return false;
    usize entry = root + target;
    if (entry < root || entry > size || size - entry < 8 ||
        !ScPeDword(image, size, entry, &rva) || !ScPeDword(image, size, entry + 4, &length) ||
        !ScPeOffset(image, size, sections, section_count, rva, &data) || length > size - data)
      return false;
    usize end = data + length;
    for (u32 index = 0; index <= (kScOemStringResourceId & 15); ++index) {
      u16 characters;
      if (!ScPeWord(image, end, data, &characters))
        return false;
      data += 2;
      if (characters > (end - data) / 2)
        return false;
      length = static_cast<u32>(characters) * 2;
      if (index != (kScOemStringResourceId & 15))
        data += length;
    }
    usize characters = length / 2;
    if (characters >= capacity)
      characters = capacity - 1;
    length = static_cast<u32>(characters * 2);
    for (usize character = 0; character < characters; ++character) {
      u16 word;
      ScPeWord(image, end, data + character * 2, &word);
      if (!word && character >= 8) {
        length = static_cast<u32>(character * 2);
        break;
      }
    }
    if (!i) {
      first_data = data;
      first_size = length;
      *selected = target;
    } else if (length != first_size || std::memcmp(image + first_data, image + data, length)) {
      return false;
    }
  }
  return true;
}

// Locate an OEM string-table resource using host-preferred languages, English and then the first numeric language.
static bool ScPeOemString(const u8 *image, usize size, char *text, usize capacity, usize *converted) {
  u16 magic, sections_count, optional_size;
  u32 pe, signature, resource_rva, target;
  if (!ScPeWord(image, size, 0, &magic) || magic != 0x5a4d || !ScPeDword(image, size, 0x3c, &pe) ||
      !ScPeDword(image, size, pe, &signature) || signature != 0x4550 ||
      !ScPeWord(image, size, static_cast<usize>(pe) + 6, &sections_count) ||
      !ScPeWord(image, size, static_cast<usize>(pe) + 20, &optional_size))
    return false;
  usize optional = static_cast<usize>(pe) + 24, sections = optional + optional_size;
  if (!ScPeWord(image, size, optional, &magic))
    return false;
  usize directory = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
  if (!directory || optional_size < directory + 24 ||
      !ScPeDword(image, size, optional + directory + 16, &resource_rva) || !resource_rva)
    return false;
  usize root;
  if (!ScPeOffset(image, size, sections, sections_count, resource_rva, &root) ||
      !ScPeResourceEntry(image, size, root, 0, 6, &target) || !(target & 0x80000000u) ||
      !ScPeResourceEntry(image, size, root, target & 0x7fffffffu, ((kScOemStringResourceId & 0xffff) >> 4) + 1,
          &target) ||
      !(target & 0x80000000u))
    return false;
  usize languages = root + (target & 0x7fffffffu);
  u16 named, count;
  if (languages < root || !ScPeWord(image, size, languages + 12, &named) ||
      !ScPeWord(image, size, languages + 14, &count) || !count)
    return false;
  // Windows language lookup considers numeric entries and ignores named language entries.
  if (count != 1) {
    u32 language_directory = target & 0x7fffffffu;
    bool found = ScPeResourceEntry(image, size, root, language_directory, 0, &target);
    SDL_Locale **locales = found ? nullptr : SDL_GetPreferredLocales(nullptr);
    for (usize i = 0; locales && locales[i] && !found; ++i) {
      u32 id;
      if (ScHostExactLanguageId(locales[i], &id))
        found = ScPeResourceEntry(image, size, root, language_directory, id, &target) ||
            ScPeResourceEntry(image, size, root, language_directory, id & 0x3ff, &target);
    }
    SDL_free(locales);
    if (!found)
      found = ScPeEquivalentResources(
          image, size, root, sections, sections_count, language_directory, named, count, capacity, &target);
    if (!found)
      found = ScPeResourceEntry(image, size, root, language_directory, 0x0409, &target);
    if (!found && !ScPeDword(image, size, languages + 20 + static_cast<usize>(named) * 8, &target))
      return false;
  } else if (!ScPeDword(image, size, languages + 20 + static_cast<usize>(named) * 8, &target)) {
    return false;
  }
  if (target & 0x80000000u)
    return false;
  u32 data_rva, data_size;
  usize data_entry = root + target, data;
  if (data_entry < root || !ScPeDword(image, size, data_entry, &data_rva) ||
      !ScPeDword(image, size, data_entry + 4, &data_size) ||
      !ScPeOffset(image, size, sections, sections_count, data_rva, &data) || data_size > size - data)
    return false;
  usize end = data + data_size;
  for (u32 index = 0; index <= (kScOemStringResourceId & 15); ++index) {
    u16 length;
    if (!ScPeWord(image, end, data, &length))
      return false;
    data += 2;
    if (length > (end - data) / 2)
      return false;
    if (index == (kScOemStringResourceId & 15)) {
      if (!length)
        return false;
      usize copied = 0;
      for (usize i = 0; i < length; ++i) {
        if (copied == capacity - 1)
          break;
        u16 word;
        ScPeWord(image, end, data + i * 2, &word);
        if (word > 0x7f)
          ScNotImpl("PE OEM resource Windows system ANSI code page");
        text[copied++] = static_cast<char>(word);
        if (!word && copied > 8) {
          *converted = copied - 1;
          return true;
        }
      }
      text[copied] = 0;
      *converted = copied;
      return true;
    }
    data += static_cast<usize>(length) * 2;
  }
  return false;
}
#endif

// Read the current module's OEM resource, strip its prefix and decode a non-placeholder value.
bool ScVersion::GetDecryptedOEMString([[maybe_unused]] ScString *out) {
#if defined(_WIN32)
  if (!module)
    return false;
  char text[40];
  bool loaded = LoadStringA(module, kScOemStringResourceId, text, sizeof(text)) != 0;
  constexpr char prefix[] = "FnordID:";
  if (std::strncmp(text, prefix, sizeof(prefix) - 1))
    return false;
  std::memmove(text, text + sizeof(prefix) - 1, std::strlen(text + sizeof(prefix) - 1) + 1);
  usize length = std::strlen(text);
  if (std::strncmp(text, ScOemPlaceholder, length))
    EncryptOEMString(text, text);
  out->FromChar(text);
  return loaded;
#else
  return false;
#endif
}

// Load another file's OEM string resource and decode its text after the original eight-byte prefix.
bool ScVersion::GetDecryptedOEMStringFromFile([[maybe_unused]] ScString *file, [[maybe_unused]] ScString *out) {
#if defined(_WIN32)
  HMODULE library = LoadLibraryExA(file->ToChar(), nullptr, 3);
  if (!library)
    return false;
  char text[16384], decoded[16384];
  i32 loaded_length = LoadStringA(library, kScOemStringResourceId, text, sizeof(text));
  if (!loaded_length) {
    FreeLibrary(library);
    return false;
  }
  usize length = std::strlen(text);
  assert(loaded_length >= 8);
  if (!std::strncmp(text + 8, ScOemPlaceholder, length)) {
    out->FromChar(text + 8);
  } else {
    DecryptOEMString(text + 8, decoded);
    out->FromChar(decoded);
  }
  FreeLibrary(library);
  return true;
#else
  ScString path = ScNativeFilePath(file->ToChar());
  usize size;
  auto *image = static_cast<u8 *>(SDL_LoadFile(path.ToChar(), &size));
  if (!image)
    return false;
  char text[16384], decoded[16384];
  usize converted;
  bool loaded = ScPeOemString(image, size, text, sizeof(text), &converted);
  SDL_free(image);
  if (!loaded)
    return false;
  usize length = std::strlen(text);
  assert(converted >= 8);
  if (!std::strncmp(text + 8, ScOemPlaceholder, length)) {
    out->FromChar(text + 8);
  } else {
    DecryptOEMString(text + 8, decoded);
    out->FromChar(decoded);
  }
  return true;
#endif
}

// Format the platform and major/minor components returned by the original Windows version API.
bool ScVersion::GetSystemVersion(ScString *out) {
#if defined(_WIN32)
  DWORD version = GetVersion();
  out->Sprintf(version < kScLegacyWindowsVersionFlag ? "Windows NT %u.%u" : "Windows %u.%u", version & 0xff,
      (version >> 8) & 0xff);
#else
  utsname version;
  if (uname(&version) != 0)
    return false;
  out->Sprintf("%s %s", version.sysname, version.release);
#endif
  return true;
}

// Return the original Windows major or minor version byte for indices zero and one.
i32 ScVersion::GetSystemVersion(u32 index) {
#if defined(_WIN32)
  DWORD version = GetVersion();
  if (index == 0)
    return static_cast<i32>(version & 0xff);
  if (index == 1)
    return static_cast<i32>((version >> 8) & 0xff);
  return -1;
#else
  if (index > 1)
    return -1;
  utsname version;
  if (uname(&version) != 0)
    return -1;
  char *end;
  long major = strtol(version.release, &end, 10);
  if (end == version.release || major < 0 || major > INT32_MAX)
    return -1;
  if (index == 0)
    return static_cast<i32>(major);
  if (*end != '.')
    return -1;
  char *start = end + 1;
  long minor = strtol(start, &end, 10);
  if (end == start || minor < 0 || minor > INT32_MAX)
    return -1;
  return static_cast<i32>(minor);
#endif
}

// Report the original platform category, including the native macOS and Linux ports.
ScVersion::SystemType ScVersion::GetSystemType() {
#if defined(_WIN32)
  return static_cast<SystemType>(GetVersion() < kScLegacyWindowsVersionFlag ? 4 : 3);
#elif defined(__APPLE__)
  return static_cast<SystemType>(5);
#elif defined(__linux__)
  return static_cast<SystemType>(6);
#else
  ScNotImpl("system category is unavailable on this operating system");
#endif
}

// Classify CPUID-capable processors using the original Pentium Pro/II model ranges.
// Legacy x86 family detection remains defined even though its independent clone flag is not.
i32 ScVersion::GetCPUVersion() {
  i32 registers[4];
  if (!ScCpuInformation(registers)) {
#if defined(_M_IX86) || defined(__i386__)
    return ScCpuLegacyFamily();
#elif defined(_M_X64) || defined(__x86_64__)
    assert(false && "x86-64 processors require CPUID");
    std::unreachable();
#else
    ScNotImpl("processor family identification requires x86");
#endif
  }
  i32 family = (registers[0] >> 8) & 15;
  if (family == 6) {
    bool available = ScCpuInformation(registers);
    if (!available) {
#if defined(_M_IX86) || defined(__i386__)
      ScCpuLegacyFamily();
      return 6;
#else
      assert(false && "x86-64 processors require CPUID");
      std::unreachable();
#endif
    }
    u32 model = (static_cast<u32>(registers[0]) >> 4) & 7;
    return model <= 1 ? 6 : model <= 3 ? 7 : model <= 5 ? 8 : model == 6 ? 9 : 6;
  }
  return family >= 7 ? family + 3 : family;
}

// Format the original processor-family label and append the non-Intel suffix when detected.
bool ScVersion::GetCPUVersion(ScString *out) {
#if !defined(_M_IX86) && !defined(_M_X64) && !defined(__i386__) && !defined(__x86_64__)
  return GetCPUName(out);
#else
  static const char *const names[] = {"8086/8088", "80186", "80286", "80386", "80486", "Pentium",
      "Pentium Pro", "Pentium II", "Pentium II-'Deschutes'", "Pentium II-'Katmai'", "P7-family"};
  i32 version = GetCPUVersion();
  out->FromChar(version >= 0 && version <= 10 ? names[version] : "P8-family");
  if (IsCPUClone())
    out->Append(" Non-Intel (clone)");
  return true;
#endif
}

// The executable's CPU timing helpers return four DWORDs by value.
struct ScCpuSpeedResult {
  u32 values[4];
};
ScCpuSpeedResult ScMeasureCpuBusyLoop(u32 iterations);
ScCpuSpeedResult ScMeasureCpuClock();

#if defined(_WIN32)
// Read the original CMOS seconds register through the x86 real-time-clock ports.
i32 ScGetCmosTick() {
#if defined(_M_IX86) || defined(_M_X64)
  __outbyte(0x70, 0);
  return __inbyte(0x71);
#else
  ScNotImpl("the original CMOS clock requires x86 port I/O");
#endif
}

// Capture the timestamp counter as high and low words and return its low word.
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
u32 ScCpuTimeStamp(u32 *high, u32 *low) {
  u64 value = __rdtsc();
  *high = static_cast<u32>(value >> 32);
  *low = static_cast<u32>(value);
  return *low;
}
#else
u32 ScCpuTimeStamp(u32 *, u32 *) {
  ScNotImpl("the original timestamp counter requires x86 instructions");
}
#endif

// Measure timestamp cycles between successive CMOS clock ticks at the original raised thread priority.
ScCpuSpeedResult ScMeasureCpuClock() {
  ScCpuSpeedResult result{};
  HANDLE thread = GetCurrentThread();
  i32 priority = GetThreadPriority(thread);
  if (priority != THREAD_PRIORITY_ERROR_RETURN)
    SetThreadPriority(thread, priority + 1);
  i32 first = ScGetCmosTick(), start;
  do {
    start = ScGetCmosTick();
  } while ((start >= first ? (start - first > 0) : start - first + 10) == 0);
  u32 first_high, first_low, last_high, last_low;
  ScCpuTimeStamp(&first_high, &first_low);
  i32 finish;
  do {
    finish = ScGetCmosTick();
  } while ((finish >= start ? (finish - start > 0) : finish - start + 10) == 0);
  ScCpuTimeStamp(&last_high, &last_low);
  if (priority != THREAD_PRIORITY_ERROR_RETURN)
    SetThreadPriority(thread, priority);
  u64 before = (static_cast<u64>(first_high) << 32) | first_low;
  u64 after = (static_cast<u64>(last_high) << 32) | last_low;
  u32 cycles = static_cast<u32>(after - before), speed = cycles / kScCpuClockSampleScale;
  result.values[0] = cycles;
  result.values[1] = kScCpuClockSampleScale * finish - kScCpuClockSampleScale * start;
  result.values[2] = speed;
  result.values[3] = speed + (cycles / kScCpuClockRoundingDivisor - 10u * speed >= 6);
  return result;
}

// Measure ten runs of the original 4000-instruction loop and derive the rounded speed from the shortest run.
#if defined(_M_IX86)
ScCpuSpeedResult ScMeasureCpuBusyLoop(u32 iterations) {
  ScCpuSpeedResult result{};
  LARGE_INTEGER frequency;
  if (!QueryPerformanceFrequency(&frequency))
    return result;
  u32 shortest = UINT32_MAX;
  LARGE_INTEGER first, last;
  for (i32 sample = 0; sample < 10; ++sample) {
    QueryPerformanceCounter(&first);
    __asm {
      mov eax, 80000000h
      mov bx, 0fa0h
    loop_start:
      bsf ecx, eax
      dec bx
      jnz loop_start
    }
    QueryPerformanceCounter(&last);
    u32 elapsed = last.LowPart - first.LowPart;
    if (elapsed < shortest)
      shortest = elapsed;
  }
  u32 divisor = frequency.LowPart / 10;
  assert(divisor != 0);
  u32 duration = kScCpuTimerDurationScale * shortest / divisor;
  if (duration % frequency.LowPart > frequency.LowPart / 2)
    ++duration;
  assert(duration != 0);
  u32 speed = iterations / duration;
  result.values[0] = iterations;
  result.values[1] = duration;
  result.values[2] = speed;
  result.values[3] = speed + (iterations % duration > duration / 2);
  return result;
}
#else
ScCpuSpeedResult ScMeasureCpuBusyLoop(u32) {
  ScNotImpl("the original CPU timing loop requires x86 instructions");
}
#endif

// Identify CPUID-capable processor families and preserve the original non-Intel latch.
// The original non-CPUID return consumes the undefined clone result of the DIV-flags probe.
u32 ScWinCpuId() {
  i32 registers[4];
  if (!ScCpuInformation(registers)) {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    ScCpuUndefinedLegacyClone();
#else
    ScNotImpl("processor identification requires x86");
#endif
  }
  return ((static_cast<u32>(registers[0]) >> 8) & 15) | (ScCpuClone ? 0x8000u : 0);
}

u32 ScWinCpuFeatures() {
  i32 registers[4];
  return ScCpuInformation(registers) ? static_cast<u32>(registers[3]) : 0;
}

// Estimate CPU speed from repeated timestamp-counter and performance-counter intervals.
ScCpuSpeedResult ScMeasureCpuCounter() {
  ScCpuSpeedResult result{};
  LARGE_INTEGER frequency;
  if (!QueryPerformanceFrequency(&frequency))
    return result;
  u32 frequency_low = frequency.LowPart;
  HANDLE thread = GetCurrentThread();
  u32 total_cycles = 0, total_time = 0;
  i32 older = 0, previous = 0;
  LARGE_INTEGER start, current;
  for (i32 sample_count = 1;; ++sample_count) {
    QueryPerformanceCounter(&start);
    current = start;
    i32 priority = GetThreadPriority(thread);
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
      SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    u32 first_cycles = 0, last_cycles = 0;
    while (static_cast<u32>(current.LowPart - start.LowPart) < 50) {
      QueryPerformanceCounter(&current);
      first_cycles = static_cast<u32>(__rdtsc());
    }
    start = current;
    while (static_cast<u32>(current.LowPart - start.LowPart) < 1000) {
      QueryPerformanceCounter(&current);
      last_cycles = static_cast<u32>(__rdtsc());
    }
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
      SetThreadPriority(thread, priority);
    u32 cycles = last_cycles - first_cycles;
    u32 divisor = frequency_low / 10;
    assert(divisor != 0);
    u32 elapsed = (kScCpuTimerDurationScale * static_cast<u32>(current.LowPart - start.LowPart)) / divisor;
    total_cycles += cycles;
    total_time += elapsed;
    if (elapsed % frequency_low > frequency_low / 2)
      ++elapsed;
    assert(elapsed != 0);
    u32 measured = cycles / elapsed;
    if (cycles % elapsed > elapsed / 2)
      ++measured;
    i32 current_speed = static_cast<i32>(measured);
    i64 pair = static_cast<i64>(older) + previous;
    assert(pair >= INT32_MIN && pair <= INT32_MAX);
    i64 sum = pair + current_speed;
    assert(sum >= INT32_MIN && sum <= INT32_MAX);
    if (sample_count >= 3) {
      if (sample_count >= 20)
        break;
      auto close_to_mean = [sum](i32 speed) {
        i64 product = 3 * static_cast<i64>(speed);
        assert(product >= INT32_MIN && product <= INT32_MAX);
        i64 delta = product - sum;
        assert(delta > INT32_MIN && delta <= INT32_MAX);
        return delta >= -3 && delta <= 3;
      };
      if (close_to_mean(current_speed) && close_to_mean(previous) && close_to_mean(older))
        break;
    }
    older = previous;
    previous = current_speed;
  }
  assert(total_time != 0);
  u32 tenths = 10u * total_cycles / total_time;
  if (100u * total_cycles / total_time - 10u * tenths >= 6)
    ++tenths;
  u32 integer = total_cycles / total_time;
  result.values[0] = total_cycles;
  result.values[1] = total_time;
  result.values[2] = integer;
  result.values[3] = integer + (tenths - 10u * integer >= 6);
  return result;
}

// Select the original CPU timing probe using the processor family, feature bits and requested duration.
// The original exception handler returns four zero words when a hardware probe faults in user mode.
ScCpuSpeedResult ScMeasureCpuSpeed(i32 mode) {
  __try {
    static const u32 iterations[] = {0, 0, 0, 115, 47, 43, 38, 38, 38, 38, 38, 38};
    u32 identification = ScWinCpuId(), features = ScWinCpuFeatures();
    u32 family = identification & 0x7fff;
    bool clone = (identification & 0x8000) != 0;
    if (family >= 12)
      family = 11;
    i32 duration = clone ? -1 : mode;
    bool requested = duration > 0;
    if (duration > 150)
      duration = 150;
    u32 count = 4000 * (requested ? static_cast<u32>(duration) : iterations[family]);
    if (!(features & 0x10) || requested) {
      if (family >= 3 || clone)
        return ScMeasureCpuBusyLoop(count);
      return {};
    }
    return duration ? ScMeasureCpuClock() : ScMeasureCpuCounter();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return {};
  }
}
#endif

// Measure CPU speed using the original default mode and return the rounded MHz result.
u32 ScVersion::GetCPUSpeed() {
#if defined(_WIN32)
  return ScMeasureCpuSpeed(0).values[3];
#elif defined(__i386__) || defined(__x86_64__)
  i32 registers[4];
  if (!ScCpuIdentity(1, registers) || !(registers[3] & 0x10))
    return 0;
  u64 frequency = SDL_GetPerformanceFrequency();
  if (!frequency)
    return 0;
  u64 start_clock = SDL_GetPerformanceCounter(), start_cycles = __rdtsc();
  SDL_Delay(20);
  u64 end_cycles = __rdtsc(), end_clock = SDL_GetPerformanceCounter();
  if (end_clock <= start_clock || end_cycles < start_cycles)
    return 0;
  f64 mhz = static_cast<f64>(end_cycles - start_cycles) * static_cast<f64>(frequency) /
      (static_cast<f64>(end_clock - start_clock) * 1000000.0);
  if (mhz < 0 || mhz > UINT32_MAX - 0.5)
    return 0;
  return static_cast<u32>(mhz + 0.5);
#elif defined(__APPLE__)
  u64 frequency = 0;
  usize length = sizeof(frequency);
  if (sysctlbyname("hw.cpufrequency", &frequency, &length, nullptr, 0) != 0 || !frequency)
    return 0;
  return static_cast<u32>((frequency + kScCpuMegahertzRoundingBias) / kScCpuHertzPerMegahertz);
#else
  FILE *file = std::fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
  if (!file)
    return 0;
  unsigned long long frequency = 0;
  int read = std::fscanf(file, "%llu", &frequency);
  std::fclose(file);
  if (read != 1 || frequency > static_cast<u64>(UINT32_MAX) * 1000)
    return 0;
  return static_cast<u32>((frequency + 500) / 1000);
#endif
}

bool ScVersion::GetCPUSpeed(ScString *out) {
  out->Sprintf("%ld Mhz.", static_cast<long>(GetCPUSpeed()));
  return true;
}

// Combine the processor family, measured speed and feature descriptions.
bool ScVersion::GetCPUDescription(ScString *out) {
  ScString version, speed, flags;
  GetCPUVersion(&version);
  GetCPUSpeed(&speed);
  GetCPUFlags(&flags);
  out->Sprintf("%s, %s\n%s\n", version.ToChar(), speed.ToChar(), flags.ToChar());
  return true;
}

// Return the CPUID vendor string or clear the output when identification is unavailable.
bool ScVersion::GetCPUName(ScString *out) {
#if !defined(_WIN32) && !defined(__i386__) && !defined(__x86_64__)
  utsname information;
  if (uname(&information) != 0) {
    out->Erase(0, out->Strlen());
    return false;
  }
  out->FromChar(information.machine);
  return true;
#else
  i32 identity[4];
  if (!ScCpuIdentity(0, identity)) {
    out->Erase(0, out->Strlen());
    return false;
  }
  char name[13];
  std::memcpy(name, &identity[1], 4);
  std::memcpy(name + 4, &identity[3], 4);
  std::memcpy(name + 8, &identity[2], 4);
  name[12] = 0;
  out->FromChar(name);
  return true;
#endif
}

u32 ScVersion::GetCPUFlags() {
  i32 registers[4];
  return ScCpuInformation(registers) ? static_cast<u32>(registers[3]) : 0;
}

// Describe the original MMX, FPU and timestamp-counter feature bits.
bool ScVersion::GetCPUFlags(ScString *out) {
#if !defined(_M_IX86) && !defined(_M_X64) && !defined(__i386__) && !defined(__x86_64__)
  i32 registers[4];
  if (!ScCpuInformation(registers)) {
    out->Erase(0, out->Strlen());
    return false;
  }
  u32 flags = static_cast<u32>(registers[3]);
#else
  u32 flags = GetCPUFlags();
#endif
  out->FromChar(flags & kApCpuMmxFeatureBit ? "CPU has MMX.\n" : "CPU doesn't have MMX.\n");
  out->Append(flags & 1 ? "CPU has an FPU.\n" : "CPU doesn't have an FPU.\n");
  out->Append(flags & 0x10 ? "CPU has Time Stamp instruction.\n" : "CPU doesn't have Time Stamp instruction.\n");
  return true;
}

// Report the original non-Intel processor flag for CPUID-capable processors.
// The non-CPUID clone flag depends on undefined DIV flags and is outside the defined execution domain.
bool ScVersion::IsCPUClone() {
  i32 registers[4];
  if (!ScCpuInformation(registers)) {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    ScCpuUndefinedLegacyClone();
#else
    ScNotImpl("processor identification requires x86");
#endif
  }
  return ScCpuClone;
}

// Read physical memory byte counts from the active operating system.
static bool ScReadHostMemory(u64 *total, u64 *available) {
#if defined(_WIN64)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status))
    return false;
  *total = status.ullTotalPhys;
  *available = status.ullAvailPhys;
#elif defined(_WIN32)
  MEMORYSTATUS status;
  status.dwLength = sizeof(status);
  GlobalMemoryStatus(&status);
  *total = status.dwTotalPhys;
  *available = status.dwAvailPhys;
#else
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0)
    return false;
#if defined(__APPLE__)
  usize total_size = sizeof(*total);
  if (sysctlbyname("hw.memsize", total, &total_size, nullptr, 0) != 0 || total_size != sizeof(*total))
    return false;
  vm_statistics64_data_t statistics;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  mach_port_t host = mach_host_self();
  kern_return_t result = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count);
  mach_port_deallocate(mach_task_self(), host);
  if (result != KERN_SUCCESS)
    return false;
  *available = static_cast<u64>(statistics.free_count) * static_cast<u64>(page_size);
#else
  long total_pages = sysconf(_SC_PHYS_PAGES);
  long available_pages = sysconf(_SC_AVPHYS_PAGES);
  if (total_pages <= 0 || available_pages < 0)
    return false;
  *total = static_cast<u64>(page_size) * static_cast<u64>(total_pages);
  *available = static_cast<u64>(page_size) * static_cast<u64>(available_pages);
#endif
#endif
  return true;
}

// Return physical memory in megabytes rounded upward to the original four-megabyte boundary.
bool ScVersion::GetSystemMemory(u32 *total, u32 *available) {
  u64 total_bytes, available_bytes;
  if (!ScReadHostMemory(&total_bytes, &available_bytes))
    return false;
  u64 total_megabytes = total_bytes >> 20, available_megabytes = available_bytes >> 20;
  if (total_megabytes > UINT32_MAX - 3 || available_megabytes > UINT32_MAX - 3)
    ScFatal(SC_ERROR_INVALID, "System memory exceeds the supported measurement range");
  u32 first = static_cast<u32>(total_megabytes), second = static_cast<u32>(available_megabytes);
  *total = ((first - 1) | 3) + 1;
  *available = ((second - 1) | 3) + 1;
  return true;
}

// Return the Windows user name, or copy the supplied fallback and report failure.
bool ScVersion::GetUserNameA(ScString *out, ScString *fallback) {
#if defined(_WIN32)
  char name[512];
  DWORD length = sizeof(name);
  if (::GetUserNameA(name, &length)) {
    out->FromChar(name);
    return true;
  }
#else
  passwd *account = getpwuid(geteuid());
  if (account && account->pw_name && std::strlen(account->pw_name) < 512) {
    out->FromChar(account->pw_name);
    return true;
  }
#endif
  if (fallback)
    *out = *fallback;
  else
    out->FromChar("?");
  return false;
}
