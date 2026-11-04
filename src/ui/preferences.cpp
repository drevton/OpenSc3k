#include "ui/preferences.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/window.h"
#include "window/wn_resource_provider.h"

// The preferences command describes its input mode byte and output acceptance byte.
enum UiPreferencesParameterId : u32 {
  kUiPreferencesModeParameterId = 0x230014bb,
  kUiPreferencesAcceptedParameterId = 0x30014c5,
};

// Song preview buttons retain separate installed play and stop images.
enum UiSongPreviewArtwork : u32 {
  kUiSongPreviewPlayImageId = 0x82fab6d2,
  kUiSongPreviewStopImageId = 0x22fab6de,
};

// Apply the original play/stop, checkbox-preview, confirmation and frame-close commands without changing their gates.
// Apply ignores the save reply, notifies preferences observers, scopes restart text, then completes the modal window.
u8 UiPreferencesDialog::OnButtonActivated(WnWindow *window, u32 command) {
  if (command == kUiPreferencesPlaySongId) {
    if (song_count) {
      UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      UiGetBoxX()->Event(kAuEventPlaySong, selected_song_id, 0, 0, 0);
    }
    return 1;
  }
  if (command == kUiPreferencesApplyId) {
    UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    SaveWindowSettingsToPrefs(window);
    ScMessage changed = {kApMsgPreferencesChanged, reinterpret_cast<uptr>(UiGetPreferences()), 0, 0};
    UiGetMessageServer()->MessageSend(changed);
    if (initial_resolution != selected_resolution) {
      UiTextRef body_ref(kRsMessageSettingsRequireRestartText, kRsMessageTextGroup),
          caption_ref(kRsGamePreferencesText, kRsGameTextGroup);
      ScString body, caption;
      if (body_ref.IsResolved())
        body_ref.CopyTo(&body);
      if (caption_ref.IsResolved())
        caption_ref.CopyTo(&caption);
      if (UiGetWindowManager()->GZMsgBox(body, caption, 0x30003, true, 0) == kWnYesButtonId) {
        UiGetApplication()->SetRestartOnExit(true);
        UiGetApplication()->Quit(true, true);
      }
    }
    EndDialogEffects(window, kUiPreferencesApplyId);
    return 1;
  }
  if ((command > kUiPreferencesAnimationsOffId && command <= kUiPreferencesThreeDimensionalSoundId) ||
      (command > kUiPreferencesEffectsVolumeId && command <= kUiPreferencesLastSongCheckboxId)) {
    UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    WnButton *checkbox = nullptr;
    if (window->GetChildAs(command, IID_WnButton, reinterpret_cast<void **>(&checkbox)) && checkbox) {
      bool checked = checkbox->IsChecked();
      if (command == kUiPreferencesMusicEnabledId) {
        i32 volume = 0;
        if (checked) {
          WnSliderControl *slider = nullptr;
          if (window->GetChildAs(
                  kUiPreferencesMusicVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
              slider) {
            volume = slider->GetValue();
            slider->Release();
          }
          GrayMusicEffects(window, 0);
          SelectMusicTitleEffects(window, selected_song, 1);
        } else
          GrayMusicEffects(window, 1);
        UiGetBoxX()->Event(kAuEventSetMusicVolume, (u32)volume, 0, 0, 0);
      } else if (command == kUiPreferencesAmbienceEnabledId) {
        i32 volume = 0;
        if (checked) {
          WnSliderControl *slider = nullptr;
          if (window->GetChildAs(
                  kUiPreferencesAmbienceVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
              slider) {
            volume = slider->GetValue();
            slider->Release();
          }
        }
        UiGetBoxX()->Event(kAuEventSetAmbienceVolume, (u32)volume, 0, 0, 0);
      } else if (command == kUiPreferencesEffectsEnabledId) {
        i32 volume = 0;
        if (checked) {
          WnSliderControl *slider = nullptr;
          if (window->GetChildAs(
                  kUiPreferencesEffectsVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
              slider) {
            volume = slider->GetValue();
            slider->Release();
          }
        }
        UiGetBoxX()->Event(kAuEventSetEffectsVolume, (u32)volume, 0, 0, 0);
      } else if (command == kUiPreferencesThreeDimensionalSoundId)
        UiGetBoxX()->Event(kAuEventSetDirectSound3D, (u8)checked, 0, 0, 0);
      else if (command > kUiPreferencesEffectsVolumeId && command <= kUiPreferencesLastSongCheckboxId) {
        u32 index = first_song + command - kUiPreferencesFirstSongCheckboxId;
        if (index < song_count) {
          u32 *ids = track_ids.data;
          ApBoxX *audio = UiGetBoxX();
          audio->Event(kAuEventSetSongEnabled, ids[index], (u8)checked, 0, 0);
          ids = track_ids.data;
          ApPreferences *preferences = UiGetPreferences();
          preferences->EnableDisableSong(ids[index], checked);
        }
      }
      checkbox->Release();
    }
    return 1;
  }
  if (command == kUiPreferencesStopSongId) {
    if (song_count) {
      UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      UiGetBoxX()->Event(kAuEventStopSong, 0, 0, 0, 0);
    }
    return 1;
  }
  if (command != kUiFrameCloseButtonId)
    return 0;
  UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  EndDialogEffects(window, kUiPreferencesCancelId);
  return 1;
}

// Create the preferences label, applying its language-dependent width after the font and low-WORD text pixel.
// The original ignores attachment failure and releases the label only after its final primary-window acquisition.
u8 UiPreferencesDialog::AddText(
    WnWindow *window, u32 identifier, u32 x, u32 y, const ScStringView &text, u32 alignment) {
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(identifier, text);
  label->GetWindow()->SetID(identifier);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  u16 pixel = UiGetGraphics()->ConvertRGBValueToNative16Bit(121, 125, 181);
  label->SetTextColor(pixel);
  i32 language = UiGetLanguageManager()->GetCurrentLanguage();
  if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai)
    label->GetWindow()->SetW(180);
  else
    label->GetWindow()->SetW(140);
  label->SetAlignment(alignment);
  label->GetWindow()->GZWinMoveTo((i32)x, (i32)y);
  window->ChildAdd(label->GetWindow());
  label->Release();
  return 1;
}

// Resolve the image key before creating the button with its full-area rectangle and signed key byte.
// Retire the scoped image reference before using the borrowed resource-cache buffer.
u8 UiPreferencesDialog::AddImageButton(
    WnWindow *window, u32 identifier, u32 x, u32 y, u32 instance, i8 key_equivalent) {
  GxBuffer *image;
  {
    const GxColorType format(GxColorType::Color::RGB16);
    UiImageRef reference(instance, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = reference.GetBuffer();
  }
  if (!image)
    return 0;
  WnButton *button;
  {
    ScString empty;
    WnResources *resources = UiGetWindowResources();
    const ScRect *area = image->GetBufferArea();
    button = resources->CreateButton(identifier, empty, image, area, (u32)(i32)key_equivalent);
  }
  if (!button)
    return 0;
  WnWindow *primary = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  primary->GZWinMoveTo((i32)x, (i32)y);
  window->ChildAdd(primary);
  button->Release();
  return 1;
}

// Create the original checkbox control, then configure its state, gutters, font and independently queried text colors.
// Attachment failure destroys the reached window before the held button interface is released, and returns zero.
u8 UiPreferencesDialog::AddCheckboxWithWidth(
    WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u8 checked, const ScStringView &text) {
  WnButton *button = UiGetWindowResources()->CreateCheckBox(identifier, text, nullptr, nullptr, 0);
  button->SetChecked(checked != 0);
  button->SetGutters(3, 1, 2, 2);
  button->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  button->SetFontColor(kWnButtonTextEnabled, UiGetWindowResources()->GetColor(kWnThemeTextColorId));
  button->SetFontColor(kWnButtonTextHovered, UiGetWindowResources()->GetColor(kWnThemeTextColorId));
  button->SetFontColor(kWnButtonTextDisabled, UiGetWindowResources()->GetColor(kWnThemeTextColorId));
  button->GetWindow()->SetW((i32)width);
  button->GetWindow()->GZWinMoveTo((i32)x, (i32)y);
  u8 result = 1;
  if (!window->ChildAdd(button->GetWindow())) {
    WnWindowManager *manager = UiGetWindowManager();
    manager->DestroyWindow(button->GetWindow());
    result = 0;
  }
  button->Release();
  return result;
}

// Create the horizontal slider with its separate range/value arguments, then caption, size, position and attach it.
// A failed attachment shuts down its freshly acquired primary view before the held slider interface is released.
u8 UiPreferencesDialog::AddHorizontalSlider(WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u32 minimum,
    u32 maximum, u32 value, ScStringView *text) {
  WnSliderControl *slider = UiGetWindowResources()->CreateSlider(identifier, 1, (i32)minimum, (i32)maximum, (i32)value);
  if (!slider)
    return 0;
  slider->GetWindow()->SetCaption(*text);
  slider->GetWindow()->SetW((i32)width);
  slider->GetWindow()->GZWinMoveTo((i32)x, (i32)y);
  u8 result = window->ChildAdd(slider->GetWindow());
  if (!result)
    slider->GetWindow()->Shutdown();
  slider->Release();
  return result;
}

void UiPreferencesDialog::EnableButtonEffects(WnWindow *window, u32 identifier, u8 enabled) {
  WnWindow *child = window->GetChildWindowFromID(identifier);
  if (child)
    child->SetFlag(kWnFlagEnabled, enabled != 0);
}

// Configure the outline through fresh primary getters, then attach it before retiring the acquired control.
// A refused or null factory publication preserves its Boolean reply without compensating cleanup.
u8 UiPreferencesDialog::AddOutline(WnWindow *window, u32 identifier, u32 x, u32 y, u32 width, u32 height,
    u32 color, u8 raised) {
  WnOutlineControl *outline = nullptr;
  bool result =
      UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline));
  if (result && outline) {
    outline->SetFlags((raised ? kWnOutlineInset : kWnOutlinePlain) | kWnOutlineFill);
    outline->SetFillColor(color);
    ScRect area = {0, 0, (i32)width, (i32)height};
    outline->GetWindow()->SetArea(area);
    outline->GetWindow()->SetID(identifier);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    outline->GetWindow()->GZWinMoveTo((i32)x, (i32)y);
    window->ChildAdd(outline->GetWindow());
    outline->Release();
    return 1;
  }
  return result;
}

// Create the option group with an empty caption and no initial rectangle, then configure its font and colors.
// Publish the held control only after applying the caller's area; a missing group leaves the output untouched.
u8 UiPreferencesDialog::CreateOptionGroup(u32 identifier, const ScRect &area, WnOptionControl **out) {
  WnResources *resources = UiGetWindowResources();
  ScString empty;
  WnOptionControl *group = resources->CreateOptGrp(identifier, empty, nullptr);
  if (!group)
    return 0;
  u32 color = resources->GetColor(kWnThemeTextColorId);
  group->SetButtonFont(resources->GetFont(kWnFontUiSmall));
  group->SetFontColor(2, color);
  group->SetFontColor(3, color);
  group->SetFontColor(4, color);
  group->SetOptionFlag(1, 0);
  group->SetOptionFlag(8, 0);
  group->SetGutters(0, 0);
  group->GetWindow()->SetArea(area);
  *out = group;
  return 1;
}

// Populate supported16-bit modes in adapter order, recording live graphics preferences before attaching the combo.
// Select the last matching mode/GUID or the first available entry, and release the queried manager before attachment.
u8 UiPreferencesDialog::AddScreenResolutionComboBox(WnWindow *window, FtFont *font) {
  ScString empty;
  ApGameInterface *application = UiGetApplication();
  WnComboControl *combo = nullptr;
  if (!UiGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&combo)))
    return 0;
  combo->SetEditable(0);
  combo->SetFont(font);
  combo->GetWindow()->SetID(kUiPreferencesResolutionComboId);
  combo->SetHighlightColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
  combo->GetWindow()->SetArea(18, 314, 194, 334);
  ScString description;
  GxGraphics *graphics = UiGetGraphics();
  GxDeviceManager *manager = nullptr;
  if (!graphics->QueryInterface(IID_GxDeviceManager, reinterpret_cast<void **>(&manager))) {
    if (!window->ChildAdd(combo->GetWindow())) {
      combo->GetWindow()->Shutdown();
      combo->Release();
      return 0;
    }
    assert(!resolutions.size);
    combo->Release();
    return 0;
  }
  GxAdapters *adapters = manager->GetAdapters();
  GxAdapter *selected = manager->GetSelectedAdapter();
  GxMode current = graphics->GetGameMetrics();
  ApPreferences *preferences = UiGetPreferences();
  bool accelerated = manager->IsHardware3DAccellerated();
  preferences->VideoPrefs()->use_hardware_acceleration = static_cast<u8>(accelerated);
  preferences = UiGetPreferences();
  u8 fullscreen = graphics->IsFullScreenMode();
  preferences->VideoPrefs()->windowed = fullscreen == 0;
  memcpy(UiGetPreferences()->VideoPrefs()->driver_identifier, selected->guid, 16);
  for (GxAdapterLink *link = adapters->head.next; link != &adapters->head; link = link->next) {
    GxAdapter *adapter = &static_cast<GxAdapterNode *>(link)->adapter;
    ScString label(adapter->description);
    for (u32 i = 0; i < adapter->modes.rows.size; ++i) {
      GxMode mode = adapter->modes.rows.data[i];
      if (application->IsStartupResolutionOK(mode.width, mode.height) && mode.depth == 16) {
        UiResolution record;
        memcpy(record.guid, adapter->guid, 16);
        record.driver = adapter->description;
        record.description = adapter->name;
        record.width = mode.width;
        record.height = mode.height;
        *resolutions.Append() = new UiResolution(record);
      }
    }
  }
  for (u32 i = 0; i < resolutions.size; ++i) {
    UiResolution record(*resolutions.data[i]);
    ScString text;
    text.Sprintf("%ldx%ld %s", static_cast<long>(record.width), static_cast<long>(record.height),
        record.driver.begin);
    combo->InsertString(text, -2);
  }
  u8 current_guid[16];
  memcpy(current_guid, selected->guid, 16);
  manager->Release();
  if (!window->ChildAdd(combo->GetWindow())) {
    combo->GetWindow()->Shutdown();
    combo->Release();
    return 0;
  }
  bool matched = false;
  for (u32 i = 0; i < resolutions.size; ++i) {
    UiResolution record(*resolutions.data[i]);
    if (record.width == current.width && record.height == current.height && !memcmp(record.guid, current_guid, 16)) {
      matched = true;
      initial_resolution = i;
    }
  }
  if (!matched) {
    if (!resolutions.size) {
      combo->Release();
      return 0;
    }
    initial_resolution = 0;
  }
  selected_resolution = initial_resolution;
  combo->SetSelection(static_cast<i32>(initial_resolution), false);
  combo->Release();
  return 1;
}

UiPreferencesCommand::UiPreferencesCommand() {
  AddInputParameter(11, kUiPreferencesModeParameterId);
  AddOutputParameter(11, kUiPreferencesAcceptedParameterId);
}

// Pause the captured city and consume at most one raw Boolean byte, selecting subtype one only for byte one.
// Retire the modal window and callback before queuing the accept byte, then unpause the originally captured city.
bool UiPreferencesCommand::Execute() {
  u8 accepted = 0;
  CtCity *city = UiGetCity();
  if (city)
    city->SimHiddenPause();
  u8 input = 1;
  if (InputQueueSize()) {
    ScVariant *argument;
    PopFrontInput(&argument);
    input = *argument->AsBool();
    argument->Release();
  }
  UiPreferencesDialog *dialog = new UiPreferencesDialog(static_cast<UiPreferencesDialog::UiSubtype>(input == 1));
  dialog->AddRef();
  WnWindow *window = nullptr;
  if (dialog->CreateOptionsPreferencesWindow(&window)) {
    i32 result = UiGetWindowManager()->DoModalWin(nullptr, window);
    if (result == kUiPreferencesCancelId)
      accepted = 0;
    else if (result == kUiPreferencesApplyId)
      accepted = 1;
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
    window = nullptr;
  }
  dialog->Release();
  PushBackOutput(new ScValue(bool(accepted)));
  if (city)
    city->SimHiddenUnpause();
  return true;
}

UiPreferencesDialog::UiPreferencesDialog(UiSubtype value)
    : references(0), subtype(value), state12(0), state32(1), song_count(0), first_song(0), selected_song(0),
      selected_song_id(0), word52(0), initial_resolution(0), selected_resolution(0), song_states(nullptr) {
}

// Retire saved song states before resolution strings, then track storage and music titles in their insertion order.
UiPreferencesDialog::~UiPreferencesDialog() {
  while (song_states) {
    UiSongState *next = song_states->next;
    delete song_states;
    song_states = next;
  }
  for (u32 i = 0; i < resolutions.size; ++i)
    delete resolutions.data[i];
  resolutions.Clear();
  track_ids.Clear();
  for (u32 i = 0; i < titles.size; ++i)
    delete titles.data[i];
  titles.Clear();
}

// Rejected identities clear the output; successful queries require the original writable output.
bool UiPreferencesDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Use the folded module-backed retain, incrementing this owner before the director callback.
// Return the live field after that callback rather than a saved increment result.
u32 UiPreferencesDialog::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Above one reference, decrement before removing the module reference and return the callback-visible live count.
// Counts zero or one remove the module reference before unconditional deletion without writing a decremented field.
u32 UiPreferencesDialog::Release() {
  if (references > 1) {
    --references;
    UiGetDirector()->RemoveRef();
    return references;
  }
  UiGetDirector()->RemoveRef();
  delete this;
  return 0;
}

// Configure the generic view in original order and transfer its acquired owner through the returned primary window.
// Installing this callback retains it; no extra primary retain or generic release is inserted into that transfer.
u8 UiPreferencesDialog::CreateOptionsPreferencesWindow(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  generic->SetStyle(kUiGenericDraggable, UiGetCity() != nullptr);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericBackButton, 0);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kUiPreferencesDialogId);
  return 1;
}

bool UiPreferencesDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_Command:
    switch (first) {
    case 3:
      UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      return 1;
    case 11: {
      WnComboControl *combo = nullptr;
      if (!window->GetChildAs((u32)second, IID_WnComboControl, reinterpret_cast<void **>(&combo)))
        return 0;
      if ((u32)combo->GetSelection() != selected_resolution) {
        selected_resolution = (u32)combo->GetSelection();
        ApVideoSettings *video = UiGetPreferences()->VideoPrefs();
        video->resolution_x = resolutions.data[selected_resolution]->width;
        video->resolution_y = resolutions.data[selected_resolution]->height;
        memcpy(video->driver_identifier, resolutions.data[selected_resolution]->guid, 16);
      }
      combo->Release();
      return 1;
    }
    case 12:
    case 13: {
      u32 identifier = (u32)second;
      WnWindow *control = window->GetChildWindowFromID(identifier);
      WnSliderControl *slider = nullptr;
      if (!control->QueryInterface(IID_WnSliderControl, reinterpret_cast<void **>(&slider)))
        return 0;
      i32 value = slider->GetValue();
      slider->Release();
      if (identifier == kUiPreferencesSongScrollbarId) {
        ScrollMusicEffects(window, (u32)value);
        return 1;
      }
      u32 checkbox_id, audio_message, preview_message;
      if (first == kWnCommandSliderValueChanged && identifier == kUiPreferencesMusicVolumeId) {
        checkbox_id = kUiPreferencesMusicEnabledId;
        audio_message = 1002;
        preview_message = 1018;
      } else if (first == kWnCommandSliderValueChanged && identifier == kUiPreferencesAmbienceVolumeId) {
        checkbox_id = kUiPreferencesAmbienceEnabledId;
        audio_message = 1004;
        preview_message = 1019;
      } else if (first == kWnCommandSliderDragFinished && identifier == kUiPreferencesEffectsVolumeId) {
        checkbox_id = kUiPreferencesEffectsEnabledId;
        audio_message = 1003;
        preview_message = 1020;
      } else
        return 1;
      WnButton *checkbox = nullptr;
      if (!window->GetChildAs(checkbox_id, IID_WnButton, reinterpret_cast<void **>(&checkbox)) || !checkbox)
        return 1;
      if (checkbox->IsChecked()) {
        UiGetBoxX()->Event(audio_message, (u32)value, 0, 0, 0);
        UiGetBoxX()->Event(preview_message, 0, 0, 0, 0);
      }
      checkbox->Release();
      return 1;
    }
    case 1:
      return OnButtonActivated(window, (u32)second);
    default:
      return 0;
    }
  case kWnMsg_Shutdown:
    return 1;
  case kWnMsg_ClientConstraints:
    return 0;
  case kWnMsg_KeyDown: {
    u32 key = (u32)first;
    if (key == 13) {
      EndDialogEffects(window, kUiPreferencesApplyId);
      return 1;
    }
    if (key == 27 || (key == 115 && (second & 4))) {
      EndDialogEffects(window, kUiPreferencesCancelId);
      return 1;
    }
    if (key == 115)
      return 1;
    if (key != 38 && key != 40)
      return 0;
    WnScrollbarControl *scrollbar = nullptr;
    if (!window->GetChildAs(
            kUiPreferencesSongScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar)))
      return 0;
    u32 next = (u32)scrollbar->GetValue();
    if (key == 38)
      --next;
    else {
      ++next;
      u32 last = song_count - 1;
      if (last < next)
        next = last;
    }
    scrollbar->SetValue((i32)next);
    scrollbar->Release();
    return 1;
  }
  case kWnMsg_MouseDownL: {
    if (!state32)
      return 0;
    u32 shown = track_ids.size < 7 ? track_ids.size : 7;
    i32 x = (i32)(u32)first, y = (i32)(u32)second;
    for (u32 row = 0; row < shown; ++row) {
      ScRect area;
      window->GetChildWindowFromID(kUiPreferencesFirstSongLabelId + row)->GetArea(&area);
      if (x < area.left || y < area.top || x > area.right || y > area.bottom)
        continue;
      SelectMusicTitleEffects(window, selected_song, 0);
      selected_song = first_song + row;
      SelectMusicTitleEffects(window, selected_song, 1);
      selected_song_id = track_ids.data[selected_song];
      return 1;
    }
    return 0;
  }
  case kWnMsg_Init:
    return Init(window);
  default:
    return 0;
  }
}

// Stop preview music first, restore cancellation state through fresh services, then post the original completion word.
void UiPreferencesDialog::EndDialogEffects(WnWindow *window, u32 command) {
  UiGetBoxX()->Event(1011, 0, 0, 0, 0);
  if (command == kUiPreferencesCancelId) {
    if (UiGetPreferences()->SoundPrefs()->music == 1) {
      ApPreferences *preferences = UiGetPreferences();
      ApBoxX *audio = UiGetBoxX();
      i32 volume = preferences->SoundPrefs()->music_volume;
      audio->Event(kAuEventSetMusicVolume, (u32)volume, 0, 0, 0);
    } else
      UiGetBoxX()->Event(kAuEventSetMusicVolume, 0, 0, 0, 0);
    if (UiGetPreferences()->SoundPrefs()->ambience == 1) {
      ApPreferences *preferences = UiGetPreferences();
      ApBoxX *audio = UiGetBoxX();
      i32 volume = preferences->SoundPrefs()->ambience_volume;
      audio->Event(kAuEventSetAmbienceVolume, (u32)volume, 0, 0, 0);
    } else
      UiGetBoxX()->Event(kAuEventSetAmbienceVolume, 0, 0, 0, 0);
    if (UiGetPreferences()->SoundPrefs()->sound_effects == 1) {
      ApPreferences *preferences = UiGetPreferences();
      ApBoxX *audio = UiGetBoxX();
      i32 volume = preferences->SoundPrefs()->sound_effects_volume;
      audio->Event(kAuEventSetEffectsVolume, (u32)volume, 0, 0, 0);
    } else
      UiGetBoxX()->Event(kAuEventSetEffectsVolume, 0, 0, 0, 0);
    ApPreferences *preferences = UiGetPreferences();
    ApBoxX *audio = UiGetBoxX();
    u8 enable_3d = preferences->SoundPrefs()->enable_3d_sound;
    audio->Event(kAuEventSetDirectSound3D, enable_3d, 0, 0, 0);
    song_state_cursor = song_states;
    while (song_state_cursor) {
      u32 song = song_state_cursor->song;
      u8 enabled = song_state_cursor->enabled;
      UiGetBoxX()->Event(kAuEventSetSongEnabled, song, enabled, 0, 0);
      UiGetPreferences()->EnableDisableSong(song, enabled != 0);
      song_state_cursor = song_state_cursor->next;
    }
    u32 initial = initial_resolution;
    if (initial != selected_resolution) {
      selected_resolution = initial;
      ApVideoSettings *video = UiGetPreferences()->VideoPrefs();
      video->resolution_x = resolutions.data[selected_resolution]->width;
      video->resolution_y = resolutions.data[selected_resolution]->height;
      memcpy(video->driver_identifier, resolutions.data[selected_resolution]->guid, 16);
    }
  }
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), command);
}

// Resolution enumeration remains partial at the explicitly unavailable graphics-manager adapter operations.
u8 UiPreferencesDialog::Init(WnWindow *window) {
  UiGetBoxX()->Event(1010, 0, 0, 0, 0);
  WnResources *resources = UiGetWindowResources();
  ScString empty;
  if (!window)
    return 0;
  {
    UiTextRef reference(kRsGamePreferencesText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    window->SetCaption(caption);
  }
  state12 = 0;
  FtFont *font = resources->GetFont(kWnFontUiSystem);
  AddOutline(
      window, kUiPreferencesPerformanceOutlineId, 6, 42, 200, 300, resources->GetColor(kWnThemeBackgroundColorId), 1);
  AddOutline(
      window, kUiPreferencesSoundOutlineId, 212, 42, 262, 300, resources->GetColor(kWnThemeBackgroundColorId), 1);
  AddOutline(window, kUiPreferencesGeneralOptionsOutlineId, 6, 346, 468, 76,
      resources->GetColor(kWnThemeBackgroundColorId), 1);
  AddOutline(window, kUiPreferencesVolumeOutlineId, 372, 46, 96, 110, resources->GetColor(kWnThemePanelFillColorId), 0);
  AddOutline(
      window, kUiPreferencesPreviewOutlineId, 218, 310, 250, 28, resources->GetColor(kWnThemePanelFillColorId), 0);
  {
    UiTextRef reference(kRsGamePerformanceText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesPerformanceLabelId, 12, 46, caption, 1);
  }
  {
    UiTextRef reference(kRsGameTrafficVisibleText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesTrafficLabelId, 12, 68, caption, 1);
  }
  {
    UiTextRef reference(kRsGameSimsVisibleText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesSimsLabelId, 12, 162, caption, 1);
  }
  {
    UiTextRef reference(kRsGameOtherAnimationsText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesAnimationsLabelId, 12, 238, caption, 1);
  }
  {
    UiTextRef reference(kRsGameScreenResolutionText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesResolutionLabelId, 12, 296, caption, 1);
  }
  {
    UiTextRef reference(kRsGameSoundText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesSoundLabelId, 218, 46, caption, 1);
  }
  {
    UiTextRef reference(kRsGameMusicChoicesText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesMusicChoicesLabelId, 218, 160, caption, 1);
  }
  {
    UiTextRef reference(kRsGamePreviewHighlightedScoreText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesMusicPreviewLabelId, 224, 315, caption, 1);
  }
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiPreferencesMusicPreviewLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetLabelFlag(kWnLabelUseFontColors, false);
    label->SetTextColor(resources->GetColor(kWnThemeTextColorId));
    label->GetWindow()->SetW(160);
    label->Release();
  }
  {
    UiTextRef reference(kRsGameGeneralOptionsText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    AddText(window, kUiPreferencesGeneralOptionsLabelId, 12, 346, caption, 1);
  }
  AddImageButton(window, kUiPreferencesApplyId, 432, 434, kRsStandardButtonImageId, 48);
  AddImageButton(window, kUiPreferencesPlaySongId, 414, 313, kUiSongPreviewPlayImageId, 48);
  AddImageButton(window, kUiPreferencesStopSongId, 438, 313, kUiSongPreviewStopImageId, 48);
  EnableButtonEffects(window, kUiPreferencesApplyId, 1);
  EnableButtonEffects(window, kUiPreferencesPlaySongId, 1);
  EnableButtonEffects(window, kUiPreferencesStopSongId, 1);
  InitMusicList(window);
  WnOutlineControl *outline = nullptr;
  if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)) &&
      outline) {
    outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
    outline->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
    ScRect area = {0, 0, 234, 130};
    outline->GetWindow()->SetArea(area);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    outline->GetWindow()->GZWinMoveTo(218, 178);
    window->ChildAdd(outline->GetWindow());
    outline->Release();
    outline = nullptr;
  }
  for (u32 row = 0, y = 182; row < 7; ++row, y += 18) {
    AddCheckboxWithWidth(window, kUiPreferencesFirstSongCheckboxId + row, 223, y - 2, 15, 0, empty);
    AddText(window, kUiPreferencesFirstSongLabelId + row, 238, y, empty, 1);
  }
  WnScrollbarControl *scrollbar = nullptr;
  if (UiGetCom()->GetClassObject(kWnScrollbarClassId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar))) {
    scrollbar->GetWindow()->SetID(kUiPreferencesSongScrollbarId);
    scrollbar->GetWindow()->SetSize(16, 130);
    scrollbar->GetWindow()->GZWinMoveTo(453, 178);
    GxBuffer *image = resources->GetImage(kWnThemeScrollbarImageId);
    if (image) {
      i32 height = image->Height(), width = image->Width();
      ScRect area = {0, 0, width / 12, height};
      scrollbar->ImageSetup(area, image, 0);
    }
    scrollbar->SetRange(0, (i32)(song_count - 1));
    scrollbar->SetLineCount(1);
    scrollbar->SetPageCount(7);
    if (!window->ChildAdd(scrollbar->GetWindow())) {
      scrollbar->GetWindow()->Shutdown();
      scrollbar->Release();
      return 0;
    }
    scrollbar->Release();
  }
  u32 song;
  UiGetBoxX()->Event(kAuEventCurrentSong, reinterpret_cast<uptr>(&song), 0, 0, 0);
  selected_song_id = song;
  word52 = song;
  u32 index = 0;
  u32 *ids = track_ids.data;
  while (index < track_ids.size && ids[index] != song)
    ++index;
  if (index < track_ids.size) {
    selected_song = index;
    first_song = index;
  } else if (song_count) {
    selected_song = 0;
    first_song = 0;
    selected_song_id = track_ids.data[0];
    SelectMusicTitleEffects(window, 0, 1);
  }
  state32 = 1;
  ScrollMusicEffects(window, 0);
  state32 = 1;
  if (UiGetPreferences()->SoundPrefs()->music) {
    GrayMusicEffects(window, 0);
    SelectMusicTitleEffects(window, selected_song, 1);
  } else
    GrayMusicEffects(window, 1);
  AddScreenResolutionComboBox(window, font);
  {
    UiTextRef reference(kRsGameMusicText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->SoundPrefs()->music;
    AddCheckboxWithWidth(window, kUiPreferencesMusicEnabledId, 222, 68, 120, checked, caption);
  }
  {
    UiTextRef reference(kRsGameAmbientSoundText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->SoundPrefs()->ambience;
    AddCheckboxWithWidth(window, kUiPreferencesAmbienceEnabledId, 222, 92, 120, checked, caption);
  }
  {
    UiTextRef reference(kRsGameSoundEffectsText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->SoundPrefs()->sound_effects;
    AddCheckboxWithWidth(window, kUiPreferencesEffectsEnabledId, 222, 116, 120, checked, caption);
  }
  {
    UiTextRef reference(kRsGameEnable3DSoundsText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->SoundPrefs()->enable_3d_sound;
    AddCheckboxWithWidth(window, kUiPreferencesThreeDimensionalSoundId, 222, 140, 120, checked, caption);
  }
  i32 music_volume = UiGetPreferences()->SoundPrefs()->music_volume;
  i32 ambience_volume = UiGetPreferences()->SoundPrefs()->ambience_volume;
  i32 effects_volume = UiGetPreferences()->SoundPrefs()->sound_effects_volume;
  AddHorizontalSlider(window, kUiPreferencesMusicVolumeId, 378, 67, 86, 0, 1024, (u32)music_volume, &empty);
  AddHorizontalSlider(window, kUiPreferencesAmbienceVolumeId, 378, 91, 86, 0, 1024, (u32)ambience_volume, &empty);
  AddHorizontalSlider(window, kUiPreferencesEffectsVolumeId, 378, 115, 86, 0, 1024, (u32)effects_volume, &empty);
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_reference(1, kRsOtherControlImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_reference.GetBuffer();
  if (image) {
    WnBitmapControl *bitmap = resources->CreateWinBMP(kUiPreferencesVolumeBitmapId, image, true);
    if (bitmap) {
      if (!window->ChildAdd(bitmap->GetWindow())) {
        bitmap->GetWindow()->Shutdown();
        bitmap->Release();
        return 0;
      }
      bitmap->GetWindow()->GZWinMoveTo(386, 50);
      bitmap->Release();
    }
  }
  {
    UiTextRef reference(kRsGameShowTipsAtStartupText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->TODPrefs()->enabled;
    AddCheckboxWithWidth(window, kUiPreferencesShowTipsId, 18, 364, 222, checked, caption);
  }
  u32 auto_y = 364;
  WnWindow *tip_checkbox = window->GetChildWindowFromID(kUiPreferencesShowTipsId);
  if (tip_checkbox)
    auto_y += (u32)tip_checkbox->GetH();
  {
    UiTextRef reference(kRsGameAutoGoToDisastersText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    u8 checked = UiGetPreferences()->SimPrefs()->auto_goto;
    AddCheckboxWithWidth(window, kUiPreferencesAutoGoToDisastersId, 18, auto_y, 222, checked, caption);
  }
  WnOptionControl *group = nullptr;
  ScRect area = {18, 86, 148, 158};
  CreateOptionGroup(kUiPreferencesTrafficOptionsId, area, &group);
  {
    UiTextRef reference(kRsGameClosest3ZoomsText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesTrafficClosestThreeZoomsId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x202, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesTrafficClosestTwoZoomsId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x203, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesTrafficClosestZoomId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x20a, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesTrafficOffId, caption, 0, nullptr, nullptr);
  }
  i32 traffic = UiGetPreferences()->VisibilityPrefs()->traffic_visibility;
  if (traffic == 1)
    group->SetValue(kUiPreferencesTrafficClosestZoomId);
  else if (traffic == 2)
    group->SetValue(kUiPreferencesTrafficClosestTwoZoomsId);
  else if (traffic == 3)
    group->SetValue(kUiPreferencesTrafficClosestThreeZoomsId);
  else
    group->SetValue(kUiPreferencesTrafficOffId);
  group->GetWindow()->GZWinMoveTo(18, 86);
  window->ChildAdd(group->GetWindow());
  group->Release();
  group = nullptr;
  area = {18, 180, 148, 234};
  CreateOptionGroup(kUiPreferencesSimsOptionsId, area, &group);
  {
    UiTextRef reference(0x208, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesSimsClosestTwoZoomsId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x209, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesSimsClosestZoomId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x20a, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesSimsHiddenId, caption, 0, nullptr, nullptr);
  }
  i32 sims = UiGetPreferences()->VisibilityPrefs()->sims_visibility;
  if (sims == 1)
    group->SetValue(kUiPreferencesSimsClosestZoomId);
  else if (sims == 2)
    group->SetValue(kUiPreferencesSimsClosestTwoZoomsId);
  else
    group->SetValue(kUiPreferencesSimsHiddenId);
  group->GetWindow()->GZWinMoveTo(18, 180);
  window->ChildAdd(group->GetWindow());
  group->Release();
  group = nullptr;
  area = {18, 256, 148, 292};
  CreateOptionGroup(kUiPreferencesAnimationsOptionsId, area, &group);
  {
    UiTextRef reference(kRsGameOnText, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesAnimationsOnId, caption, 0, nullptr, nullptr);
  }
  {
    UiTextRef reference(0x206, kRsGameTextGroup);
    ScString caption = reference.GetText();
    group->AddButton(kUiPreferencesAnimationsOffId, caption, 0, nullptr, nullptr);
  }
  i32 animations = UiGetPreferences()->VisibilityPrefs()->other_animations_visibility;
  group->SetValue(animations == 1 ? kUiPreferencesAnimationsOnId : kUiPreferencesAnimationsOffId);
  group->GetWindow()->GZWinMoveTo(18, 256);
  window->ChildAdd(group->GetWindow());
  group->Release();
  group = nullptr;
  window->SetSize(480, 472);
  WnWindow *height_parent = window->GetParentWin(), *width_parent = window->GetParentWin();
  i32 height = window->GetH(), parent_height = height_parent->GetH();
  i32 y = (i32)((u32)parent_height - height) / 2;
  i32 parent_width = width_parent->GetW(), width = window->GetW();
  window->GZWinMoveTo((i32)((u32)parent_width - width) / 2, y);
  return 1;
}

void UiPreferencesDialog::SelectMusicTitleEffects(WnWindow *window, u32 index, u8 selected) {
  if (!state32 || !song_count)
    return;
  u32 first = first_song;
  if (index < first || index > first + 6)
    return;
  WnLabelControl *label = nullptr;
  if (!window->GetChildAs(
          index - first + kUiPreferencesFirstSongLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label)) ||
      !label)
    return;
  if (selected == 1) {
    u32 white = UiGetGraphics()->ConvertRGBValueToNative16Bit(255, 255, 255);
    label->SetTextColor(white);
    GxGraphics *graphics = UiGetGraphics();
    WnWindow *primary = label->GetWindow();
    u32 blue = graphics->ConvertRGBValueToNative16Bit(0, 0, 255);
    primary->SetFillColor(blue);
    label->SetBackground(true);
  } else {
    u32 ordinary = UiGetGraphics()->ConvertRGBValueToNative16Bit(49, 54, 124);
    label->SetTextColor(ordinary);
    label->SetBackground(false);
  }
  label->Release();
}

// Rebuild captions before reading live song enable state, and restore the selected row after all controls retire.
void UiPreferencesDialog::ScrollMusicEffects(WnWindow *window, u32 first) {
  if (!state32)
    return;
  SelectMusicTitleEffects(window, selected_song, 0);
  first_song = first;
  for (u32 row = 0; row < 7; ++row) {
    WnButton *checkbox = nullptr;
    WnLabelControl *label = nullptr;
    bool acquired =
        window->GetChildAs(kUiPreferencesFirstSongCheckboxId + row, IID_WnButton, reinterpret_cast<void **>(&checkbox));
    if (acquired)
      acquired = window->GetChildAs(
          kUiPreferencesFirstSongLabelId + row, IID_WnLabelControl, reinterpret_cast<void **>(&label));
    if (acquired && checkbox && label) {
      u32 index = row + first_song;
      ScString text;
      if (index < song_count) {
        for (u32 title = 0; title < titles.size; ++title) {
          ScString copy(titles.data[title]->Data());
          text = copy;
          if (title >= index)
            break;
        }
        label->SetCaption(text);
        label->GetWindow()->SetW(212);
        label->GetWindow()->ShowWindow();
        label->SetLabelFlag(kWnLabelForwardMessages, true);
        u32 *ids = track_ids.data;
        ApPreferences *preferences = UiGetPreferences();
        u32 song = ids[index];
        bool enabled = preferences->IsSongEnabled(song);
        checkbox->SetChecked(enabled);
        checkbox->GetWindow()->ShowWindow();
      } else {
        checkbox->GetWindow()->HideWindow();
        label->GetWindow()->HideWindow();
      }
    }
    if (checkbox)
      checkbox->Release();
    if (label)
      label->Release();
  }
  SelectMusicTitleEffects(window, selected_song, 1);
}

void UiPreferencesDialog::GrayMusicEffects(WnWindow *window, u8 disabled) {
  bool enabled = disabled == 0;
  state32 = enabled;
  for (u32 identifier = kUiPreferencesFirstSongLabelId; identifier < kUiPreferencesSongScrollbarId; ++identifier) {
    WnLabelControl *label = nullptr;
    if (window->GetChildAs(identifier, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
      if (disabled == 1) {
        label->SetLabelFlag(kWnLabelUseFontColors, false);
        u32 color = UiGetWindowResources()->GetColor(kWnColorGray);
        label->SetTextColor(color);
        label->SetBackground(false);
        label->GetWindow()->SetFlag(kWnFlagEnabled, false);
      } else {
        label->SetLabelFlag(kWnLabelUseFontColors, true);
        label->GetWindow()->SetFlag(kWnFlagEnabled, true);
      }
      label->Release();
    }
  }
  for (u32 identifier = kUiPreferencesFirstSongCheckboxId; identifier < kUiPreferencesFirstSongLabelId; ++identifier)
    window->GetChildWindowFromID(identifier)->SetFlag(kWnFlagEnabled, enabled);
  window->GetChildWindowFromID(kUiPreferencesPlaySongId)->SetFlag(kWnFlagEnabled, enabled);
  window->GetChildWindowFromID(kUiPreferencesStopSongId)->SetFlag(kWnFlagEnabled, enabled);
  window->GetChildWindowFromID(kUiPreferencesSongScrollbarId)->SetFlag(kWnFlagEnabled, enabled);
}

// Enumerate audio songs into the existing title/ID collections and save each live preference byte by unsigned song key.
// Reset only the count, preserve duplicate-key replacement, and recheck the mutable song output after callbacks.
u8 UiPreferencesDialog::InitMusicList(WnWindow *) {
  ScString title;
  u32 song;
  song_count = 0;
  UiGetBoxX()->Event(kAuEventBeginSongEnumeration, 0, 0, 0, 0);
  do {
    if (UiGetBoxX()->Event(kAuEventNextSong, reinterpret_cast<uptr>(&song),
            reinterpret_cast<uptr>(static_cast<ScStringView *>(&title)), 0, 0) != 0 ||
        !song)
      break;
    *titles.Append() = new ScString(title);
    *track_ids.Append() = song;
    u8 enabled = UiGetPreferences()->IsSongEnabled(song);
    UiSongState *previous = nullptr, *state = song_states;
    while (state && state->song < song) {
      previous = state;
      state = state->next;
    }
    if (!state || state->song != song) {
      UiSongState *inserted = new UiSongState{state, previous, song, 0};
      if (previous)
        previous->next = inserted;
      else
        song_states = inserted;
      if (state)
        state->previous = inserted;
      state = inserted;
    }
    state->enabled = enabled;
    ++song_count;
  } while (song);
  return 1;
}

// Save reached option groups, checkbox bytes and signed slider values in their original independent order.
// Invalid option selections return zero before releasing that group; absent controls leave their settings unchanged.
u8 UiPreferencesDialog::SaveWindowSettingsToPrefs(WnWindow *window) {
  WnOptionControl *group = nullptr;
  if (window->GetChildAs(kUiPreferencesTrafficOptionsId, IID_WnOptionControl, reinterpret_cast<void **>(&group)) &&
      group) {
    u32 option = group->GetValue() - kUiPreferencesTrafficClosestThreeZoomsId;
    if (option > 3)
      return 0;
    i32 visible = (i32)(3 - option);
    UiGetPreferences()->VisibilityPrefs()->traffic_visibility = visible;
    CtCity *city = UiGetCity();
    if (city) {
      CtStartSimLayer *streets = city->StrtSimLayer();
      if (streets)
        streets->SetNumZoomLevelsTrafficVisible(visible);
    }
    group->Release();
  }
  if (window->GetChildAs(kUiPreferencesSimsOptionsId, IID_WnOptionControl, reinterpret_cast<void **>(&group)) &&
      group) {
    u32 option = group->GetValue() - kUiPreferencesSimsClosestTwoZoomsId;
    if (option > 2)
      return 0;
    i32 visible = (i32)(2 - option);
    UiGetPreferences()->VisibilityPrefs()->sims_visibility = visible;
    CtCity *city = UiGetCity();
    if (city) {
      CtStartSimLayer *streets = city->StrtSimLayer();
      if (streets)
        streets->SetNumZoomLevelsPeopleAreVisible(visible);
    }
    group->Release();
  }
  if (window->GetChildAs(kUiPreferencesAnimationsOptionsId, IID_WnOptionControl, reinterpret_cast<void **>(&group)) &&
      group) {
    u32 option = group->GetValue() - kUiPreferencesAnimationsOnId;
    if (option > 1)
      return 0;
    UiGetPreferences()->VisibilityPrefs()->other_animations_visibility = (i32)(1 - option);
    group->Release();
  }
  WnButton *checkbox = nullptr;
  if (window->GetChildAs(kUiPreferencesShowTipsId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) && checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->TODPrefs()->enabled = (u8)checked;
    checkbox->Release();
  }
  if (window->GetChildAs(kUiPreferencesAutoGoToDisastersId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->SimPrefs()->auto_goto = (u8)checked;
    checkbox->Release();
  }
  if (window->GetChildAs(kUiPreferencesMusicEnabledId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->SoundPrefs()->music = (u8)checked;
    checkbox->Release();
  }
  if (window->GetChildAs(kUiPreferencesAmbienceEnabledId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->SoundPrefs()->ambience = (u8)checked;
    checkbox->Release();
  }
  if (window->GetChildAs(kUiPreferencesEffectsEnabledId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->SoundPrefs()->sound_effects = (u8)checked;
    checkbox->Release();
  }
  if (window->GetChildAs(kUiPreferencesThreeDimensionalSoundId, IID_WnButton, reinterpret_cast<void **>(&checkbox)) &&
      checkbox) {
    ApPreferences *preferences = UiGetPreferences();
    bool checked = checkbox->IsChecked();
    preferences->SoundPrefs()->enable_3d_sound = (u8)checked;
    checkbox->Release();
  }
  WnSliderControl *slider = nullptr;
  if (window->GetChildAs(kUiPreferencesMusicVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
      slider) {
    ApPreferences *preferences = UiGetPreferences();
    i32 value = slider->GetValue();
    preferences->SoundPrefs()->music_volume = value;
    slider->Release();
  }
  if (window->GetChildAs(kUiPreferencesAmbienceVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
      slider) {
    ApPreferences *preferences = UiGetPreferences();
    i32 value = slider->GetValue();
    preferences->SoundPrefs()->ambience_volume = value;
    slider->Release();
  }
  if (window->GetChildAs(kUiPreferencesEffectsVolumeId, IID_WnSliderControl, reinterpret_cast<void **>(&slider)) &&
      slider) {
    ApPreferences *preferences = UiGetPreferences();
    i32 value = slider->GetValue();
    preferences->SoundPrefs()->sound_effects_volume = value;
    slider->Release();
  }
  return 1;
}
