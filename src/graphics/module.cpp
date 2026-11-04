#define WIN32_LEAN_AND_MEAN
#include "graphics/module.h"


#include <cstring>
#include <SDL3/SDL.h>

#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_general_buffer.h"
#include "window/api.h"

// Native interpolation retains the upper half of the rounded red and green words alongside each channel mask.
enum GxInterpolatedColorWord : u32 {
  kGxInterpolatedChannelUpperWordMask = 0xffff0000,
};

// The original graphics capability word uses DDCAPS_ZBLTS to gate depth-based color effects.
enum GxDepthBlitSupport : u32 {
  kGxDepthBlitCapability = 0x100000,
};



u8 GxDeviceInitialized = 0, GxNoHardware = 0, GxDisableColorEffects = 1, GxNoSyslock = 0, GxForceSoftware = 0;
GxColorEffect GxApplyColor = nullptr;
void *GxManagerInstance = nullptr;

// Borrow the module's actual QI output slot without moving its publication past the original retain/release callbacks.
GxDeviceManager *GxGetDeviceManager() {
  return static_cast<GxDeviceManager *>(GxManagerInstance);
}

// Recover the low DWORD of the source's rounded IEEE double without pointer-alias violations.
static u32 GxRoundedChannel(f64 value) {
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return (u32)bits;
}

// Interpolate masked native channels with the source rounding bias and preserve its ordinary full-DWORD return.
// Copy the WORD through bytes so the original x86 operation also accepts unaligned physical planes.
static u32 GxInterpolateColor(u16 *destination, u16 source, f32 alpha, u32 red, u32 green, u32 blue) {
  f64 inverse = 1.0 - (f64)alpha, bias = kScDoubleIntegerBias;
  u16 stored;
  memcpy(&stored, destination, sizeof(stored));
  u32 old = stored;
  f64 r = (f64)(old & red) * inverse + (f64)(source & red) * (f64)alpha + bias;
  f64 g = (f64)(old & green) * inverse + (f64)(source & green) * (f64)alpha + bias;
  f64 b = (f64)(old & blue) * inverse + (f64)(source & blue) * (f64)alpha + bias;
  u32 result = (GxRoundedChannel(b) & blue) | (GxRoundedChannel(g) & (kGxInterpolatedChannelUpperWordMask | green)) |
      (GxRoundedChannel(r) & (kGxInterpolatedChannelUpperWordMask | red));
  stored = (u16)result;
  memcpy(destination, &stored, sizeof(stored));
  return result;
}

u32 GxColor555(u16 *destination, u16 source, f32 alpha) {
  return GxInterpolateColor(destination, source, alpha, 0x7c00, 0x3e0, 31);
}

u32 GxColor655(u16 *destination, u16 source, f32 alpha) {
  return GxInterpolateColor(destination, source, alpha, 0xfc00, 0x3e0, 31);
}

u32 GxColor565(u16 *destination, u16 source, f32 alpha) {
  return GxInterpolateColor(destination, source, alpha, 0xf800, 0x7e0, 31);
}

u32 GxColor556(u16 *destination, u16 source, f32 alpha) {
  return GxInterpolateColor(destination, source, alpha, 0xf800, 0x7c0, 63);
}

// Execute the observed effects of GxGeneralBuffer::StaticInit with the graphics service, manager and probe buffer. A
// null argument performs a fresh framework lookup and releases its temporary service reference before querying the
// manager.
void GxInitializeDeviceGlobals(GxGraphics *graphics) {
  GxColorType selected(GxColorType::Color::RGB16);
  if (!graphics) {
    void *value = nullptr;
    GxFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    graphics = static_cast<GxGraphics *>(value);
    if (!graphics)
      return;
    graphics->Release();
  }
  if (!graphics->QueryInterface(IID_GxDeviceManager, &GxManagerInstance))
    return;
  static_cast<GxDeviceManager *>(GxManagerInstance)->Release();
  GxDeviceInitialized = 1;
  GxNoHardware = graphics->IsForceSystemRAMSurfaces();
  GxGeneralBuffer *probe = new GxGeneralBuffer;
  probe->Init(16, 16, GxColorType(static_cast<GxColorType::Color>(4)), nullptr, GxDepthType(GxDepthType::Depth::None),
      GxVramRequest::VideoMemoryLowPriority, false);
  probe->GetColorType(&selected);
  switch (static_cast<i32>(selected.color)) {
  case 6:
    GxApplyColor = GxColor655;
    break;
  case 7:
    GxApplyColor = GxColor565;
    break;
  case 8:
    GxApplyColor = GxColor556;
    break;
  default:
    GxApplyColor = GxColor555;
    break;
  }
  delete probe;
  GxDisableColorEffects =
      !(static_cast<GxDeviceManager *>(GxManagerInstance)->GetCapabilities()->capabilities & kGxDepthBlitCapability);
  u32 version;
  version = 0;
  GxDeviceManager *manager = static_cast<GxDeviceManager *>(GxManagerInstance);
  GxNoSyslock = version < kScLegacyWindowsVersionFlag;
  GxForceSoftware = manager->IsSoftware();
}

GxDirectorClasses::GxDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
GxDirectorClasses::~GxDirectorClasses() {
  while (head.next != &head) {
    GxClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void GxDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  GxClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  GxClassRegistration *entry = new GxClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

GxDllDirector::GxDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

GxDllDirector::~GxDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool GxDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GxDllDirector::AddRef() {
  return ++references;
}

u32 GxDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 GxDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 GxDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *GxDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *GxDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool GxDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its own children, then append the child without a retain.
void GxDllDirector::AddDirector(GxDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void GxDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (GxClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool GxDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (GxClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
    if (entry->clsid != clsid)
      continue;
    ScUnknown *object = entry->create();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let live children veto unloading before checking this director's own reference word.
// The installed PC body differs from the available SDK's reference-count shortcut.
bool GxDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool GxDllDirector::OnUnload() {
  return true;
}

bool GxDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool GxDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool GxDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  GxFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool GxDllDirector::PreFrameWorkInit() {
  return true;
}

bool GxDllDirector::PreAppInit() {
  return true;
}

bool GxDllDirector::PostAppInit() {
  return true;
}

bool GxDllDirector::PreAppShutdown() {
  return true;
}

bool GxDllDirector::PostAppShutdown() {
  return true;
}

bool GxDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool GxDllDirector::AbortiveQuit() {
  return true;
}

bool GxDllDirector::OnInstall() {
  return true;
}

GxDirector::GxDirector() {
  AddDirector(GxGetClassDirector());
}

u32 GxDirector::GetDirectorID() const {
  return kGxDirectorId;
}

// Create and publish the three graphics services in original order, reloading the COM and framework between calls.
bool GxDirector::OnStart(ScCom *) {
  const u32 ids[3] = {kGxDisplayClassId, kGxEffectsServiceClassId, kGxColorServiceClassId};
  void *value = nullptr;
  for (u32 index = 0; index < 3; ++index) {
    GZCOM()->GetClassObject(ids[index], IID_ScSystemService, &value);
    if (value) {
      GxFrameWork()->AddSystemService(static_cast<ScSystemService *>(value));
      static_cast<ScSystemService *>(value)->Release();
      value = nullptr;
    }
  }
  return true;
}

GxClassDirector::GxClassDirector() {
  AddDirector(GxGetDisplayDirector());
  classes.Insert(kGxBufferClassId, GxCreateBufferObject, 0);
  classes.Insert(kGxImageBuilderClassId, GxCreateImageObject, 0);
  classes.Insert(kGxEffectsServiceClassId, GxCreateEffectsObject, 0);
  classes.Insert(kGxColorServiceClassId, GxCreateColorService, 0);
}

u32 GxClassDirector::GetDirectorID() const {
  return kGxClassDirectorId;
}

GxDisplayDirector::GxDisplayDirector() {
  classes.Insert(kGxDisplayClassId, GxCreateDisplayObject, 0);
}

u32 GxDisplayDirector::GetDirectorID() const {
  return kGxDisplayDirectorId;
}

// Construct the static module graph on first use and retain its address until normal process teardown.
GxDirector *GxGetDirector() {
  static GxDirector director;
  return &director;
}

// Construct the static class director before publishing its singleton address.
GxClassDirector *GxGetClassDirector() {
  static GxClassDirector director;
  return &director;
}

// Construct the display director's static registration catalog once.
GxDisplayDirector *GxGetDisplayDirector() {
  static GxDisplayDirector director;
  return &director;
}

// Borrow the actual framework from the lazily constructed graphics module director.
ScFramework *GxFrameWork() {
  return GxGetDirector()->FrameWork();
}

// Return the module director through the original loader export without retaining it.
// Its lazy accessor constructs the registered graphics and display directors on first use.
SC3_C_LINKAGE GxDirector *GxDllGetGZCOMDirector() {
  return GxGetDirector();
}

// Return a new unretained general-buffer primary identity for the director's subsequent QueryInterface.
ScUnknown *GxCreateBufferObject() {
  return static_cast<GxBuffer *>(new GxGeneralBuffer);
}

// Return the real unretained effects-service primary identity; QI supplies ownership after construction.
ScUnknown *GxCreateEffectsObject() {
  return static_cast<ScSystemService *>(new GxEffectsService);
}

// Return the actual unretained color-service primary identity for subsequent director QueryInterface.
ScUnknown *GxCreateColorService() {
  return static_cast<ScSystemService *>(new GxColorService);
}

// Return the actual unretained image-builder secondary identity for director QueryInterface.
ScUnknown *GxCreateImageObject() {
  return static_cast<GxImageBuilder *>(new GxImage);
}

static WnWindowManager *GxWindows;
static ScMessageServer *GxMessages;

// Resolve the actual graphics service once, publish its borrowed cache before releasing the temporary queried reference.
GxGraphics *GxGetGraphics() {
  if (!GxGraphicsCurrent) {
    void *value = nullptr;
    GxFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    GxGraphicsCurrent = static_cast<GxGraphics *>(value);
    if (value)
      static_cast<GxGraphics *>(value)->Release();
  }
  return GxGraphicsCurrent;
}

// Apply the same actual window-manager service cache ordering; null lookup results remain retryable.
WnWindowManager *GxGetWindowManager() {
  if (!GxWindows) {
    void *value = nullptr;
    GxFrameWork()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &value);
    GxWindows = static_cast<WnWindowManager *>(value);
    if (value)
      static_cast<WnWindowManager *>(value)->Release();
  }
  return GxWindows;
}

// Borrow the module's actual message server after publishing its cache and releasing the temporary service reference.
ScMessageServer *GxGetMessageServer() {
  if (!GxMessages) {
    void *value = nullptr;
    GxFrameWork()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &value);
    GxMessages = static_cast<ScMessageServer *>(value);
    if (value)
      static_cast<ScMessageServer *>(value)->Release();
  }
  return GxMessages;
}

GxModuleObject::GxModuleObject() : references(0) {
}

bool GxModuleObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxModuleObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GxModuleObject::AddRef() {
  GxGetDirector()->AddRef();
  return ++references;
}

// Capture final-reference selection before removing the module reference, then delete or decrement the live object.
u32 GxModuleObject::Release() {
  if (references == 1) {
    GxGetDirector()->RemoveRef();
    delete this;
    return 0;
  }
  GxGetDirector()->RemoveRef();
  return --references;
}

// Remove the module reference before decrementing a positive local count, preserving the live object even at zero.
// A zero local count skips both decrements, unlike Release's wrapping zero-count path.
u32 GxModuleObject::RemoveRef() {
  if (references) {
    GxGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 GxModuleObject::RefCount() {
  return references;
}

GxSystemServiceBase::GxSystemServiceBase(u32 value, i32 order) : identifier(value), priority(order), running(false) {
}

// Keep system-service and basic identities distinct while sharing the same dynamic retain implementation.
bool GxSystemServiceBase::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScSystemService)
    return GxModuleObject::QueryInterface(iid, out);
  *out = static_cast<ScSystemService *>(this);
  static_cast<ScSystemService *>(this)->AddRef();
  return true;
}

u32 GxSystemServiceBase::AddRef() {
  return GxModuleObject::AddRef();
}

u32 GxSystemServiceBase::Release() {
  return GxModuleObject::Release();
}

u32 GxSystemServiceBase::GetServiceID() const {
  return identifier;
}

void GxSystemServiceBase::SetServiceID(u32 value) {
  identifier = value;
}

i32 GxSystemServiceBase::GetServicePriority() const {
  return priority;
}

bool GxSystemServiceBase::IsServiceRunning() const {
  return running;
}

void GxSystemServiceBase::SetServiceRunning(bool value) {
  running = value;
}

bool GxSystemServiceBase::Init() {
  return true;
}

bool GxSystemServiceBase::Shutdown() {
  return true;
}

bool GxSystemServiceBase::OnTick(u32) {
  return true;
}

bool GxSystemServiceBase::OnIdle(u32) {
  return true;
}

bool GxSystemServiceBase::OnInstall() {
  return true;
}

GxTimer::GxTimer(GxTimeUnit value, u32 sample_limit) : unit(value) {
  SetSampleBufferSize(sample_limit);
}

GxTimer::~GxTimer() {
  delete[] samples;
}

void GxTimer::SetResolution(GxTimeUnit value) {
  if (unit != value)
    unit = value;
}

bool GxTimer::IsTimerRunning() const {
  return first_tick != 0;
}

// Reset sample accounting while retaining capacity, logical sample size and both timestamps.
// The source reserve copies live records before replacing storage, then zeroes only the new requested window.
void GxTimer::SetSampleBufferSize(u32 sample_limit) {
  sample_index = 0;
  sample_count = 0;
  buffer_size = sample_limit;
  u32 size = samples ? (u32)(samples_end - samples) : 0;
  u32 capacity = samples ? (u32)(samples_capacity - samples) : 0;
  if (capacity < sample_limit) {
    GxTimerSample *replacement = new GxTimerSample[sample_limit];
    for (u32 index = 0; index < size; ++index)
      replacement[index] = samples[index];
    delete[] samples;
    samples = replacement;
    samples_end = replacement + size;
    samples_capacity = replacement + sample_limit;
  }
  for (u32 index = 0; index < buffer_size; ++index)
    samples[index] = {};
}

// Read the performance counter into the actual caller output, or write both zero words when the API fails.
// The original const method returns void and does not expose the Win32 Boolean as a game result.
void GxTimer::GetCurrentInternal(i64 *out) const {
  *out = (i64)SDL_GetPerformanceCounter();
}

void GxTimer::GetElapsedInternal(i64 *out) const {
  GetCurrentInternal(out);
  *out = (i64)((u64)*out - (u64)first_tick);
}

// Clear timestamps, sample accounting and the existing sample window without changing resolution or capacity.
// The original retains vector size, capacity and limit and zeroes exactly the existing requested sample range.
void GxTimer::Reset() {
  first_tick = 0;
  second_tick = 0;
  sample_index = 0;
  sample_count = 0;
  for (u32 index = 0; index < buffer_size; ++index)
    samples[index] = {};
}

u32 GxClockOverhead = 0;

// Start only when the complete first timestamp is zero.
// A fresh start clears the accumulated timestamp before reading the clock; running timers retain both values.
void GxTimer::Start() {
  if (!IsTimerRunning()) {
    second_tick = 0;
    GetCurrentInternal(&first_tick);
  }
}

void GxTimer::Restart() {
  Reset();
  GetCurrentInternal(&first_tick);
}

// Add the current delta to the live elapsed accumulator only while the timer is running.
// The first timestamp remains unchanged, so repeated calls accumulate each delta from the same original start.
void GxTimer::Stop() {
  if (IsTimerRunning()) {
    i64 elapsed;
    GetElapsedInternal(&elapsed);
    second_tick = (i64)((u64)second_tick + (u64)elapsed);
  }
}

// Yield once and perform four original start/accumulate/read rounds, replacing the retained measurement each time.
// Compare only the final two deltas as signed64 values and return their selected low DWORD as unsigned overhead.
u32 GxTimerComputeBase() {
  GxTimer timer(GxTimeUnit::Cycles, 0);
  SDL_Delay(0);
  timer.Start();
  timer.Stop();
  i64 elapsed, last;
  timer.GetElapsedInternal(&elapsed);
  for (u32 pass = 0; pass < 2; ++pass) {
    timer.Restart();
    timer.Stop();
    timer.GetElapsedInternal(&elapsed);
  }
  timer.Restart();
  timer.Stop();
  timer.GetElapsedInternal(&last);
  if (last < elapsed)
    elapsed = last;
  GxClockOverhead = (u32)elapsed;
  return GxClockOverhead;
}

f64 GxClockUnits[10] = {};

static u32 GxClockLength(u64 value) {
  u32 length = 0;
  for (; value; value >>= 1)
    ++length;
  return length;
}

// Encode a rounded normal clock significand, carrying a rounded overflow into the binary exponent.
// Clock conversion magnitudes stay within the normal binary64 range. Exponent1024 represents a zero divisor's infinity.
static f64 GxClockValue(u64 significand, i32 exponent, u64 sign) {
  if (significand == (1ull << 53)) {
    significand >>= 1;
    ++exponent;
  }
  u64 bits = sign | ((u64)(exponent + 1023) << 52) | (significand & 0xfffffffffffffull);
  f64 value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

// Divide a positive clock unit by the exact signed64 frequency before rounding once to binary64.
// The normalized quotient has 53 bits. Its remainder resolves ties to even without first rounding the frequency.
static f64 GxClockRatio(f64 unit, i64 frequency) {
  if (!frequency)
    return GxClockValue(1ull << 52, 1024, 0);
  u64 bits;
  memcpy(&bits, &unit, sizeof(bits));
  u64 numerator = (bits & 0xfffffffffffffull) | (1ull << 52);
  u64 denominator = frequency < 0 ? 0ull - (u64)frequency : (u64)frequency;
  u64 sign = frequency < 0 ? 1ull << 63 : 0;
  i32 exponent = (i32)GxClockLength(numerator) - (i32)GxClockLength(denominator);
  if (exponent >= 0) {
    if (numerator < (denominator << exponent))
      --exponent;
  } else if ((numerator << -exponent) < denominator)
    --exponent;
  u32 shift = (u32)(52 - exponent);
  u64 low = numerator << shift, high = shift ? numerator >> (64 - shift) : 0;
  u64 quotient = 0, remainder = 0;
  for (i32 index = 127; index >= 0; --index) {
    u64 bit = (index >= 64 ? high >> (index - 64) : low >> index) & 1;
    remainder = 2 * remainder + bit;
    quotient <<= 1;
    if (remainder >= denominator) {
      remainder -= denominator;
      ++quotient;
    }
  }
  u64 twice = 2 * remainder;
  if (twice > denominator || (twice == denominator && (quotient & 1)))
    ++quotient;
  i32 unit_exponent = (i32)((bits >> 52) & 2047) - 1023 - 52;
  return GxClockValue(quotient, exponent + unit_exponent, sign);
}

// Multiply an exact signed64 frequency by a positive clock scale and round its full integer product once.
// Two 32-bit limbs retain the source FILD operand's precision even when the frequency is not representable as a double.
static f64 GxClockScale(i64 frequency, f64 scale) {
  if (!frequency)
    return 0.0;
  u64 bits;
  memcpy(&bits, &scale, sizeof(bits));
  u64 magnitude = frequency < 0 ? 0ull - (u64)frequency : (u64)frequency;
  u64 mantissa = (bits & 0xfffffffffffffull) | (1ull << 52), sign = frequency < 0 ? 1ull << 63 : 0;
  u64 first = (u64)(u32)magnitude * (u32)mantissa, second = (magnitude >> 32) * (u32)mantissa;
  u64 third = (u64)(u32)magnitude * (mantissa >> 32), fourth = (magnitude >> 32) * (mantissa >> 32);
  u64 middle = (first >> 32) + (u32)second + (u32)third;
  u64 low = (middle << 32) | (u32)first;
  u64 high = fourth + (second >> 32) + (third >> 32) + (middle >> 32);
  u32 length = high ? 64 + GxClockLength(high) : GxClockLength(low), shift = length - 53;
  u64 significand = shift ? (low >> shift) | (high << (64 - shift)) : low;
  if (shift) {
    u64 discarded = low & ((1ull << shift) - 1), half = 1ull << (shift - 1);
    if (discarded > half || (discarded == half && (significand & 1)))
      ++significand;
  }
  i32 exponent = (i32)((bits >> 52) & 2047) - 1023 - 52;
  return GxClockValue(significand, exponent + (i32)shift + 52, sign);
}

// Initialize the shared frequency conversions and calibrate timer overhead at module startup.
// A failed query leaves all prior conversion factors and overhead untouched and performs no calibration calls.
GxTimerSetup::GxTimerSetup() {
  i64 ticks = (i64)SDL_GetPerformanceFrequency();
  if (ticks) {
    GxClockUnits[0] = GxClockRatio(1000000000.0, ticks);
    GxClockUnits[1] = GxClockRatio(1000000.0, ticks);
    GxClockUnits[2] = GxClockRatio(1000.0, ticks);
    GxClockUnits[3] = GxClockRatio(1.0, ticks);
    GxClockUnits[4] = GxClockRatio(0.016666666666666666, ticks);
    GxClockUnits[5] = GxClockScale(ticks, 0.000000001);
    GxClockUnits[6] = GxClockScale(ticks, 0.000001);
    GxClockUnits[7] = GxClockScale(ticks, 0.001);
    GxClockUnits[8] = GxClockScale(ticks, 1.0);
    GxClockUnits[9] = GxClockScale(ticks, 60.0);
    GxClockOverhead = GxTimerComputeBase();
  }
}

static GxTimerSetup GxTimerStartup;
