#include "audio/audio_freshness.h"

#include <stdlib.h>

#include <cassert>
#include <list>

#include "audio/api.h"
#include "audio/audio_cache.h"
#include "audio/audio_core.h"
#include "audio/audio_music.h"
#include "audio/callback_timer.h"
#include "city/sc_stopwatch.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/thread.h"

// Freshness treats an elapsed interval above one day as a discontinuity.
enum AuFreshnessTime : u32 {
  kAuFreshnessMaximumElapsedMilliseconds = 0x5265c00,
};

// Check the original signed multiplication and division before evaluating a freshness limit.
static i32 AuFreshnessRatio(i32 first, i32 second, i32 divisor) {
  i64 product = static_cast<i64>(first) * second;
  assert(product >= INT32_MIN && product <= INT32_MAX && divisor && !(product == INT32_MIN && divisor == -1));
  return static_cast<i32>(product / divisor);
}

// Initialize the player's selection state, random generator, voice grid and owned directory string.
AuFreshnessPlayer::AuFreshnessPlayer()
    : initialized(0), references(0), scheduler(nullptr), random(kScRandomSeedFromClock), volume(1024), clock(nullptr),
      table(nullptr), cache(nullptr), playing(0), flag57(0), value60(0), x(1), z(1), value72(0), value76(1),
      value80(15), value84(0), value88(1), value92(0), value96(0), value100(0), value104(4), radiusZ(3), radiusX(3),
      value120(0), value124(0), value128(0), value132(0), value136(0), value140(0), voices{}, value16548(100),
      value16552(0), flag16556(0), flag16557(0), timer(nullptr), value16564(100), value16568(0) {
}

bool AuFreshnessPlayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AuFreshnessPlayer, AuFreshnessPlayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AuFreshnessPlayer::AddRef() {
  return ++references;
}

// Delete the complete player when its last reference is released; otherwise decrement its count.
u32 AuFreshnessPlayer::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Attach the freshness table and owners, create its voice grid once and count cells in the current selection.
u8 AuFreshnessPlayer::Init(
    AuFreshnessTable *source, const ScStringView *folder, AuFreshnessScheduler *owner, AuSoundCache *soundCache) {
  AuFreshnessLock.Lock();
  table = source;
  directory = *folder;
  scheduler = owner;
  value120 = -1;
  value124 = -1;
  value128 = -1;
  value132 = -1;
  volume = table->value4;
  z = 1;
  x = 1;
  value100 = table->value12;
  value104 = table->value16;
  radiusZ = table->value32;
  radiusX = table->value28;
  i32 rows = table->value36, columns = table->value40;
  if (!initialized) {
    assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
    for (i32 row = 0; row < rows; ++row)
      for (i32 column = 0; column < columns; ++column)
        voices[row][column] = new AuFreshnessVoice(this);
  }
  value80 = 0;
  for (i32 row = 0; row < rows; ++row)
    for (i32 column = 0; column < columns; ++column)
      if (Contains(row, column))
        ++value80;
  initialized = 1;
  cache = soundCache;
  AuFreshnessLock.Unlock();
  return 1;
}

// Cancel the player fade, stop playback and release its table and all allocated voice cells.
u8 AuFreshnessPlayer::Shutdown() {
  CancelFade();
  AuFreshnessLock.Lock();
  if (initialized) {
    Stop();
    delete table;
    table = nullptr;
    for (i32 row = 0; row < 64; ++row) {
      for (i32 column = 0; column < 64; ++column) {
        delete voices[row][column];
        voices[row][column] = nullptr;
      }
    }
  }
  playing = 0;
  initialized = 0;
  cache = nullptr;
  AuFreshnessLock.Unlock();
  return 1;
}

u8 AuFreshnessPlayer::IsPlaying() {
  return playing;
}

// Start an initialized player, reset its selection and timing state, and register it with the scheduler.
u8 AuFreshnessPlayer::Start() {
  AuFreshnessLock.Lock();
  if (initialized) {
    if (flag16557)
      CancelFade();
    if (!playing) {
      flag57 = 0;
      value60 = table->value44;
      random.Seed(kScRandomSeedFromClock);
      value92 = table->value48;
      if (value92 > 2)
        --value92;
      value84 = value92;
      value88 = value92;
      RefreshSelection();
      clock = new ScStopwatch(ScStopwatch::Milliseconds, 0);
      clock->Start();
      clock->GetElapsedTime();
      SetZ(table->value60);
      SetX(table->value64);
      scheduler->AddPlayer(this);
      playing = 1;
    }
  }
  AuFreshnessLock.Unlock();
  return 1;
}

// Advance elapsed beats, select and update freshness voices, and reset timing after the original daily limit.
u32 AuFreshnessPlayer::Update() {
  static bool statisticsInitialized;
  static u32 statisticsTime;
  i32 selectionLimit = SelectionLimit();
  i32 playbackLimit = PlaybackLimit();
  if (!initialized || !playing || flag16556)
    return static_cast<u32>(playbackLimit);
  AuFreshnessLock.Lock();
  if (!statisticsInitialized) {
    statisticsInitialized = true;
    statisticsTime = clock->GetElapsedTime();
  }
  u32 statisticsNow = clock->GetElapsedTime();
  i64 statisticsDelta = static_cast<i64>(static_cast<i32>(statisticsNow)) - static_cast<i32>(statisticsTime);
  assert(statisticsDelta >= INT32_MIN && statisticsDelta <= INT32_MAX);
  if (statisticsDelta > 10000)
    statisticsTime = clock->GetElapsedTime();
  u32 elapsed = clock->GetElapsedTime();
  u32 deadline = static_cast<u32>(BeatDuration(value88));
  i32 selectedGroup = 0, previousLevel = 0;
  auto isSelectionBeat = [this]() {
    i64 penultimate = static_cast<i64>(table->value48) - 1;
    assert(penultimate >= INT32_MIN);
    if (value84 == penultimate)
      return true;
    i64 earlier = static_cast<i64>(table->value48) - 6;
    assert(earlier >= INT32_MIN);
    return value84 == earlier;
  };
  while (elapsed >= deadline && elapsed <= kAuFreshnessMaximumElapsedMilliseconds) {
    u8 late = elapsed - deadline > 7 || value16552 != 0;
    i32 row, column;
    if (isSelectionBeat() && CountPlaying() >= selectionLimit && !flag57 && table->value8 &&
        PickRandomCell(&row, &column)) {
      assert(row >= 0 && row < 64 && column >= 0 && column < 64);
      AuFreshnessVoice *voice = voices[row][column];
      voice->flag21 = 1;
      selectedGroup = voice->cell->value80;
      if (selectedGroup)
        previousLevel = voice->cell->value52;
    }
    i32 flagged = CountFlagged();
    i32 active = CountActive() - flagged;
    if (isSelectionBeat() && value16548 && active < playbackLimit && !flag57 && !flag16557) {
      bool found = FindBestCell(&row, &column, selectedGroup);
      if (!found && selectedGroup && previousLevel == 19)
        found = FindBestCell(&row, &column, selectedGroup);
      if (!found && selectedGroup && previousLevel == 19)
        found = FindBestCell(&row, &column, selectedGroup);
      if (!found && active < selectionLimit)
        found = FindBestCell(&row, &column, selectedGroup);
      if (found) {
        assert(row >= 0 && row < 64 && column >= 0 && column < 64);
        voices[row][column]->Start(table->cells[row][column]);
        value128 = row;
        value132 = column;
      }
      if (active < playbackLimit) {
        found = FindBestCell(&row, &column, 0);
        if (!found && active < selectionLimit)
          found = FindBestCell(&row, &column, 0);
        if (found) {
          assert(row >= 0 && row < 64 && column >= 0 && column < 64);
          voices[row][column]->Start(table->cells[row][column]);
          value128 = row;
          value132 = column;
        }
      }
    }
    for (i32 currentRow = 0; currentRow < table->value36; ++currentRow) {
      for (i32 currentColumn = 0; currentColumn < table->value40; ++currentColumn) {
        assert(currentRow < 64 && currentColumn < 64);
        voices[currentRow][currentColumn]->Update(late, value84);
      }
    }
    assert(value84 != INT32_MAX && value88 != INT32_MAX);
    if (++value84 > table->value48)
      value84 = 1;
    deadline = static_cast<u32>(BeatDuration(++value88));
    previousLevel = 0;
  }
  if (elapsed >= deadline && elapsed > kAuFreshnessMaximumElapsedMilliseconds) {
    value92 = table->value48;
    value84 = value92;
    value88 = value92;
    clock->Reset();
    clock->GetElapsedTime();
  }
  return static_cast<u32>(AuFreshnessLock.Unlock());
}

// Stop playback through the internal hook while holding the global freshness lock.
u8 AuFreshnessPlayer::Stop() {
  AuFreshnessLock.Lock();
  u8 result = StopInternal();
  AuFreshnessLock.Unlock();
  return result;
}

// Detach a playing player from the scheduler, retire its clock and stop each active voice.
u8 AuFreshnessPlayer::StopInternal() {
  if (!initialized || !playing)
    return 1;
  CancelFade();
  scheduler->RemovePlayer(this);
  if (clock) {
    clock->Stop();
    delete clock;
    clock = nullptr;
  }
  i32 rows = table->value36, columns = table->value40;
  assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
  for (i32 row = 0; row < rows; ++row) {
    for (i32 column = 0; column < columns; ++column) {
      AuFreshnessVoice *voice = voices[row][column];
      if (voice->state)
        voice->Stop();
    }
  }
  playing = 0;
  return 1;
}

u8 AuFreshnessPlayer::SetVolume(i32 requestedVolume) {
  AuFreshnessLock.Lock();
  u8 result = ApplyVolume(requestedVolume);
  AuFreshnessLock.Unlock();
  return result;
}

// Advance a scheduled fade and stop playback when the original fade target and stop flag are reached.
void AuFreshnessPlayer::FadeCallback(uptr context) {
  AuFreshnessPlayer *owner = reinterpret_cast<AuFreshnessPlayer *>(context);
  AuFreshnessLock.Lock();
  if (owner->flag16557) {
    i64 next = static_cast<i64>(owner->volume) + owner->value16564;
    assert(next >= INT32_MIN && next <= INT32_MAX);
    if (next < 0)
      next = 0;
    owner->ApplyVolume(static_cast<i32>(next));
    if (next <= owner->value16568 && owner->timer && owner->flag16572)
      owner->StopInternal();
  }
  AuFreshnessLock.Unlock();
}

// Schedule a downward volume fade while the player is initialized and playing.
u8 AuFreshnessPlayer::FadeVolume(i32 duration, i32 target, u8 stop) {
  AuFreshnessLock.Lock();
  {
    ScStopwatch temporary(ScStopwatch::Milliseconds, 0);
    if (initialized && playing && !flag16557 && volume && target < volume) {
      flag16572 = stop;
      if (!timer) {
        i64 difference = static_cast<i64>(target) - volume;
        assert(difference >= INT32_MIN && difference <= INT32_MAX);
        timer = new AuCallbackTimer;
        timer->SetCallbackFunction(FadeCallback, reinterpret_cast<uptr>(this));
        timer->SetResolution(100);
        value16564 = static_cast<i32>(difference / 20);
        value16568 = target;
        timer->StartPeriodic(static_cast<u32>(duration / 10), nullptr);
        flag16557 = 1;
      }
    }
  }
  AuFreshnessLock.Unlock();
  return 1;
}

// Convert the normalized horizontal coordinate to a clamped table cell and refresh the selection counts.
u8 AuFreshnessPlayer::SetX(i32 value) {
  if (initialized) {
    i32 width = table->value36;
    i64 product = static_cast<i64>(value) * width;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    i64 selected = product / 1000;
    if (table->value8) {
      i64 step = static_cast<i64>(radiusX) * 2 + 1;
      assert(step >= INT32_MIN && step <= INT32_MAX && step && !(selected == INT32_MIN && step == -1));
      i64 offset = step * (selected / step);
      assert(offset >= INT32_MIN && offset <= INT32_MAX && offset + radiusX >= INT32_MIN &&
          offset + radiusX <= INT32_MAX);
      selected = radiusX + offset;
    }
    i64 edge = static_cast<i64>(width) - radiusX;
    assert(edge >= INT32_MIN && edge <= INT32_MAX && !(selected >= edge && edge == INT32_MIN) && width != INT32_MIN);
    if (selected >= edge)
      selected = edge - 1;
    if (selected < radiusX)
      selected = radiusX;
    if (selected > static_cast<i64>(width) - 1)
      selected = static_cast<i64>(width) - 1;
    if (selected < 0)
      selected = 0;
    x = static_cast<i32>(selected);
    RefreshSelection();
  }
  return 1;
}

// Convert the normalized vertical coordinate to a clamped table cell and refresh the selection counts.
u8 AuFreshnessPlayer::SetZ(i32 value) {
  if (initialized) {
    i32 height = table->value40;
    i64 product = static_cast<i64>(value) * height;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    i64 selected = product / 1000;
    if (table->value8) {
      i64 step = static_cast<i64>(radiusZ) * 2 + 1;
      assert(step >= INT32_MIN && step <= INT32_MAX && step && !(selected == INT32_MIN && step == -1));
      i64 offset = step * (selected / step);
      assert(offset >= INT32_MIN && offset <= INT32_MAX && offset + radiusZ >= INT32_MIN &&
          offset + radiusZ <= INT32_MAX);
      selected = radiusZ + offset;
    }
    i64 edge = static_cast<i64>(height) - radiusZ;
    assert(edge >= INT32_MIN && edge <= INT32_MAX && !(selected >= edge && edge == INT32_MIN));
    if (selected >= edge)
      selected = edge - 1;
    if (selected < radiusZ)
      selected = radiusZ;
    if (selected < 0)
      selected = 0;
    if (selected >= height) {
      assert(height != INT32_MIN);
      selected = static_cast<i64>(height) - 1;
    }
    z = static_cast<i32>(selected);
    RefreshSelection();
  }
  return 1;
}

// Store a new volume and refresh every actively playing voice in the loaded grid.
u8 AuFreshnessPlayer::ApplyVolume(i32 requestedVolume) {
  if (initialized) {
    volume = requestedVolume;
    i32 rows = table->value36, columns = table->value40;
    assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
    for (i32 row = 0; row < rows; ++row) {
      for (i32 column = 0; column < columns; ++column) {
        AuFreshnessVoice *voice = voices[row][column];
        if (voice->state == 4)
          voice->UpdateVolume(volume);
      }
    }
  }
  return 1;
}

// Convert the difference from the current beat offset to milliseconds, or return minus one for zero tempo.
i32 AuFreshnessPlayer::BeatDuration(i32 position) {
  if (!value60)
    return -1;
  i64 difference = static_cast<i64>(position) - value92;
  assert(difference >= INT32_MIN && difference <= INT32_MAX);
  i64 product = difference * 60000;
  assert(product >= INT32_MIN && product <= INT32_MAX && !(product == INT32_MIN && value60 == -1));
  return static_cast<i32>(product / value60);
}

// Change the active playback tempo and reset its elapsed timer and next beat position.
u8 AuFreshnessPlayer::SetValue60(i32 value) {
  AuFreshnessLock.Lock();
  if (IsPlaying() && value60 != value) {
    value60 = value;
    clock->Stop();
    clock->Reset();
    i64 next = static_cast<i64>(value84) + value92;
    assert(next >= INT32_MIN && next <= INT32_MAX);
    value88 = static_cast<i32>(next);
    clock->GetElapsedTime();
    BeatDuration(value88);
    BeatDuration(value88);
    clock->GetElapsedTime();
  }
  AuFreshnessLock.Unlock();
  return 1;
}

// Store the requested tuning word only after initialization.
u8 AuFreshnessPlayer::SetValue76(i32 value) {
  if (initialized)
    value76 = value;
  return 1;
}

// Store the requested secondary tuning word only after initialization.
u8 AuFreshnessPlayer::SetValue16548(i32 value) {
  if (initialized)
    value16548 = value;
  return 1;
}

i32 AuFreshnessPlayer::GetX() {
  return x;
}

i32 AuFreshnessPlayer::GetZ() {
  return z;
}

i32 AuFreshnessPlayer::GetValue76() {
  return value76;
}

// Store the raw controller flag only after initialization.
u8 AuFreshnessPlayer::SetFlag57(u8 value) {
  if (initialized)
    flag57 = value;
  return 1;
}

u8 AuFreshnessPlayer::GetDirectory(ScString *out) {
  *out = directory;
  return 1;
}

// Store the two selection radii in the original argument order.
u8 AuFreshnessPlayer::SetRadii(i32 horizontal, i32 vertical) {
  radiusX = horizontal;
  radiusZ = vertical;
  return 1;
}

// Copy the two selection radii to the caller's output pointers.
u8 AuFreshnessPlayer::GetRadii(i32 *horizontal, i32 *vertical) {
  *horizontal = radiusX;
  *vertical = radiusZ;
  return 1;
}

AuFreshnessPlayer::~AuFreshnessPlayer() {
}

// Stop and retire the fade timer when a fade is active, clearing the flag under the shared lock.
void AuFreshnessPlayer::CancelFade() {
  if (flag16557) {
    timer->Stop();
    AuFreshnessLock.Lock();
    flag16557 = 0;
    AuFreshnessLock.Unlock();
    timer->Stop();
    delete timer;
    timer = nullptr;
  }
}

u8 AuFreshnessPlayer::InitResource(const ScStringView *path, AuFreshnessScheduler *owner, AuSoundCache *soundCache) {
  ScString original(*path);
  ScPath parts(original);
  parts.SplitPath();
  ScString folder(*parts.Drive());
  folder.Append(parts.Directory().Data(), parts.Directory().Strlen());
  ScString filename(parts.FileName());
  filename.Append(parts.Extension().Data(), parts.Extension().Strlen());
  return InitFile(&folder, &filename, owner, soundCache);
}

// Load the requested freshness table and initialize the player with the original directory and owners.
u8 AuFreshnessPlayer::InitFile(
    const ScStringView *folder, const ScStringView *filename, AuFreshnessScheduler *owner, AuSoundCache *soundCache) {
  table = new AuFreshnessTable;
  ScString path(*folder);
  path.Append(filename->Data(), filename->Strlen());
  table->Load(path);
  return Init(table, folder, owner, soundCache);
}

u8 AuFreshnessPlayer::StartAtVolume(i32 requestedVolume) {
  SetVolume(requestedVolume);
  return Start();
}

i32 AuFreshnessPlayer::SetValue16552(i32 value) {
  value16552 = value;
  return value;
}

u8 AuFreshnessPlayer::SetFlag16556(u8 value) {
  flag16556 = value;
  return value;
}

i32 AuFreshnessPlayer::SetCoordinates(i32 first, i32 second) {
  value136 = first;
  value140 = second;
  return second;
}

// Test whether a cell lies within both stored selection radii.
bool AuFreshnessPlayer::Contains(i32 first, i32 second) {
  i64 dx = static_cast<i64>(first) - x, dz = static_cast<i64>(second) - z;
  assert(dx >= -INT32_MAX && dx <= INT32_MAX);
  i64 distanceX = dx < 0 ? -dx : dx;
  if (distanceX > radiusX)
    return false;
  assert(dz >= -INT32_MAX && dz <= INT32_MAX);
  return (dz < 0 ? -dz : dz) <= radiusZ;
}

i32 AuFreshnessPlayer::StopCell(i32 row, i32 column) {
  value120 = row;
  value124 = column;
  assert(row >= 0 && row < 64 && column >= 0 && column < 64);
  i32 index = row * 64 + column;
  AuFreshnessVoice *voice = voices[row][column];
  if (voice->state)
    return voice->Stop();
  return index;
}

u8 AuFreshnessPlayer::ToggleCell(i32 row, i32 column) {
  assert(row >= 0 && row < 64 && column >= 0 && column < 64);
  AuFreshnessVoice *voice = voices[row][column];
  if (voice->state)
    StopCell(row, column);
  else
    voice->Start(table->cells[row][column]);
  return 1;
}

// Prefer a playing unmarked voice outside the selection; otherwise sample an eligible voice inside it.
u8 AuFreshnessPlayer::PickRandomCell(i32 *row, i32 *column) {
  u32 word = static_cast<u32>(ScRand());
  i32 rows = table->value36, columns = table->value40;
  assert(rows && columns);
  i32 firstRow = static_cast<i32>((word >> 8) & 255u) % rows;
  i32 firstColumn = static_cast<i32>(word & 255u) % columns;
  *row = firstRow;
  for (i32 pass = 0; pass < 2; ++pass) {
    i32 stopRow = pass ? firstRow : rows;
    if (pass)
      *row = 0;
    while (*row < stopRow) {
      for (i32 columnPass = 0; columnPass < 2; ++columnPass) {
        *column = columnPass ? 0 : firstColumn;
        i32 stopColumn = columnPass ? firstColumn : columns;
        while (*column < stopColumn) {
          assert(*row >= 0 && *row < 64 && *column >= 0 && *column < 64);
          AuFreshnessVoice *voice = voices[*row][*column];
          if (voice->state == 4 && !Contains(*row, *column) && !voice->flag21)
            return 1;
          ++*column;
        }
      }
      ++*row;
    }
  }
  i32 firstX = FirstX(), lastX = LastX(), firstZ = FirstZ(), lastZ = LastZ();
  i64 width = static_cast<i64>(lastX) - firstX + 1, height = static_cast<i64>(lastZ) - firstZ + 1;
  assert(width >= INT32_MIN && width <= INT32_MAX && height >= INT32_MIN && height <= INT32_MAX &&
      width * 4 >= INT32_MIN && width * 4 <= INT32_MAX && width * 4 * height >= 0 &&
      width * 4 * height <= INT32_MAX);
  usize capacity = static_cast<usize>(width * height);
  i32 *candidateRows = new i32[capacity];
  i32 *candidateColumns = new i32[capacity];
  u32 count = 0;
  *row = firstX;
  while (*row <= lastX) {
    *column = firstZ;
    while (*column <= lastZ) {
      assert(*row >= 0 && *row < 64 && *column >= 0 && *column < 64);
      AuFreshnessVoice *voice = voices[*row][*column];
      if (voice->state == 4 && voice->cell->value52 != 20 && !voice->flag21) {
        candidateRows[count] = *row;
        candidateColumns[count] = *column;
        ++count;
      }
      ++*column;
    }
    ++*row;
  }
  if (count) {
    u32 selected = random.RandomUint32Uniform() % count;
    *row = candidateRows[selected];
    *column = candidateColumns[selected];
  }
  delete[] candidateRows;
  delete[] candidateColumns;
  return count != 0;
}

i32 AuFreshnessPlayer::CellProbability(i32 level, i32 count) {
  static constexpr i32 kAuFreshnessLevelProbabilities[] = {0, 600, 1500, 3000, 7500, 12000, 15000, 18000, 21000, 25500,
      32767, 36000, 45000, 58000, 67500, 90000, 120000, 210000, 300000, 40000000, 40000000};
  assert(level >= 0 && level <= 20 && count);
  return kAuFreshnessLevelProbabilities[level] / count;
}

// Score a free cell after rejecting active related voices and the most recently stopped coordinate.
i32 AuFreshnessPlayer::UpdateCell(i32 row, i32 column, i32 group) {
  assert(row >= 0 && row < 64 && column >= 0 && column < 64);
  AuFreshnessCell *cell = table->cells[row][column];
  AuFreshnessVoice *voice = voices[row][column];
  if (!cell || voice->flag22 || voice->state || !Contains(row, column) ||
      (row == value120 && column == value124))
    return 0;
  i32 cellGroup = cell->value80, rows = table->value36, columns = table->value40;
  if (cellGroup && rows > 0) {
    assert(columns <= 0 || (rows <= 64 && columns <= 64));
    for (i32 currentRow = 0; currentRow < rows; ++currentRow) {
      for (i32 currentColumn = 0; currentColumn < columns; ++currentColumn) {
        AuFreshnessVoice *other = voices[currentRow][currentColumn];
        if (other->cell && cellGroup == other->cell->value80 && other->state && !other->flag21)
          return 0;
      }
    }
  }
  for (AuFreshnessLink *link = cell->sentinel->next; link != cell->sentinel; link = link->next) {
    i32 linkedColumn = static_cast<i32>(link->first), linkedRow = static_cast<i32>(link->second);
    if (linkedRow < rows && linkedColumn < columns) {
      assert(linkedRow >= 0 && linkedRow < 64 && linkedColumn >= 0 && linkedColumn < 64);
      AuFreshnessVoice *other = voices[linkedRow][linkedColumn];
      if (other->state && !other->flag21)
        return 0;
    }
  }
  if (cell->value52 > 20)
    cell->value52 = 20;
  if (cell->value52 < 0)
    cell->value52 = 0;
  if (!cell->value52)
    return 0;
  if (cell->value52 == 20)
    return group && cellGroup == group ? 101 : 100;
  if (!value80)
    value80 = 10;
  if (group && cellGroup == group)
    return 2;
  i32 threshold = CellProbability(cell->value52, value80);
  return ScRand() < threshold;
}

// Search the selection from a randomized starting point and return the highest-scoring eligible cell.
bool AuFreshnessPlayer::FindBestCell(i32 *outRow, i32 *outColumn, i32 group) {
  std::list<i32> candidates;
  i32 best = 0;
  u32 word = static_cast<u32>(ScRand());
  i32 firstRow = FirstX(), firstColumn = FirstZ(), lastRow = LastX(), lastColumn = LastZ();
  i64 rowStep = static_cast<i64>(radiusX) * 2 + 1, columnStep = static_cast<i64>(radiusZ) * 2 + 1;
  assert(rowStep >= INT32_MIN && rowStep <= INT32_MAX && rowStep &&
      columnStep >= INT32_MIN && columnStep <= INT32_MAX && columnStep);
  i64 row = static_cast<i64>(x) + static_cast<i32>((word >> 8) & 255u) % rowStep;
  i64 column = static_cast<i64>(z) + static_cast<i32>(word & 255u) % columnStep;
  assert(row >= INT32_MIN && row <= INT32_MAX && column >= INT32_MIN && column <= INT32_MAX &&
      row - radiusX >= INT32_MIN && row - radiusX <= INT32_MAX &&
      column - radiusZ >= INT32_MIN && column - radiusZ <= INT32_MAX);
  row -= radiusX;
  column -= radiusZ;
  if (row < 0)
    row = 0;
  if (column < 0)
    column = 0;
  if (row >= table->value36) {
    assert(table->value36 != INT32_MIN);
    row = table->value36 - 1;
  }
  if (column >= table->value40) {
    assert(table->value40 != INT32_MIN);
    column = table->value40 - 1;
  }
  i32 startRow = static_cast<i32>(row), startColumn = static_cast<i32>(column);
  for (i32 rowPass = 0; rowPass < 2; ++rowPass) {
    i32 beginRow = rowPass ? firstRow : startRow;
    for (i32 currentRow = beginRow; rowPass ? currentRow < startRow : currentRow <= lastRow; ++currentRow) {
      for (i32 columnPass = 0; columnPass < 2; ++columnPass) {
        i32 beginColumn = columnPass ? firstColumn : startColumn;
        for (i32 currentColumn = beginColumn; columnPass ? currentColumn < startColumn : currentColumn <= lastColumn;
             ++currentColumn) {
          Contains(currentRow, currentColumn);
          i32 score = UpdateCell(currentRow, currentColumn, group);
          if (score > best) {
            best = score;
            *outRow = currentRow;
            *outColumn = currentColumn;
          }
        }
      }
    }
  }
  return best != 0;
}

// Count voice cells with a nonzero playback state in the loaded grid.
i32 AuFreshnessPlayer::CountActive() {
  i32 count = 0, rows = table->value36, columns = table->value40;
  assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
  for (i32 row = 0; row < rows; ++row)
    for (i32 column = 0; column < columns; ++column)
      if (voices[row][column]->state)
        ++count;
  return count;
}

// Count voice cells carrying the original pending-selection flag.
i32 AuFreshnessPlayer::CountFlagged() {
  i32 count = 0, rows = table->value36, columns = table->value40;
  assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
  for (i32 row = 0; row < rows; ++row)
    for (i32 column = 0; column < columns; ++column)
      if (voices[row][column]->flag21)
        ++count;
  return count;
}

// Count voice cells whose active state is the original playing state four.
i32 AuFreshnessPlayer::CountPlaying() {
  i32 count = 0, rows = table->value36, columns = table->value40;
  assert(rows <= 0 || columns <= 0 || (rows <= 64 && columns <= 64));
  for (i32 row = 0; row < rows; ++row)
    for (i32 column = 0; column < columns; ++column)
      if (voices[row][column]->state == 4)
        ++count;
  return count;
}

u8 AuFreshnessPlayer::InspectVoices() {
  if (initialized) {
    if (!playing)
      return 0;
    CountActive();
    CountPlaying();
  }
  return 1;
}

// Compute the position-dependent selection limit and cap it at the number of distinct available groups.
i32 AuFreshnessPlayer::SelectionLimit() {
  i32 width = table->value36, height = table->value40;
  assert(width != INT32_MIN && height != INT32_MIN && x != INT32_MAX && z != INT32_MAX);
  i32 heightDivisor = height == 1 ? 1 : height - 1;
  i32 widthDivisor = width == 1 ? 1 : width - 1;
  i32 base = AuFreshnessRatio(table->value12, value16548, 100);
  i32 vertical = AuFreshnessRatio(table->value20, z + 1, heightDivisor);
  i32 horizontal = AuFreshnessRatio(table->value24, x + 1, widthDivisor);
  i64 combined = static_cast<i64>(vertical) + horizontal;
  assert(combined >= INT32_MIN && combined <= INT32_MAX && combined + base >= INT32_MIN &&
      combined + base <= INT32_MAX);
  i32 result = static_cast<i32>(combined + base);
  return result > value108 ? value108 : result;
}

i32 AuFreshnessPlayer::PlaybackLimit() {
  return AuFreshnessRatio(value16548, table->value16, 100);
}

// Clamp the selection's first horizontal coordinate to the lower grid boundary.
i32 AuFreshnessPlayer::FirstX() {
  i64 result = static_cast<i64>(x) - radiusX;
  assert(result >= INT32_MIN && result <= INT32_MAX);
  return result < 0 ? 0 : static_cast<i32>(result);
}

// Clamp the selection's final horizontal coordinate to the loaded table width.
i32 AuFreshnessPlayer::LastX() {
  i64 result = static_cast<i64>(x) + radiusX;
  assert(result >= INT32_MIN && result <= INT32_MAX && table->value36 != INT32_MIN);
  return result >= table->value36 ? table->value36 - 1 : static_cast<i32>(result);
}

// Clamp the selection's first vertical coordinate to the lower grid boundary.
i32 AuFreshnessPlayer::FirstZ() {
  i64 result = static_cast<i64>(z) - radiusZ;
  assert(result >= INT32_MIN && result <= INT32_MAX);
  return result < 0 ? 0 : static_cast<i32>(result);
}

// Clamp the selection's final vertical coordinate to the loaded table height.
i32 AuFreshnessPlayer::LastZ() {
  i64 result = static_cast<i64>(z) + radiusZ;
  assert(result >= INT32_MIN && result <= INT32_MAX && table->value40 != INT32_MIN);
  return result >= table->value40 ? table->value40 - 1 : static_cast<i32>(result);
}

// Count populated cells and distinct nonzero groups within the current bounded selection.
i32 AuFreshnessPlayer::RefreshSelection() {
  u8 seen[32]{};
  value80 = 0;
  value108 = 0;
  i32 first = FirstX(), last = LastX();
  for (i32 row = first; row <= last; ++row) {
    i32 firstColumn = FirstZ(), lastColumn = LastZ();
    for (i32 column = firstColumn; column <= lastColumn; ++column) {
      assert(row >= 0 && row < 64 && column >= 0 && column < 64);
      AuFreshnessCell *cell = table->cells[row][column];
      if (cell) {
        i32 group = cell->value80;
        if (group) {
          assert(group >= 0 && group < 32);
          if (!seen[group]) {
            seen[group] = 1;
            ++value108;
          }
        } else
          ++value108;
        ++value80;
      }
    }
  }
  return LastX();
}

ScLock AuFreshnessLock;

AuFreshnessScheduler::AuFreshnessScheduler()
    : state0(0), state4(0), state8(0), timer(nullptr), clock(nullptr), value20(0), value24(0), value28(0), value32(0) {
}

AuFreshnessScheduler::~AuFreshnessScheduler() {
}

// Stop and destroy the scheduler timer and clock, and remove the borrowed player-list entries.
void AuFreshnessScheduler::Shutdown() {
  state4 = 0;
  if (timer) {
    timer->Stop();
    delete timer;
    timer = nullptr;
  }
  players.clear();
  if (clock) {
    clock->Stop();
    delete clock;
    clock = nullptr;
  }
}

int AuFreshnessScheduler::Pause() {
  AuFreshnessLock.Lock();
  if (++state0 == 1 && timer)
    timer->Stop();
  return AuFreshnessLock.Unlock();
}

// Read the scheduler's second counter under the shared freshness lock.
u32 AuFreshnessScheduler::GetState8() {
  AuFreshnessLock.Lock();
  u32 result = state8;
  AuFreshnessLock.Unlock();
  return result;
}

// Decrement the second counter and restore the timer's resolution and period on the transition to zero.
u32 AuFreshnessScheduler::DecrementState8() {
  AuFreshnessLock.Lock();
  if (state8-- == 1 && timer) {
    timer->SetResolution(100);
    timer->SetPeriod(100);
  }
  u32 result = state8;
  AuFreshnessLock.Unlock();
  return result;
}

void AuFreshnessScheduler::TimerCallback(uptr context) {
  AuFreshnessScheduler *owner = reinterpret_cast<AuFreshnessScheduler *>(context);
  owner->Dispatch();
}

void AuFreshnessScheduler::Dispatch() {
  AuFreshnessLock.Lock();
  for (AuFreshnessPlayer *player : players)
    player->Update();
  AuFreshnessLock.Unlock();
}

int AuFreshnessScheduler::AddPlayer(AuFreshnessPlayer *player) {
  AuFreshnessLock.Lock();
  players.push_back(player);
  if (++state4 == 1) {
    timer = new AuCallbackTimer;
    timer->SetCallbackFunction(TimerCallback, reinterpret_cast<uptr>(this));
    timer->SetResolution(100);
    timer->StartPeriodic(100, nullptr);
    clock = new ScStopwatch(ScStopwatch::Milliseconds, 0);
    clock->Start();
    value28 = clock->GetElapsedTime();
  }
  return AuFreshnessLock.Unlock();
}

// Remove the player's first list entry and retire the scheduler timer and clock after the last player leaves.
int AuFreshnessScheduler::RemovePlayer(AuFreshnessPlayer *player) {
  AuFreshnessLock.Lock();
  auto found = players.begin();
  while (found != players.end() && *found != player)
    ++found;
  assert(found != players.end());
  players.erase(found);
  if (state4-- == 1) {
    timer->Stop();
    delete timer;
    timer = nullptr;
    clock->Stop();
    delete clock;
    clock = nullptr;
  }
  return AuFreshnessLock.Unlock();
}

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "resources/api.h"
#include "resources/file.h"

// Reject numeric input outside the original signed-32 range before scanf would attempt its conversion.
static const char *AuCheckFreshnessNumber(const char *text) {
  char *end;
  [[maybe_unused]] long long value = strtoll(text, &end, 10);
  if (end == text)
    return nullptr;
  assert(value >= INT32_MIN && value <= INT32_MAX);
  return end;
}

// The originally unwritten value52 is zeroed to exclude reads of indeterminate data.
AuFreshnessCell::AuFreshnessCell()
    : flag20(0), value24(1024), value28(512), value32(512), flag36(0), flag37(0), value40(0), value44(0), value48(0),
      value52(0), flag56(0), value60(8), value64(0), flag68(1), value72(0), value76(0), value80(0), flag84(0) {
  sentinel = new AuFreshnessLink;
  sentinel->next = sentinel;
  sentinel->previous = sentinel;
}

// Destroy each cell-list node and its sentinel before the owned station string retires.
AuFreshnessCell::~AuFreshnessCell() {
  AuFreshnessLink *node = sentinel->next;
  while (node != sentinel) {
    AuFreshnessLink *next = node->next;
    delete node;
    node = next;
  }
  delete sentinel;
}

AuFreshnessTable::AuFreshnessTable()
    : value4(1024), value8(0), value12(0), value16(4), value20(0), value24(0), value28(2), value32(2), value36(4),
      value40(4), value44(120), value48(8), value52(1), value56(1), value60(0), value64(0), cells{} {
}

AuFreshnessTable::~AuFreshnessTable() {
  for (u32 row = 0; row < 64; ++row) {
    for (u32 column = 0; column < 64; ++column) {
      delete cells[row][column];
      cells[row][column] = nullptr;
    }
  }
}

// Load the versioned freshness grid through the original file and String operations.
// Zero-initialize the read buffer so a short read cannot expose indeterminate bytes.
bool AuFreshnessTable::Load(ScString filename) {
  ScString contents;
  ScString token;
  RsFile file(filename);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u32 size = file.Size();
  if (!size)
    return false;
  assert(size != UINT32_MAX);
  char *buffer = new char[size + 1]();
  file.Read(buffer, size);
  file.Close();
  buffer[size] = 0;
  {
    ScString temporary(buffer);
    contents = temporary;
  }
  delete[] buffer;
  if (!contents.BeginsWith("version", 7))
    return true;
  token = contents.SplitToken("version");
  token = contents.SplitToken("\n");
  u32 version = ScConvertToUint32(token.Data());
  if (version != 5 && version != 6 && version != 7)
    return true;
  if (version == 7)
    return ParseCells(&contents, &token, version);
  ScString directory(filename);
  RsFile::GetPathDirectory(directory, &directory);
  return ParseCells(&contents, &token, version);
}

// Parse the shared versioned header and grid after the loader has resolved its original path and version.
bool AuFreshnessTable::ParseCells(ScString *contents, ScString *token, u32 version) {
  long first = 0, second = 0;
  long fields[18] = {};
  *token = contents->SplitToken("\n");
  *token = contents->SplitToken("\n");
  const char *number = token->Data();
  for (u32 index = 0; index < 14 && number; ++index)
    number = AuCheckFreshnessNumber(number);
  long values[] = {value4, value8, value12, value16, value36, value40, value44, value48, value32, value28, value52,
      value56, value20, value24};
  sscanf(token->Data(), "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n", &values[0],
      &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9],
      &values[10], &values[11], &values[12], &values[13]);
  value4 = static_cast<i32>(values[0]);
  value8 = static_cast<i32>(values[1]);
  value12 = static_cast<i32>(values[2]);
  value16 = static_cast<i32>(values[3]);
  value36 = static_cast<i32>(values[4]);
  value40 = static_cast<i32>(values[5]);
  value44 = static_cast<i32>(values[6]);
  value48 = static_cast<i32>(values[7]);
  value32 = static_cast<i32>(values[8]);
  value28 = static_cast<i32>(values[9]);
  value52 = static_cast<i32>(values[10]);
  value56 = static_cast<i32>(values[11]);
  value20 = static_cast<i32>(values[12]);
  value24 = static_cast<i32>(values[13]);
  *token = contents->SplitToken("\n");
  if (value36 <= 0 || value40 <= 0)
    return true;
  assert(value36 <= 64 && value40 <= 64);
  for (i32 row = 0; row < value36; ++row) {
    for (i32 column = 0; column < value40; ++column) {
      AuFreshnessCell *cell = new AuFreshnessCell();
      *token = contents->SplitToken("\n");
      char name[260] = {};
      fields[0] = cell->value24;
      fields[2] = cell->value28;
      fields[3] = cell->value32;
      fields[5] = cell->value40;
      fields[6] = cell->value44;
      fields[7] = cell->value48;
      fields[9] = cell->value60;
      fields[11] = cell->value64;
      fields[12] = cell->value52;
      fields[13] = cell->value72;
      fields[14] = cell->value76;
      fields[16] = cell->value80;
      int required = version == 7 ? 18 : 17;
      number = token->Data();
      for (int index = 0; index < required && number; ++index)
        number = AuCheckFreshnessNumber(number);
      if (number) {
        while (isspace(static_cast<u8>(*number)))
          ++number;
        [[maybe_unused]] usize nameLength = 0;
        while (nameLength < sizeof(name) && number[nameLength] && !isspace(static_cast<u8>(number[nameLength])))
          ++nameLength;
        assert(nameLength < sizeof(name));
      }
      if (version == 7)
        sscanf(token->Data(),
            "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%259s",
            &fields[0], &fields[1], &fields[2], &fields[3], &fields[4], &fields[5], &fields[6], &fields[7], &fields[8],
            &fields[9], &fields[10], &fields[11], &fields[12], &fields[13], &fields[14], &fields[15], &fields[16],
            &fields[17], name);
      else
        sscanf(token->Data(),
            "\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%259s",
            &fields[0], &fields[1], &fields[2], &fields[3], &fields[4], &fields[5], &fields[6], &fields[7], &fields[8],
            &fields[9], &fields[10], &fields[11], &fields[12], &fields[13], &fields[14], &fields[15], &fields[16],
            name);
      cell->value24 = static_cast<i32>(fields[0]);
      cell->value28 = static_cast<i32>(fields[2]);
      cell->value32 = static_cast<i32>(fields[3]);
      cell->value40 = static_cast<i32>(fields[5]);
      cell->value44 = static_cast<i32>(fields[6]);
      cell->value48 = static_cast<i32>(fields[7]);
      cell->value60 = static_cast<i32>(fields[9]);
      cell->value64 = static_cast<i32>(fields[11]);
      cell->value52 = static_cast<i32>(fields[12]);
      cell->value72 = static_cast<i32>(fields[13]);
      cell->value76 = static_cast<i32>(fields[14]);
      cell->value80 = static_cast<i32>(fields[16]);
      cell->flag36 = fields[1] != 0;
      cell->flag37 = fields[4] != 0;
      cell->flag56 = fields[8] != 0;
      cell->flag68 = fields[10] != 0;
      cell->flag20 = fields[15] != 0;
      cell->flag84 = version == 7 && fields[17] != 0;
      ScString pair;
      ScString prefix = token->SplitToken("(");
      while (prefix.Strlen()) {
        pair = token->SplitToken(")");
        number = AuCheckFreshnessNumber(pair.Data());
        if (number && *number == ',')
          AuCheckFreshnessNumber(number + 1);
        sscanf(pair.Data(), "%ld,%ld", &first, &second);
        AuFreshnessLink *link = new AuFreshnessLink;
        link->first = static_cast<u32>(first);
        link->second = static_cast<u32>(second);
        link->next = cell->sentinel;
        link->previous = cell->sentinel->previous;
        cell->sentinel->previous->next = link;
        cell->sentinel->previous = link;
        prefix = token->SplitToken("(");
      }
      ScString upper(name);
      cell->name = upper;
      upper.MakeUpper();
      bool isNone;
      {
        ScString none("NONE");
        isNone = upper.Strlen() == none.Strlen() && !memcmp(upper.Data(), none.Data(), upper.Strlen());
      }
      if (isNone) {
        delete cell;
        cells[row][column] = nullptr;
      } else {
        cells[row][column] = cell;
      }
    }
  }
  return true;
}

u8 AuFreshnessVoice::UpdateVolume(i32) {
  if (!sound)
    return 1;
  i64 product = static_cast<i64>(player->volume) * cell->value24;
  assert(product >= INT32_MIN && product <= INT32_MAX);
  i64 scaled = product / 1024;
  product = static_cast<i64>(player->table->value4) * scaled;
  assert(product >= INT32_MIN && product <= INT32_MAX);
  return sound->SetVolume(static_cast<i32>(product / 1024));
}

u32 AuFreshnessVoice::Random() {
  static AuRandom unused(kScRandomSeedFromClock);
  return player->random.RandomUint32Uniform();
}

// Position a 3D sound from its cell, using the original randomized or configured cardinal direction.
void AuFreshnessVoice::UpdatePosition() {
  if (!cell)
    return;
  i64 dx = static_cast<i64>(cell->value28) - 512, dz = static_cast<i64>(cell->value32) - 512;
  assert(dx >= INT32_MIN && dx <= INT32_MAX && dz >= INT32_MIN && dz <= INT32_MAX);
  f64 x = static_cast<f64>(dx) / 512.0, z = static_cast<f64>(dz) / 512.0;
  if (cell->flag37) {
    if (cell->value28 == 512 && cell->value32 == 512) {
      u32 direction = Random() & 3u;
      if (AuForcedQuadrantEnabled)
        direction = AuForcedQuadrant;
      if (direction == 0) {
        x = 0.0;
        z = 1.0;
      } else if (direction == 1) {
        x = 0.0;
        z = -1.0;
      } else if (direction == 2) {
        x = -1.0;
        z = 0.0;
      } else if (direction == 3) {
        x = 1.0;
        z = 0.0;
      }
      if (sound)
        sound->SetPosition(static_cast<f32>(x * 100.0), 0.0f, static_cast<f32>(z * 100.0));
      return;
    }
    if (cell->value28 == 512)
      x = (Random() & 1u) ? 1.0 : -1.0;
    if (cell->value32 == 512)
      z = (Random() & 1u) ? 1.0 : -1.0;
  }
  if (sound)
    sound->SetPosition(static_cast<f32>(x * 100.0), 0.0f, static_cast<f32>(z * 100.0));
}

// Advance the original voice states, including sound creation and the defined high-byte success and reset paths.
void AuFreshnessVoice::Update(u32 flags, i32 beat) {
  if (state == 1) {
    ScString path(player->directory);
    path.Append(cell->name.Data(), cell->name.Strlen());
    if (player->cache) {
      if (!player->cache->HasFile(&path))
        return;
      path = player->cache->GetCachedPath(&path);
    }
    sound = AuGetManagerSoundDevice()->SndCreate3D(&path, static_cast<AuStreamingType>(0), 1);
    if (flags >> 24) {
      sound->SetMinimumDistance(100.0f);
      sound->SetMaximumDistance(100000.0f);
      state = 2;
    } else
      Reset();
    return;
  }
  if (state == 2) {
    if (beat == player->table->value48 && cell->flag20 && !flag20) {
      player->scheduler->GetState8();
      flag20 = 1;
    }
    if (beat != 1 || (cell->flag20 && static_cast<u8>(flags)))
      return;
    state = 4;
    i32 minimum = cell->value72, maximum = cell->value76;
    if ((minimum || maximum) && minimum <= maximum) {
      i64 span = static_cast<i64>(maximum) - minimum + 1;
      assert(span > 0 && span <= INT32_MAX);
      i64 pitch = static_cast<i64>(minimum) + Random() % static_cast<u32>(span);
      assert(pitch >= -12 && pitch <= 12);
      static constexpr f64 ratios[] = {1.0, 1.059463094359, 1.122462048309, 1.189207115003, 1.259921049895,
          1.33483985417, 1.414213562373, 1.498307076877, 1.587401051968, 1.681792830507,
          1.781797436281, 1.887748625363, 2.0};
      f64 frequency = sound->GetFrequency();
      frequency = pitch < 0 ? frequency / ratios[-pitch] : frequency * ratios[pitch];
      sound->SetFrequency(static_cast<i32>(static_cast<i64>(frequency)));
    }
    i64 product = static_cast<i64>(player->volume) * cell->value24;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    i32 selectedVolume = static_cast<i32>(product / 1024);
    if (cell->flag36 && selectedVolume > 3)
      selectedVolume -= Random() % static_cast<u32>(selectedVolume / 3);
    if (cell->value40)
      sound->SetVolume(0);
    else
      UpdateVolume(cell->value24);
    sound->Play(sound->GetVolume(), cell->flag68, static_cast<AuSoundDuplication>(1));
    UpdatePosition();
    if (cell->value40)
      sound->FadeVolume(selectedVolume, 0, 2000, 1);
    if (flag20) {
      player->scheduler->DecrementState8();
      flag20 = 0;
    }
    return;
  }
  if (state == 3) {
    state = 2;
    return;
  }
  if (state == 5) {
    if (!sound->IsPlaying()) {
      Reset();
      state = 0;
    }
    return;
  }
  if (state != 4)
    return;
  if (!cell->flag68) {
    if (!sound->IsPlaying()) {
      Stop();
      state = 0;
    }
    return;
  }
  --value16;
  if (flag21) {
    if (!value16)
      return;
    if (!cell->flag20 || !static_cast<u8>(flags)) {
      Reset();
      return;
    }
  }
  if (value16 == 1) {
    if (cell->flag20 && !flag20) {
      player->scheduler->GetState8();
      flag20 = 1;
    }
    return;
  }
  if (!value16) {
    value16 = static_cast<u32>(cell->value60);
    if (!static_cast<u8>(flags) && flag20) {
      player->scheduler->DecrementState8();
      flag20 = 0;
    }
  }
}

AuFreshnessVoice::AuFreshnessVoice(AuFreshnessPlayer *owner)
    : player(owner), sound(nullptr), state(0), cell(nullptr), flag20(0), flag21(0), flag22(0) {
}

// Reject an occupied or disabled controller and record the selected installed-resource cell.
bool AuFreshnessVoice::Start(AuFreshnessCell *selected) {
  if (flag22 || state)
    return false;
  flag21 = 0;
  cell = selected;
  if (selected->flag68)
    value16 = static_cast<u32>(selected->value60);
  if (player->cache) {
    ScString path(player->directory);
    path.Append(selected->name.Data(), selected->name.Strlen());
    player->cache->AddFile(&path);
  }
  state = 1;
  return true;
}

// Release the scheduler hold, reset pending voices or begin the configured fade-out for an active sound.
bool AuFreshnessVoice::Stop() {
  if (flag20) {
    player->scheduler->DecrementState8();
    flag20 = 0;
  }
  if (state == 1 || state == 2)
    return Reset();
  if (state == 4) {
    if (!cell->value44)
      return Reset();
    state = 5;
    u32 duration = static_cast<u32>(cell->value44);
    AuSound *current = sound;
    i32 volume = current->GetVolume();
    // The built-in device creates AuSpatialSample; its GetVolume thunk only reads the inherited volume field.
    // A concurrent unsynchronized replacement is a data race, so defined production execution retains this sound.
    assert(sound == current);
    sound->FadeVolume(volume, 0, duration, 1);
  }
  return true;
}

// Stop and release the current sound, release its cached path, then reset the voice.
bool AuFreshnessVoice::Reset() {
  if (flag20) {
    player->scheduler->DecrementState8();
    flag20 = 0;
  }
  if (sound) {
    sound->Stop();
    assert(sound);
    sound->Unload();
    assert(sound);
    sound->Release();
    sound = nullptr;
  }
  if (player->cache) {
    ScString path(player->directory);
    path.Append(cell->name.Data(), cell->name.Strlen());
    player->cache->RemoveFile(&path);
  }
  flag21 = 0;
  cell = nullptr;
  state = 0;
  return true;
}
