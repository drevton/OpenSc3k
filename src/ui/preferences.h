#ifndef SC3_UI_PREFERENCES_H
#define SC3_UI_PREFERENCES_H

#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"

// Preferences controls, including the contiguous seven-row music checkbox and label ranges.
enum UiPreferencesControlId : u32 {
  kUiPreferencesSimsHiddenId = 0x02f95af3,
  kUiPreferencesDialogId = 0x02f95ad5,
  kUiPreferencesCancelId = 0x02f95ad6,
  kUiPreferencesApplyId = 0x02f95ad8,
  kUiPreferencesPerformanceOutlineId = 0x02f95ad9,
  kUiPreferencesSoundOutlineId = 0x02f95ada,
  kUiPreferencesGeneralOptionsOutlineId = 0x02f95adb,
  kUiPreferencesVolumeOutlineId = 0x02f95adc,
  kUiPreferencesPreviewOutlineId = 0x02f95add,
  kUiPreferencesPerformanceLabelId = 0x02f95adf,
  kUiPreferencesTrafficLabelId = 0x02f95ae0,
  kUiPreferencesSimsLabelId = 0x02f95ae1,
  kUiPreferencesAnimationsLabelId = 0x02f95ae2,
  kUiPreferencesResolutionLabelId = 0x02f95ae3,
  kUiPreferencesSoundLabelId = 0x02f95ae4,
  kUiPreferencesMusicChoicesLabelId = 0x02f95ae5,
  kUiPreferencesMusicPreviewLabelId = 0x02f95ae6,
  kUiPreferencesGeneralOptionsLabelId = 0x02f95ae7,
  kUiPreferencesResolutionComboId = 0x02f95aea,
  kUiPreferencesTrafficOptionsId = 0x02f95aeb,
  kUiPreferencesTrafficClosestThreeZoomsId = 0x02f95aec,
  kUiPreferencesTrafficClosestTwoZoomsId = 0x02f95aed,
  kUiPreferencesTrafficClosestZoomId = 0x02f95aee,
  kUiPreferencesTrafficOffId = 0x02f95aef,
  kUiPreferencesSimsOptionsId = 0x02f95af0,
  kUiPreferencesSimsClosestTwoZoomsId = 0x02f95af1,
  kUiPreferencesSimsClosestZoomId = 0x02f95af2,
  kUiPreferencesAnimationsOptionsId = 0x02f95af4,
  kUiPreferencesAnimationsOnId = 0x02f95af5,
  kUiPreferencesAnimationsOffId = 0x02f95af6,
  kUiPreferencesShowTipsId = 0x02f95af7,
  kUiPreferencesAutoGoToDisastersId = 0x02f95af8,
  kUiPreferencesMusicEnabledId = 0x02f95af9,
  kUiPreferencesAmbienceEnabledId = 0x02f95afa,
  kUiPreferencesEffectsEnabledId = 0x02f95afb,
  kUiPreferencesThreeDimensionalSoundId = 0x02f95afc,
  kUiPreferencesVolumeBitmapId = 0x02f95afd,
  kUiPreferencesMusicVolumeId = 0x02f95afe,
  kUiPreferencesAmbienceVolumeId = 0x02f95aff,
  kUiPreferencesEffectsVolumeId = 0x02f95b00,
  kUiPreferencesFirstSongCheckboxId = 0x02f95b01,
  kUiPreferencesLastSongCheckboxId = 0x02f95b07,
  kUiPreferencesFirstSongLabelId = 0x02f95b08,
  kUiPreferencesSongScrollbarId = 0x02f95b0f,
  kUiPreferencesPlaySongId = 0x02f95b10,
  kUiPreferencesStopSongId = 0x02f95b11,
};

// Actual C3001401 command owns only its inherited queues and the original input/output descriptors.
class UiPreferencesCommand : public UiCommand {
public:
  UiPreferencesCommand();
  bool Execute() override;
};

class WnOptionControl;

// The original options/preferences callback owns its music and resolution records independently of its generic window.
// Container implementations are native storage; they are not overlays of the original92-byte object's library fields.
class UiPreferencesDialog : public UiWindowCallback {
public:
  enum class UiSubtype : i32;
  UiPreferencesDialog(UiSubtype subtype);
  virtual ~UiPreferencesDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  u8 CreateOptionsPreferencesWindow(WnWindow **out);

protected:
  // Actual private resolution entries own two strings after their adapter GUID and before their dimensions.
  struct UiResolution {
    u8 guid[16];
    ScString driver, description;
    u32 width, height;
  };

  struct UiSongState {
    UiSongState *next, *previous;
    u32 song;
    u8 enabled;
  };

  u32 references;
  UiSubtype subtype;
  u8 state12;
  ScArray<ScString *> titles;
  ScArray<u32> track_ids;
  u8 state32;
  u32 song_count, first_song, selected_song, selected_song_id, word52;
  ScArray<UiResolution *> resolutions;
  u32 initial_resolution, selected_resolution;
  UiSongState *song_states, *song_state_cursor;
  u8 Init(WnWindow *window);
  u8 AddOutline(WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u32 height, u32 color, u8 raised);
  u8 CreateOptionGroup(u32 identifier, const ScRect &area, WnOptionControl **out);
  u8 AddScreenResolutionComboBox(WnWindow *window, FtFont *font);
  u8 AddText(WnWindow *window, u32 identifier, u32 x, u32 y, const ScStringView &text, u32 alignment);
  u8 AddImageButton(WnWindow *window, u32 identifier, u32 x, u32 y, u32 instance, i8 key_equivalent);
  u8 AddCheckboxWithWidth(
      WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u8 checked, const ScStringView &text);
  u8 AddHorizontalSlider(WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u32 minimum, u32 maximum, u32 value,
      ScStringView *text);
  void EnableButtonEffects(WnWindow *window, u32 identifier, u8 enabled);
  u8 InitMusicList(WnWindow *window);
  void EndDialogEffects(WnWindow *window, u32 command);
  void SelectMusicTitleEffects(WnWindow *window, u32 index, u8 selected);
  void ScrollMusicEffects(WnWindow *window, u32 first);
  void GrayMusicEffects(WnWindow *window, u8 disabled);
  u8 SaveWindowSettingsToPrefs(WnWindow *window);
  u8 OnButtonActivated(WnWindow *window, u32 command);
};

#endif
