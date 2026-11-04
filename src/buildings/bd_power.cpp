#include "buildings/bd_power.h"

#include <math.h>
#include <string.h>

#include <map>

#include "advisors/ad_module.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "buildings/bd_utility_graph.h"
#include "buildings/bd_water.h"
#include "city/api.h"
#include "city/ct_scenario.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "geometry/utilities.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

// Buying power deals retain their quantity sentinel; the usage history retains its display color.
enum CtPowerPresentation : u32 {
  kCtPowerBuyingDealQuantitySentinel = 0x3b9aca00,
  kCtPowerUsageHistoryColor = 0xff6800,
};

// Both power-building occupant variants publish the same fixed power-building geometry word.
enum BdPowerGeometry : u32 {
  kBdPowerBuildingGeometry = 0x400104,
};

// Power-building descriptors store aging, retirement and waste-consumption fields under these keys.
enum BdPowerPersistenceKey : u32 {
  kBdPowerPlantOverworkKey = 0x2351faf8,
  kBdPowerPlantMaximumAgeKey = 0x2351faf9,
  kBdPowerPlantDeclineAgeKey = 0x2351fafa,
  kBdPowerPlantRetiredKey = 0x2351fafb,
  kBdWastePlantDailyGarbageCapacityKey = 0xa352179a,
  kBdWastePlantGarbageConsumedKey = 0xa352179b,
  kBdWastePlantPreviousMonthGarbageConsumedKey = 0xa352179c,
};

// Allocate one packed-word array per X column and fill every allocated word with the supplied Boolean value.
// The final partial word is initialized in full, matching the installed storage and serialization behavior.
CtPowerBitGrid::CtPowerBitGrid(u32 widthValue, u32 heightValue, const bool *initial)
    : width(widthValue), height(heightValue), wordsPerColumn((heightValue >> 5) + ((heightValue & 31) != 0)),
      words(new u32 *[widthValue]) {
  for (u32 x = 0; x < width; ++x)
    words[x] = new u32[wordsPerColumn];
  Fill(initial);
}

// Release each original column allocation before releasing the pointer array.
// has no established source return contract.
CtPowerBitGrid::~CtPowerBitGrid() {
  for (u32 x = 0; x < width; ++x)
    delete[] words[x];
  delete[] words;
}

// Fill complete packed words with all ones or all zeroes, including bits beyond the last actual cell.
void CtPowerBitGrid::Fill(const bool *value) {
  u32 fill = *value ? kScFilledBitGridWord : 0;
  for (u32 x = 0; x < width; ++x)
    for (u32 word = 0; word < wordsPerColumn; ++word)
      words[x][word] = fill;
}

// Set or clear the selected packed bit without disturbing other bits in that word.
void CtPowerBitGrid::Set(u32 x, u32 z, const bool *value) {
  u32 mask = 1u << (z & 31);
  if (*value)
    words[x][z >> 5] |= mask;
  else
    words[x][z >> 5] &= ~mask;
}

// Read every column in descending X order, retaining failure while still attempting all remaining columns.
// The word count is captured from the live height before any stream callback.
u8 CtPowerBitGrid::Read(RsStream *record) {
  u32 count = (height >> 5) + ((height & 31) != 0);
  u8 result = 1;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!record->ReadU32Array(words[x], count))
      result = 0;
  return result;
}

// Write every column in descending X order, retaining failure while still attempting all remaining columns.
// The word count is captured from the live height before any stream callback.
u8 CtPowerBitGrid::Write(RsStream *record) const {
  u32 count = (height >> 5) + ((height & 31) != 0);
  u8 result = 1;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!record->WriteU32Array(words[x], count))
      result = 0;
  return result;
}

CtPowerCellMap::CtPowerCellMap() : references(0), initialized(false), grid(nullptr) {
}

CtPowerCellMap::~CtPowerCellMap() {
  if (initialized)
    CtPowerCellMap::Shutdown();
}

// Publish the primary Boolean map for Unknown, cell-map-base and Boolean-cell-map identities. Unsupported identities
// clear the output; successful queries retain virtually.
bool CtPowerCellMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtPowerCellMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, CtPowerCellMap);
  HANDLE_QUERY_INTERFACE(IID_CtPowerCellMap, CtPowerCellMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the map reference count before acquiring the original module-director hold.
u32 CtPowerCellMap::AddRef() {
  ++references;
  ScGetSimutilDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a nonzero reference count, deleting the map at zero.
u32 CtPowerCellMap::Release() {
  if (references) {
    ScGetSimutilDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

u32 CtPowerCellMap::CellCountX() const {
  return grid->width;
}

u32 CtPowerCellMap::CellCountZ() const {
  return grid->height;
}

void CtPowerCellMap::GetValueSint32(u32 x, u32 z, i32 *out) const {
  *out = (grid->words[x][z >> 5] & (1u << (z & 31))) != 0;
}

bool CtPowerCellMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool CtPowerCellMap::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return InBounds(x0, z0) && InBounds(x1, z1);
}

bool CtPowerCellMap::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return x0 <= x1 && z0 <= z1;
}

bool CtPowerCellMap::SerialRead(RsStream *record) {
  return grid->Read(record) != 0;
}

bool CtPowerCellMap::SerialWrite(RsStream *record) const {
  return grid->Write(record) != 0;
}

// Replace an initialized grid, reading the city's Z dimension before X as in the installed constructor call.
// The original sets the initialized flag only after constructing and filling the new grid.
bool CtPowerCellMap::Init(CtCity *city, const bool *initial) {
  if (initialized)
    Shutdown();
  u32 height = city->CellCountZ(), width = city->CellCountX();
  grid = new CtPowerBitGrid(width, height, initial);
  initialized = true;
  return true;
}

// Delete the owned grid when initialized, then clear the flag and report the original successful result.
bool CtPowerCellMap::Shutdown() {
  if (initialized && grid) {
    delete grid;
    grid = nullptr;
  }
  initialized = false;
  return true;
}

void CtPowerCellMap::GetValue(u32 x, u32 z, bool *out) const {
  *out = (grid->words[x][z >> 5] & (1u << (z & 31))) != 0;
}

void CtPowerCellMap::SetValue(u32 x, u32 z, const bool *value) {
  grid->Set(x, z, value);
}

// Set or clear every cell in the inclusive rectangle, visiting X before Z as in the installed loop.
void CtPowerCellMap::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const bool *value) {
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid->Set(x, z, value);
}

void CtPowerCellMap::SetAllCells(const bool *value) {
  grid->Fill(value);
}

// The original power layer uses an ordered map of power-plant type IDs to configured capacities.
static std::map<u32, u32> CtPowerCapacities;
static f32 CtPowerConservationSaving, CtPowerStairwellSaving;

SpUtilityMapSprite::SpUtilityMapSprite()
    : references(0), kind(0), palette(nullptr), width(0), height(0), postFilter(0), cornerX{0, 0, 1, 1},
      cornerZ{0, 1, 1, 0} {
}

SpUtilityMapSprite::~SpUtilityMapSprite() {
  ScRelease(&palette);
}

// Publish this raster for the Unknown and raster-source identities and retain a successful result.
bool SpUtilityMapSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxRasterSource);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, GxRasterSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpUtilityMapSprite::AddRef() {
  return ++references;
}

u32 SpUtilityMapSprite::Release() {
  return ScReleaseImpl(this, &references);
}

// Apply the requested sprite identifier and dimensions through the original virtual setters.
bool SpUtilityMapSprite::Init(u32 id, u32 columns, u32 rows) {
  SetId(id);
  SetBaseSize(columns, rows);
  return true;
}

u32 SpUtilityMapSprite::GetId() const {
  return kind;
}

bool SpUtilityMapSprite::SetId(u32 id) {
  kind = id;
  return true;
}

// Retain the incoming palette before releasing the previous one, allowing the same object to be supplied again.
bool SpUtilityMapSprite::SetPalette(GxPaletteView *source) {
  if (source)
    source->AddRef();
  ScRelease(&palette);
  palette = source;
  return true;
}

// Return the borrowed palette pointer without changing its reference count.
GxPaletteView *SpUtilityMapSprite::GetPalette() {
  return palette;
}

i32 SpUtilityMapSprite::GetBaseWidth() const {
  return width;
}

i32 SpUtilityMapSprite::GetBaseHeight() const {
  return width;
}

// Store both base raster dimensions in the order supplied by the caller.
bool SpUtilityMapSprite::SetBaseSize(u32 columns, u32 rows) {
  width = i32(columns);
  height = i32(rows);
  return true;
}

u8 SpUtilityMapSprite::SetPostFilter(u8 enabled) {
  postFilter = enabled;
  return enabled;
}

// Save the three supplied corners and derive the fourth with the original wrapped 32-bit arithmetic.
void SpUtilityMapSprite::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  cornerX[0] = x0;
  cornerX[1] = x1;
  cornerX[2] = i32(u32(x3) + u32(x1) - u32(x0));
  cornerX[3] = x3;
  cornerZ[0] = z0;
  cornerZ[1] = z1;
  cornerZ[2] = i32(u32(z3) + u32(z1) - u32(z0));
  cornerZ[3] = z3;
}

u8 SpUtilityMapSprite::IsPostFilter() const {
  return postFilter;
}

// Draw cells first and draw annotations only if that phase succeeds and a destination exists.
bool SpUtilityMapSprite::Draw(GxBuffer *target, const ScRect &rectangle, ScRect *clip) {
  if (!target || !DrawCells(target, rectangle, clip))
    return false;
  return DrawAnnotations(target, rectangle, clip);
}

bool SpUtilityMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

// Draw clipped destination cells in column order, scaling and vertically flipping source coordinates.
bool SpUtilityMapSprite::DrawCells(GxBuffer *target, const ScRect &rectangle, ScRect *clip) {
  i32 spanX = i32(u32(rectangle.right) - u32(rectangle.left));
  i32 spanZ = i32(u32(rectangle.bottom) - u32(rectangle.top));
  i32 left = 0, top = 0, right = spanX, bottom = spanZ;
  if (clip) {
    i32 x0 = i32(u32(clip->left) - u32(rectangle.left)), z0 = i32(u32(clip->top) - u32(rectangle.top));
    i32 x1 = i32(u32(clip->right) - u32(rectangle.left)), z1 = i32(u32(clip->bottom) - u32(rectangle.top));
    if (x0 > left)
      left = x0;
    if (z0 > top)
      top = z0;
    if (x1 < right)
      right = x1;
    if (z1 < bottom)
      bottom = z1;
  }
  u32 pixel = 0;
  i32 lastZ = i32(u32(height) - 1);
  for (i32 x = left; x < right; ++x) {
    i32 sourceX = i32(u32(x) * u32(width)) / spanX;
    for (i32 z = top; z < bottom; ++z) {
      i32 sourceZ = i32(u32(z) * u32(height)) / spanZ;
      if (GetColor(u32(sourceX), u32(lastZ) - u32(sourceZ), &pixel))
        target->SetPixel(u32(x) + u32(rectangle.left), u32(z) + u32(rectangle.top), pixel);
    }
  }
  return true;
}

u8 SpUtilityMapSprite::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 SpUtilityMapSprite::DebugClassTag(ScStringView *) {
  return 0;
}

u8 SpUtilityMapSprite::DebugTypeTag(ScStringView *) {
  return 0;
}

bool SpUtilityMapSprite::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 SpUtilityMapSprite::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 SpUtilityMapSprite::SetDebugClassTag(const ScStringView &) {
  return 0;
}

// Retain the supplied city, layer and zone in order, then set the power sprite's identifier and dimensions.
CtPowerMapSprite::CtPowerMapSprite(CtCity *sourceCity, CtPowerSimulation *sourceLayer, CtZoneLayer *sourceZones)
    : city(nullptr), layer(sourceLayer), zones(nullptr) {
  if (sourceCity)
    city = ScAddRefNotNull(sourceCity);
  layer->AddRef();
  if (sourceZones)
    zones = ScAddRefNotNull(sourceZones);
  u32 rows = layer->CellCountZ(), columns = layer->CellCountX();
  SetId(10018);
  SetBaseSize(columns, rows);
}

// Release the retained city, power layer and zone in their installed destruction order.
CtPowerMapSprite::~CtPowerMapSprite() {
  ScRelease(&city);
  ScRelease(&layer);
  ScRelease(&zones);
}

// Color occupied power consumers and zoned cells using the layer's power bit and current palette.
bool CtPowerMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  bool powered = false, visible = false;
  ScOccupant *occupant = nullptr;
  if (city->SurfaceOccupantManager()->GetOccupantAtCell(x, z, &occupant) && occupant) {
    BdBuilding *building = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      visible = building->PowerRequired() != 0;
      building->Release();
    }
    occupant->Release();
  } else {
    u8 zone = 0;
    zones->GetValue(x, z, &zone);
    if (zone == 0 || zone == 17)
      return false;
    visible = true;
  }
  if (visible) {
    layer->GetValue(x, z, &powered);
    GxPaletteView *colors = GetPalette();
    if (colors)
      return colors->GetColor(powered == 0, out);
  }
  return false;
}

CtPowerManipulator::CtPowerManipulator(CtPowerSimulation *owner) : references(0), layer(owner) {
}

// Destroy the manipulator without releasing the borrowed power layer.
CtPowerManipulator::~CtPowerManipulator() = default;

// Publish the manipulator for Unknown and city-layer-manipulator identities, retaining successful queries.
bool CtPowerManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtPowerManipulator::AddRef() {
  return ++references;
}

u32 CtPowerManipulator::Release() {
  return ScReleaseImpl(this, &references);
}

bool CtPowerManipulator::Apply(CtAgent *) {
  layer->UpdateLayer();
  layer->UpdateUsageHistory();
  return true;
}

bool CtPowerManipulator::IsLockingCity() {
  return false;
}

bool CtPowerManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtPowerManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool CtPowerManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

CtPowerSimulation::CtPowerSimulation()
    : initialized(0), width(0), wordsPerRow(0), lastX(0), lastZ(0), city(nullptr), buildings(nullptr), zones(nullptr),
      neighbors(nullptr), budget(nullptr), occupants(nullptr), unrecovered80(0), manipulator(nullptr),
      markBits(nullptr), wireBits(nullptr), zoneSender(0), occupantSender(0), bitWordCount(0), unrecovered108{},
      powerProduced(0), powerUsed(0), currentUsage(0), conservationOn(0), stairwellOn(0), unrecovered126Power{},
      messageServer(nullptr), agentTree(nullptr), buyingDeals{}, sellingDeals{}, consumptionHistory{}, historyCycle(0),
      annualConsumption{}, usageHistory(nullptr), consumerDemand(nullptr), consumerCount(0), poweredConsumers(0),
      unpoweredConsumers(0), powerNeeded(0), agingPlants(0), agingNuclearPlants(0), suppressCellLock(0),
      unrecovered761{} {
}

CtPowerSimulation::~CtPowerSimulation() {
  if (initialized)
    Shutdown();
}

// Publish the installed layer, sprite-factory or power subobject, otherwise delegate to the primary Boolean map.
// Successful adjusted-view queries retain the complete receiver through its live AddRef override.
bool CtPowerSimulation::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtPowerLayer, CtPowerLayer);
  return CtPowerCellMap::QueryInterface(iid, out);
}

u32 CtPowerSimulation::AddRef() {
  return CtPowerCellMap::AddRef();
}

u32 CtPowerSimulation::Release() {
  return CtPowerCellMap::Release();
}

// On the city-start message, register for occupant changes and refresh every cell and the power layer under the city lock.
bool CtPowerSimulation::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CityViewReady) {
    occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &occupantSender, true);
    ScGetSimutilCity()->Lock();
    suppressCellLock = 1;
    CellChangedAll(occupantSender);
    suppressCellLock = 0;
    UpdateLayer();
    ScGetSimutilCity()->Unlock();
  }
  return true;
}

// Dispatch the installed power query requests and publish the graph model when its model identity is requested.
bool CtPowerSimulation::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened) {
    void *selected = nullptr;
    switch (u32(message->data1)) {
    case kUiQueryTagGraphs:
      if (source && source->QueryInterface(IID_UiGraphsViewIdentity, &selected)) {
        ScObjectCollection *collection = static_cast<ScObjectCollection *>(selected);
        collection->Add(usageHistory);
        collection->Release();
      }
      return false;
    case kUiQueryTagDialog:
      if (source && source->QueryInterface(IID_UiQueryWindow, &selected))
        static_cast<UiQueryWindow *>(selected)->Release();
      return false;
    case kUiQueryTagDebug:
      if (source && source->QueryInterface(IID_UiQuerySection, &selected))
        static_cast<UiQuery *>(selected)->Release();
      return false;
    case kUiQueryTagMaps:
      if (source && source->QueryInterface(IID_UiRasterQuery, &selected)) {
        UiRasterQuery *query = static_cast<UiRasterQuery *>(selected);
        void *raster = nullptr;
        bool result = CreateMapSprite(10018, IID_GxRasterSource, &raster);
        if (result)
          result = query->AddRaster(static_cast<GxRasterSource *>(raster)) != 0;
        if (raster)
          static_cast<GxRasterSource *>(raster)->Release();
        query->Release();
        return result;
      }
      return false;
    case kUiQueryTagStandard:
      if (source && source->QueryInterface(IID_UiQuerySection, &selected)) {
        UiQuery *query = static_cast<UiQuery *>(selected);
        bool result = DoStandardQueryViewInfo(message, query) != 0;
        query->Release();
        return result;
      }
      return false;
    default:
      return false;
    }
  }
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      ScModelReceiver *receiver = static_cast<ScModelReceiver *>(selected);
      receiver->DumpGraphData(usageHistory);
      receiver->Release();
    }
    return true;
  }
  return false;
}

u32 CtPowerSimulation::GetInfoStringCount() {
  return 1;
}

// Produce the single power-status query row for a location, returning the installed low-byte success value.
u8 CtPowerSimulation::GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 index) {
  if (index >= GetInfoStringCount())
    return 0;
  ScString caption = ScUtilityQueryText(kRsCityServicePoweredText);
  label->Copy(caption);
  bool powered = false;
  GetValue(x, z, &powered);
  ScString description = ScUtilityQueryText(powered ? 5 : 6);
  value->Copy(description);
  return 1;
}

// Add the power-status row for a query location, marking unpowered cells with the original warning byte.
u8 CtPowerSimulation::DoStandardQueryViewInfo(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  if (!query || !query->GetLocation(&location))
    return 0;
  u32 x = u32(location.x >> kCtMicroCoordinateShift), z = u32(location.z >> kCtMicroCoordinateShift);
  if (GetInfoStrings(x, z, &label, &value, 0)) {
    bool powered = false;
    GetValue(x, z, &powered);
    query->AddText(&label, &value, 512, u8(!powered));
  }
  return 1;
}

u8 CtPowerSimulation::MakeBuyingDealWithNeighbor(i32 neighbor, u32) {
  buyingDeals[neighbor].low = kCtPowerBuyingDealQuantitySentinel;
  return 1;
}

u8 CtPowerSimulation::MakeSellingDealWithNeighbor(i32 neighbor, u32 amount) {
  sellingDeals[neighbor].low = amount;
  return 1;
}

// Load up to eight active neighbor deals and initialize the four buying and selling quantities.
void CtPowerSimulation::InitInfoAboutNeighborDeals() {
  u8 count = 8;
  CtDealRecord deals[8]{};
  for (u32 index = 0; index < 4; ++index) {
    buyingDeals[index].low = 0;
    sellingDeals[index].low = 0;
  }
  neighbors->GetNeighborDeals(&count, deals);
  for (u32 index = 0; index < count; ++index)
    if (deals[index].type == 0)
      MakeBuyingDealWithNeighbor(deals[index].value_10, deals[index].value_08);
    else if (deals[index].type == 1)
      MakeSellingDealWithNeighbor(deals[index].value_10, deals[index].value_08);
  for (u32 index = 0; index < 4; ++index) {
    buyingDeals[index].high = buyingDeals[index].low;
    sellingDeals[index].high = sellingDeals[index].low;
  }
}

// Publish accumulated power purchases and sales, cancelling a selling deal that retains unused capacity.
void CtPowerSimulation::ExamineNeighborDeals() {
  u32 bought = 0, sold = 0;
  bool cancelled = false;
  for (u32 index = 0; index < 4; ++index) {
    if (buyingDeals[index].low) {
      u32 amount = buyingDeals[index].low - buyingDeals[index].high;
      consumptionHistory[historyCycle][0] = i32(u32(consumptionHistory[historyCycle][0]) + amount);
      bought += amount;
    }
    neighbors->PublishDeal(kCtDealImportPower, i32(bought));
    if (sellingDeals[index].low) {
      u32 amount = sellingDeals[index].low - sellingDeals[index].high;
      if (sellingDeals[index].high) {
        neighbors->CancelDeal(kCtDealExportPower);
        cancelled = true;
        continue;
      }
      sold += amount;
    }
    if (!cancelled)
      neighbors->PublishDeal(kCtDealExportPower, i32(sold));
  }
}

// Assign usage to every plant marked in the current region and record it in the source-type history.
void CtPowerSimulation::MarkPowerPlantsAsVisited(u32 used, u32 capacity) {
  f32 fraction = capacity ? f32(f64(used) / f64(i32(capacity))) : 0.0f;
  for (CtPowerPlantNode &node : plants) {
    if (node.region == 1) {
      u32 amount = u32(i64(f64(node.plant->ActualCapacity()) * fraction));
      node.plant->SetUsage(amount);
      u32 type = node.plant->PowerSourceType();
      consumptionHistory[historyCycle][type] = i32(u32(consumptionHistory[historyCycle][type]) + amount);
      node.region = 2;
    }
  }
}

// Grow one connected wire region, allocate its plant and neighbor supply, and update its powered cells.
void CtPowerSimulation::UpdatePowerGrid(u32 x, u32 z) {
  SetMarkBit(x, z);
  i32 firstRow = i32(z), endRow = i32(z + 1);
  CtWaterBitsSelectionGrow(markBits, wireBits, i32(width), 600, &firstRow, &endRow);
  if (firstRow > endRow) {
    firstRow = i32(z);
    endRow = i32(z + 1);
  }
  u32 plantCapacity = 0;
  for (CtPowerPlantNode &node : plants) {
    if (node.region)
      continue;
    void *selected = nullptr;
    if (node.plant->QueryInterface(IID_ScOccupant, &selected)) {
      ScOccupant *occupant = static_cast<ScOccupant *>(selected);
      u32 plantX, plantZ;
      occupant->GetLocationInCellCoords(&plantX, &plantZ);
      occupant->Release();
      if (IsMarked(plantX, plantZ)) {
        plantCapacity += node.plant->ActualCapacity();
        node.region = 1;
      }
    }
  }
  powerProduced += plantCapacity;
  u8 connected[4]{};
  bool hasNeighbor = false;
  for (CtPowerNeighborLink &link : neighborLinks) {
    if (IsMarked(link.x, link.z)) {
      link.unknown = (link.unknown & kCtMicroCellMask) | 1;
      hasNeighbor = true;
      connected[link.neighbor] = 1;
    }
  }
  u32 localCapacity = hasNeighbor ? plantCapacity : u32(i64(f64(plantCapacity) * 1.1));
  u32 purchased = 0;
  for (u32 index = 0; index < 4; ++index)
    if (connected[index])
      purchased += buyingDeals[index].high;
  u32 supply = purchased + localCapacity;
  f32 factor = 1.0f;
  if (conservationOn)
    factor = f32(1.0 - f64(CtPowerConservationSaving));
  if (stairwellOn)
    factor = f32(f64(factor) - f64(CtPowerStairwellSaving));
  u32 remaining = u32(i64(f64(supply) / factor));
  u32 word = u32(firstRow) * wordsPerRow, endWord = u32(endRow) * wordsPerRow;
  u32 row = u32(firstRow), firstX = 0;
  while (word < endWord) {
    u32 currentX = firstX, cell = firstX + row * width;
    while (markBits[word]) {
      if (markBits[word] & 1) {
        u8 demand = consumerDemand[cell];
        bool powered = true;
        if (demand) {
          ++poweredConsumers;
          SetValue(currentX, row, &powered);
          if (demand > remaining) {
            remaining = 0;
            for (u32 index = 0; index < 4; ++index)
              if (connected[index])
                buyingDeals[index].high = 0;
            ResetAllMarkBits(word, endWord);
            // The original shortage path also reaches the later supply-minus-unused accumulation.
            powerUsed += supply;
            MarkPowerPlantsAsVisited(localCapacity, plantCapacity);
            break;
          }
          remaining -= demand;
        } else {
          SetValue(currentX, row, &powered);
        }
      }
      markBits[word] >>= 1;
      ++currentX;
      ++cell;
    }
    firstX += 32;
    if (firstX >= width) {
      firstX = 0;
      ++row;
    }
    ++word;
  }
  u32 unused = u32(i64(f64(remaining) * factor));
  powerUsed += supply - unused;
  for (u32 index = 0; index < 4; ++index) {
    if (!connected[index])
      continue;
    if (sellingDeals[index].high > unused) {
      sellingDeals[index].high -= unused;
      unused = 0;
    } else {
      unused -= sellingDeals[index].high;
      sellingDeals[index].high = 0;
    }
  }
  if (unused >= purchased) {
    MarkPowerPlantsAsVisited(localCapacity + purchased - unused, plantCapacity);
    return;
  }
  u32 amount = purchased - unused;
  for (u32 index = 0; index < 4; ++index) {
    if (!connected[index])
      continue;
    if (buyingDeals[index].high < amount) {
      amount -= buyingDeals[index].high;
      buyingDeals[index].high = 0;
    } else {
      buyingDeals[index].high -= amount;
      amount = 0;
    }
  }
  MarkPowerPlantsAsVisited(localCapacity, plantCapacity);
}

// Refresh the monthly power history, plant and neighbor regions, then publish the power-layer update message.
void CtPowerSimulation::UpdateLayer() {
  conservationOn = city->OrdinanceLayer()->QueryOrdinanceOn(kCtOrdinance_PowerConservation);
  stairwellOn = city->OrdinanceLayer()->QueryOrdinanceOn(kCtOrdinance_StairwellLighting);
  InitInfoAboutNeighborDeals();
  historyCycle = i32(u32(historyCycle) + 1);
  if (historyCycle >= 12)
    historyCycle = 0;
  for (u32 index = 0; index < 10; ++index) {
    annualConsumption[index] = i32(u32(annualConsumption[index]) - u32(consumptionHistory[historyCycle][index]));
    consumptionHistory[historyCycle][index] = 0;
  }
  powerProduced = 0;
  powerUsed = 0;
  poweredConsumers = 0;
  unpoweredConsumers = 0;
  bool clear = false;
  SetAllCells(&clear);
  for (CtPowerNeighborLink &link : neighborLinks)
    link.unknown &= kCtMicroCellMask;
  for (auto it = plants.begin(); it != plants.end();) {
    auto current = it++;
    current->region = 0;
    current->plant->Update();
  }
  agingPlants = 0;
  agingNuclearPlants = 0;
  for (CtPowerPlantNode &node : plants) {
    if (node.region)
      continue;
    if (node.plant->IsAging()) {
      ++agingPlants;
      if (node.plant->PowerSourceType() == 5)
        ++agingNuclearPlants;
    }
    void *selected = nullptr;
    if (node.plant->QueryInterface(IID_ScOccupant, &selected)) {
      ScOccupant *occupant = static_cast<ScOccupant *>(selected);
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      occupant->Release();
      UpdatePowerGrid(x, z);
    }
  }
  for (CtPowerNeighborLink &link : neighborLinks)
    if (buyingDeals[link.neighbor].low && u8(link.unknown) == 0)
      UpdatePowerGrid(link.x, link.z);
  ExamineNeighborDeals();
  for (u32 index = 0; index < 10; ++index)
    annualConsumption[index] = i32(u32(annualConsumption[index]) + u32(consumptionHistory[historyCycle][index]));
  currentUsage = powerProduced > powerUsed ? (100u * powerProduced - 100u * powerUsed) / powerProduced : 0;
  unpoweredConsumers = consumerCount - poweredConsumers;
  if (i32(unpoweredConsumers) < 0)
    unpoweredConsumers = 0;
  ScMessage message(kCtMsg_PowerMapUpdated, reinterpret_cast<uptr>(static_cast<CtPowerLayer *>(this)), 0, 0);
  messageServer->MessagePost(message, false);
}

u32 CtPowerSimulation::CellCountX() const {
  return CtPowerCellMap::CellCountX();
}

u32 CtPowerSimulation::CellCountZ() const {
  return CtPowerCellMap::CellCountZ();
}

void CtPowerSimulation::GetValueSint32(u32 x, u32 z, i32 *out) const {
  CtPowerCellMap::GetValueSint32(x, z, out);
}

bool CtPowerSimulation::InBounds(u32 x, u32 z) const {
  return CtPowerCellMap::InBounds(x, z);
}

bool CtPowerSimulation::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtPowerCellMap::InBounds(x0, z0, x1, z1);
}

bool CtPowerSimulation::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtPowerCellMap::ValidRect(x0, z0, x1, z1);
}

bool CtPowerSimulation::SerialRead(RsStream *record) {
  return CtPowerCellMap::SerialRead(record);
}

bool CtPowerSimulation::SerialWrite(RsStream *record) const {
  return CtPowerCellMap::SerialWrite(record);
}

bool CtPowerSimulation::Init(CtCity *sourceCity, const bool *initial) {
  return CtPowerCellMap::Init(sourceCity, initial);
}

// Initialize the city's power grids, retained services, change subscriptions and usage-history graph.
bool CtPowerSimulation::Init(CtCity *sourceCity) {
  if (initialized)
    Shutdown();
  memset(consumptionHistory, 0, sizeof(consumptionHistory));
  memset(annualConsumption, 0, sizeof(annualConsumption));
  BdPowerPlant::CitySpecificStaticInit(sourceCity);
  unrecovered108[0] = 0;
  city = ScAddRefNotNull(sourceCity);
  bool empty = false;
  CtPowerCellMap::Init(city, &empty);
  width = city->CellCountX();
  wordsPerRow = width >> 5;
  lastX = width - 1;
  u32 height = city->CellCountZ();
  lastZ = height - 1;
  u32 cells = height * width;
  consumerDemand = new u8[cells]{};
  bitWordCount = cells >> 5;
  markBits = new u32[bitWordCount]{};
  wireBits = new u32[bitWordCount]{};
  for (u32 index = 0; index < 4; ++index) {
    buyingDeals[index] = {0, 0};
    sellingDeals[index] = {0, 0};
  }
  ApGameInterface *application = nullptr;
  ScGetSimutilFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application));
  agentTree = ScAddRefNotNull(application->GetAppAgentTypeTree());
  application->Release();
  occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  buildings = ScAddRefNotNull(city->BuildingLayer());
  budget = ScAddRefNotNull(city->BudgetLayer());
  zones = ScAddRefNotNull(city->ZoneLayer());
  CtChangeSource *source = nullptr;
  zones->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
  source->AddReceiver(static_cast<CtChangeReceiver *>(this), &zoneSender);
  source->Release();
  CtLayer *neighborLayer = city->GetSpecificLayer(kCtNeighborsLayerType);
  bool result = neighborLayer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  conservationOn = 0;
  stairwellOn = 0;
  suppressCellLock = 0;
  manipulator = ScAddRefNotNull(new CtPowerManipulator(this));
  messageServer = ScGetSimutilMessageServer();
  messageServer->AddNotification(static_cast<CtLayer *>(this), kWnMsg_CityViewReady);
  usageHistory = ScAddRefNotNull(new CtUtility::ScGraphInfo(0));
  usageHistory->SetMaxValue(100);
  usageHistory->SetAutoMax(true);
  for (i32 index = -1; index < 3; ++index) {
    RsUtilityText resource(kRsGamePowerText, kRsGameTextGroup);
    ScString text = resource.GetText(), copy(text);
    if (index < 0)
      usageHistory->SetSelectionLabel(copy);
    else
      usageHistory->SetCurrentValueLabel(ScGraphDataSet(index), copy);
  }
  u32 selectionKey = kCtPowerGraphSelectionId, valueKey = kCtPowerGraphValueId;
  usageHistory->SetSelectionLabelID(selectionKey);
  usageHistory->SetValueLabelID(valueKey);
  usageHistory->SetColor(kCtPowerUsageHistoryColor);
  initialized = 1;
  return result;
}

bool CtPowerSimulation::Init(CtCity *, ScImporter *) {
  return true;
}

// Read the rolling consumption matrix, annual totals, graph and aging count in the original short-circuit order.
bool CtPowerSimulation::Init(CtCity *input, RsDatabaseSegment *segment) {
  ScArchiveFrame frame(input, segment, {kRsCityLayerRecordType, kCtPowerSimulationLayerType, 0});
  bool result = frame.valid != 0;
  RsStream *stream = frame.GetStream();
  if (result)
    result = stream->ReadI32(&historyCycle);
  for (u32 month = 0; month < 12; ++month)
    for (u32 type = 0; result && type < 10; ++type)
      result = stream->ReadI32(&consumptionHistory[month][type]);
  for (u32 type = 0; result && type < 10; ++type)
    result = stream->ReadI32(&annualConsumption[type]);
  result = result && usageHistory->SerialRead(input, stream) != 0 && stream->ReadU32(&agingPlants);
  return result;
}

// Write the consumption matrix and annual totals before the history graph and aging-plant count.
bool CtPowerSimulation::Save(CtCity *input, RsDatabaseSegment *segment) {
  ScArchiveFrame frame(input, segment, {kRsCityLayerRecordType, kCtPowerSimulationLayerType, 0}, 1, 0);
  bool result = frame.valid != 0;
  RsStream *stream = frame.GetStream();
  if (!result || !stream)
    return false;
  result = stream->WriteI32(historyCycle);
  for (u32 month = 0; month < 12; ++month)
    for (u32 type = 0; result && type < 10; ++type)
      result = stream->WriteI32(consumptionHistory[month][type]);
  for (u32 type = 0; result && type < 10; ++type)
    result = stream->WriteI32(annualConsumption[type]);
  result = result && usageHistory->SerialWrite(input, stream) != 0 && stream->WriteU32(agingPlants);
  return result;
}

// Load plant capacities and ordinance savings from the original power-layer tuning resources.
bool CtPowerSimulation::StaticInit(CtDefinition *) {
  GeIni ini;
  ScString path, value;
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SC3PowerLayer.INI", 22);
  ini.SetPath(path);
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  ini.EnumEntries(ScString("PowerPlantCapacities"), RegisterPlantCapacity, this);
  ScString section("MiscPowerLayerTunables");
  if (ini.ReadEntry(section, ScString("SavingDueToPowerConservationOrdinance"), &value))
    CtPowerConservationSaving = f32(f64(GeParseNumber(value.ToChar())) * f64(0.01f));
  if (ini.ReadEntry(section, ScString("SavingDueToStairwellLightingOrdinance"), &value))
    CtPowerStairwellSaving = f32(f64(GeParseNumber(value.ToChar())) * f64(0.01f));
  BdPowerPlant::StaticInit();
  return true;
}

// Unregister the initialized power layer and release its graph, plant records, services and grids in original order.
// The retained zone and occupant receivers are removed before their providers are released.
bool CtPowerSimulation::Shutdown() {
  if (!initialized)
    return true;
  messageServer->RemoveNotification(static_cast<CtLayer *>(this), kWnMsg_CityViewReady);
  ScRelease(&usageHistory);
  for (CtPowerPlantNode &node : plants)
    node.plant->Release();
  plants.clear();
  if (manipulator)
    manipulator->Release();
  manipulator = nullptr;
  CtChangeSource *source = nullptr;
  zones->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
  source->RemoveReceiver(static_cast<CtChangeReceiver *>(this));
  source->Release();
  zones->Release();
  zones = nullptr;
  budget->Release();
  budget = nullptr;
  buildings->Release();
  buildings = nullptr;
  ScRelease(&neighbors);
  occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  occupants->Release();
  occupants = nullptr;
  delete[] wireBits;
  wireBits = nullptr;
  delete[] markBits;
  markBits = nullptr;
  delete[] consumerDemand;
  consumerDemand = nullptr;
  lastX = 0;
  lastZ = 0;
  bool result = CtPowerCellMap::Shutdown();
  agentTree->Release();
  agentTree = nullptr;
  city->Release();
  city = nullptr;
  BdPowerPlant::CitySpecificStaticShutdown();
  initialized = 0;
  return result;
}

void CtPowerSimulation::GetValue(u32 x, u32 z, bool *out) const {
  CtPowerCellMap::GetValue(x, z, out);
}

void CtPowerSimulation::SetValue(u32 x, u32 z, const bool *value) {
  CtPowerCellMap::SetValue(x, z, value);
}

void CtPowerSimulation::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const bool *value) {
  CtPowerCellMap::SetValue(x0, z0, x1, z1, value);
}

void CtPowerSimulation::SetAllCells(const bool *value) {
  CtPowerCellMap::SetAllCells(value);
}

bool CtPowerSimulation::SimulationBegin() {
  return true;
}

bool CtPowerSimulation::SimulationEnd() {
  return true;
}

u32 CtPowerSimulation::GetLayerType() {
  return kCtPowerSimulationLayerType;
}

// Publish the stored daily manipulator and flag for its agent type; leave the flag untouched otherwise.
bool CtPowerSimulation::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  if (type.Id() == kCtAgentType_AreaAgent) {
    *out = manipulator;
    *flags = 4;
  } else {
    *out = nullptr;
  }
  return true;
}

bool CtPowerSimulation::DebugClassTag(ScStringView *) {
  return false;
}

bool CtPowerSimulation::DebugTypeTag(ScStringView *) {
  return false;
}

// Sum all ten annual power-source totals using the original wrapped DWORD arithmetic.
u32 CtPowerSimulation::GetTotalAnnualConsumption() {
  u32 total = 0;
  for (u32 value : annualConsumption)
    total += value;
  return total;
}

u32 CtPowerSimulation::GetAnnualConsumptionOfPowerType(u32 type) {
  return annualConsumption[type];
}

u32 CtPowerSimulation::GetTotalCityPowerUsed() {
  return powerUsed;
}

u32 CtPowerSimulation::GetTotalCityPowerProduced() {
  return powerProduced;
}

u32 CtPowerSimulation::GetTotalPowerNeededByTheCity() {
  return powerNeeded;
}

u32 CtPowerSimulation::GetNumberOfUnpoweredConsumers() {
  return unpoweredConsumers;
}

u32 CtPowerSimulation::GetNumberOfPoweredConsumers() {
  return poweredConsumers;
}

u32 CtPowerSimulation::GetNumberOfAgingPowerPlants() {
  return agingPlants;
}

u32 CtPowerSimulation::GetNumberOfAgingNuclearPlants() {
  return agingNuclearPlants;
}

// Look up a configured plant capacity, leaving the output unchanged when the type is absent.
u8 CtPowerSimulation::GetPlantCapacity(u32 type, u32 *capacity) {
  auto entry = CtPowerCapacities.find(type);
  if (entry == CtPowerCapacities.end())
    return 0;
  *capacity = entry->second;
  return 1;
}

u32 CtPowerSimulation::GetMapSpriteCount() {
  return 1;
}

bool CtPowerSimulation::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10018)
    return false;
  CtPowerMapSprite *sprite = new CtPowerMapSprite(city, this, city->ZoneLayer());
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->city = nullptr;
  sprite->layer = nullptr;
  sprite->zones = nullptr;
  delete sprite;
  return false;
}

bool CtPowerSimulation::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10018;
  return true;
}

// Record a conductive occupant, its demand and its retained plant or connected neighbor-edge identity.
bool CtPowerSimulation::OccupantInserted(u32, ScOccupant *occupant) {
  if (!SetsWireBits(occupant))
    return true;
  ScGetSimutilCity()->Lock();
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  GeCellRect expanded = bounds;
  ExpandRectToIncludeNeighborhood(&expanded);
  SetWireBits(expanded);
  void *selected = nullptr;
  if (occupant->QueryInterface(IID_BdBuilding, &selected)) {
    BdBuilding *building = static_cast<BdBuilding *>(selected);
    u8 demand = building->PowerRequired();
    consumerDemand[z * width + x] = demand;
    if (demand) {
      ++consumerCount;
      powerNeeded += demand;
    }
    building->Release();
  }
  selected = nullptr;
  if (occupant->QueryInterface(IID_BdPowerPlant, &selected)) {
    u8 empty = 0;
    zones->SetValue(bounds.left, bounds.top, bounds.right, bounds.bottom, &empty);
    plants.push_back({0, static_cast<BdPowerPlant *>(selected)});
  } else if (IsOnTheEdge(x, z) && neighbors->HasConnection(3, i32(x), i32(z))) {
    neighborLinks.push_back({x, z, i32(i8(DetermineNeighbor(x, z))), 0});
  }
  ScGetSimutilCity()->Unlock();
  return true;
}

// Remove a conductive occupant's demand and its first matching plant or neighbor record after rebuilding wires.
// Count reductions preserve the original signed-negative clamp and retained-interface release order.
bool CtPowerSimulation::OccupantRemoved(u32, ScOccupant *occupant) {
  if (!initialized || !SetsWireBits(occupant))
    return true;
  ScGetSimutilCity()->Lock();
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  ExpandRectToIncludeNeighborhood(&bounds);
  ResetWireBits(bounds);
  UpdateWireBits(bounds);
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  consumerDemand[z * width + x] = 0;
  void *selected = nullptr;
  if (occupant->QueryInterface(IID_BdBuilding, &selected)) {
    BdBuilding *building = static_cast<BdBuilding *>(selected);
    u8 demand = building->PowerRequired();
    if (demand) {
      --consumerCount;
      if (i32(consumerCount) < 0)
        consumerCount = 0;
      powerNeeded -= demand;
      if (i32(powerNeeded) < 0)
        powerNeeded = 0;
    }
    building->Release();
  }
  selected = nullptr;
  if (occupant->QueryInterface(IID_BdPowerPlant, &selected)) {
    BdPowerPlant *plant = static_cast<BdPowerPlant *>(selected);
    for (auto it = plants.begin(); it != plants.end(); ++it) {
      if (it->plant == plant) {
        it->plant->Release();
        plants.erase(it);
        break;
      }
    }
    plant->Release();
  } else {
    occupant->GetLocationInCellCoords(&x, &z);
    if (IsOnTheEdge(x, z)) {
      for (auto it = neighborLinks.begin(); it != neighborLinks.end(); ++it) {
        if (it->x == x && it->z == z) {
          neighborLinks.erase(it);
          break;
        }
      }
    }
  }
  ScGetSimutilCity()->Unlock();
  return true;
}

bool CtPowerSimulation::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool CtPowerSimulation::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

// Notify the existing cell-change operation for every cell through both inclusive layer bounds.
bool CtPowerSimulation::CellChangedAll(u32 sender) {
  u32 x = 0;
  do {
    u32 z = 0;
    do {
      CellChanged(sender, x, z++);
    } while (z <= lastZ);
    ++x;
  } while (x <= lastX);
  return true;
}

// Update the neighborhood of a single changed zone cell while observing the layer's lock-suppression flag.
// The sender is unused; a failed zone read with an unwritten output is outside the original defined path.
bool CtPowerSimulation::CellChanged(u32, u32 x, u32 z) {
  if (!suppressCellLock)
    ScGetSimutilCity()->Lock();
  u8 zone = 0;
  zones->GetValue(x, z, &zone);
  if (!zone || zone == 17) {
    if (IsWired(x, z)) {
      GeCellRect bounds;
      GetNeighborhoodOfPoint(x, z, &bounds);
      ResetWireBits(bounds);
      UpdateWireBits(bounds);
    }
  } else {
    GeCellRect bounds;
    GetNeighborhoodOfPoint(x, z, &bounds);
    SetWireBits(bounds);
  }
  if (!suppressCellLock)
    ScGetSimutilCity()->Unlock();
  return true;
}

// Refresh the expanded rectangle after reading the changed zone at its original upper-left cell.
// This rectangle overload takes the city lock unconditionally, independent of single-cell suppression.
bool CtPowerSimulation::CellChanged(u32, u32 x0, u32 z0, u32 x1, u32 z1) {
  ScGetSimutilCity()->Lock();
  GeCellRect bounds{x0, z0, x1, z1};
  ExpandRectToIncludeNeighborhood(&bounds);
  u8 zone = 0;
  zones->GetValue(x0, z0, &zone);
  if (!zone || zone == 17) {
    ResetWireBits(bounds);
    UpdateWireBits(bounds);
  } else {
    SetWireBits(bounds);
  }
  ScGetSimutilCity()->Unlock();
  return true;
}

void CtPowerSimulation::RegisterPlantCapacity(const ScString &key, const ScString &value, void *) {
  u32 type = GeParseNumber(key.ToChar()), capacity = GeParseNumber(value.ToChar());
  CtPowerCapacities[type] = capacity;
}

bool CtPowerSimulation::StaticShutdown() {
  CtPowerCapacities.clear();
  return true;
}

void CtPowerSimulation::UpdateUsageHistory() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  usageHistory->SetMonthData(currentUsage);
  if (month == 1) {
    usageHistory->SetYearData(usageHistory->GetMonthAverage());
    if (year % 10 == 0)
      usageHistory->SetDecadeData(usageHistory->GetYearAverage());
  }
}

void CtPowerSimulation::ResetWireBit(u32 x, u32 z) {
  wireBits[(x >> 5) + z * wordsPerRow] &= ~(1u << (x & 31));
}

void CtPowerSimulation::SetWireBit(u32 x, u32 z) {
  wireBits[(x >> 5) + z * wordsPerRow] |= 1u << (x & 31);
}

u8 CtPowerSimulation::IsWired(u32 x, u32 z) const {
  return (wireBits[(x >> 5) + z * wordsPerRow] & (1u << (x & 31))) != 0;
}

void CtPowerSimulation::SetMarkBit(u32 x, u32 z) {
  markBits[(x >> 5) + z * wordsPerRow] |= 1u << (x & 31);
}

void CtPowerSimulation::ResetMarkBit(u32 x, u32 z) {
  markBits[(x >> 5) + z * wordsPerRow] &= ~(1u << (x & 31));
}

u8 CtPowerSimulation::IsMarked(u32 x, u32 z) const {
  return (markBits[(x >> 5) + z * wordsPerRow] & (1u << (x & 31))) != 0;
}

// Clear each region mark in the inclusive rectangle, visiting X before Z.
void CtPowerSimulation::ResetMarkBits(const GeCellRect &bounds) {
  for (u32 x = bounds.left; x <= bounds.right; ++x)
    for (u32 z = bounds.top; z <= bounds.bottom; ++z)
      ResetMarkBit(x, z);
}

// Clear every wire bit in the inclusive rectangle, visiting X before Z.
void CtPowerSimulation::ResetWireBits(const GeCellRect &bounds) {
  for (u32 x = bounds.left; x <= bounds.right; ++x)
    for (u32 z = bounds.top; z <= bounds.bottom; ++z)
      ResetWireBit(x, z);
}

// Set every wire bit in the inclusive rectangle without modifying neighboring bits.
void CtPowerSimulation::SetWireBits(const GeCellRect &bounds) {
  for (u32 x = bounds.left; x <= bounds.right; ++x)
    for (u32 z = bounds.top; z <= bounds.bottom; ++z)
      SetWireBit(x, z);
}

void CtPowerSimulation::ResetAllMarkBits(u32 first, u32 end) {
  for (u32 index = first; index < end; ++index)
    markBits[index] = 0;
}

// Decide whether an occupant's agent type conducts power, excluding the original 15400 subtype.
u8 CtPowerSimulation::SetsWireBits(ScOccupant *occupant) {
  u32 type = occupant->GetAttribData()->TypeId();
  if (agentTree->IsAncestor(100, type))
    return !agentTree->IsAncestor(15400, type);
  if (agentTree->IsAncestor(17100, type))
    return !agentTree->IsAncestor(15400, type);
  return 0;
}

// Rebuild one wire neighborhood from the first conductive occupant or nonempty zone encountered.
void CtPowerSimulation::UpdateWireBit(u32 x, u32 z) {
  GeCellRect neighborhood;
  GetNeighborhoodOfPoint(x, z, &neighborhood);
  for (u32 column = neighborhood.left; column <= neighborhood.right; ++column) {
    for (u32 row = neighborhood.top; row <= neighborhood.bottom; ++row) {
      ScOccupant *occupant = nullptr;
      if (occupants->GetOccupantAtCell(column, row, &occupant)) {
        u8 conducts = SetsWireBits(occupant);
        GeCellRect bounds{};
        if (conducts) {
          occupant->GetBoundingRectInCellCoords(&bounds);
          ExpandRectToIncludeNeighborhood(&bounds);
        }
        occupant->Release();
        if (conducts) {
          SetWireBits(bounds);
          return;
        }
      }
      u8 zone = 0;
      zones->GetValue(column, row, &zone);
      if (zone && zone != 17) {
        GeCellRect bounds;
        GetNeighborhoodOfPoint(column, row, &bounds);
        SetWireBits(bounds);
        return;
      }
    }
  }
}

// Visit an inclusive rectangle in column order, rebuilding neighborhoods whose wire bit is clear.
void CtPowerSimulation::UpdateWireBits(const GeCellRect &bounds) {
  for (u32 x = bounds.left; x <= bounds.right; ++x)
    for (u32 z = bounds.top; z <= bounds.bottom; ++z)
      if (!IsWired(x, z))
        UpdateWireBit(x, z);
}

// Expand a rectangle by two cells in each direction and clip the result to the stored inclusive layer bounds.
void CtPowerSimulation::ExpandRectToIncludeNeighborhood(GeCellRect *bounds) const {
  bounds->left = bounds->left <= 2 ? 0 : bounds->left - 2;
  bounds->top = bounds->top <= 2 ? 0 : bounds->top - 2;
  bounds->right += 2;
  if (bounds->right > lastX)
    bounds->right = lastX;
  bounds->bottom += 2;
  if (bounds->bottom > lastZ)
    bounds->bottom = lastZ;
}

// Produce the clipped two-cell neighborhood around a point using the layer's inclusive bounds.
void CtPowerSimulation::GetNeighborhoodOfPoint(u32 x, u32 z, GeCellRect *bounds) const {
  bounds->left = x <= 2 ? 0 : x - 2;
  bounds->top = z <= 2 ? 0 : z - 2;
  bounds->right = x + 2;
  if (bounds->right > lastX)
    bounds->right = lastX;
  bounds->bottom = z + 2;
  if (bounds->bottom > lastZ)
    bounds->bottom = lastZ;
}

u8 CtPowerSimulation::IsOnTheEdge(u32 x, u32 z) {
  return x == 0 || z == 0 || x == lastX || z == lastZ;
}

// Identify the original edge direction, preserving its corner precedence and zero fallback.
u8 CtPowerSimulation::DetermineNeighbor(u32 x, u32 z) const {
  if (x == 0 && z <= lastZ)
    return 0;
  if (z == 0 && x != 0)
    return 1;
  if (x == lastX && z != 0)
    return 2;
  if (z == lastZ && x < lastX)
    return 3;
  return 0;
}

// Original cISC3NuclearExplosionSimulator primary, produced by SIMSPR class E223BE6C for IID D223BE6C.
// Linux Init(int,int,int,signed char,signed char) and the installed SIMSPR producer establish the input contract.
class SpNuclearExplosionSimulator : public ScUnknown {
public:
  virtual u8 Init(i32 x, i32 z, i32 y, i8 first, i8 second) = 0;
};

static u8 ScSimutilApplicationInitialized;
static void *ScSimutilApplication;

static ScRandom BdPowerRandom(kScRandomSeedFromClock);
static ScOccupantManager *BdPowerOccupants;
static ScDirtBag *BdPowerTerrain;
static GeBuildingLayer *BdPowerBuildings;
static ScMessageServer *BdPowerMessageServer;
static u32 BdMinimumOverworkMonths = 3, BdMaximumOverworkMonths = 15;
static u8 BdCoalLoaded, BdFusionLoaded, BdGasLoaded, BdMicrowaveLoaded;
static u8 BdNuclearLoaded, BdOilLoaded, BdSolarLoaded, BdWindLoaded;
static u8 BdWasteLoaded;
static u32 BdCoalMaximum = 240, BdCoalDecline = 180, BdFusionMaximum = 240, BdFusionDecline = 180;
static u32 BdGasMaximum = 240, BdGasDecline = 180, BdMicrowaveMaximum = 240, BdMicrowaveDecline = 180;
static u32 BdNuclearMaximum = 240, BdNuclearDecline = 180, BdOilMaximum = 240, BdOilDecline = 180;
static u32 BdSolarMaximum = 240, BdSolarDecline = 180, BdWindMaximum = 180, BdWindDecline = 120;
static f32 BdCoalVariation = 0.1f, BdFusionVariation = 0.1f, BdGasVariation = 0.1f, BdMicrowaveVariation = 0.1f;
static f32 BdNuclearVariation = 0.1f, BdOilVariation = 0.1f, BdSolarVariation = 0.1f, BdWindVariation = 0.1f;
static u32 BdWasteOptimal = 10000, BdWasteMaximum = 180, BdWasteDecline = 120;
static f32 BdWasteVariation = 0.1f;

// Preserve the installed magic-add conversion and low-word wrap for waste query numeric formatting.
// The volatile store fixes the binary's double-precision rounding boundary.
static u32 BdWasteQueryRound(f64 value) {
  volatile f64 sum = value + kScDoubleIntegerBias;
  f64 rounded = sum;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Borrow the COM object from this DLL's director, as.
// The director owns the COM reference; callers do not acquire a new one.
static ScCom *BdGetPowerCom() {
  return ScGetSimutilDirector()->GZCOM();
}

// Borrow the module's cached message server, resolving it through the original system-service identities on demand.
// The temporary acquired service reference is released after publishing the cache, as.
ScMessageServer *ScGetSimutilMessageServer() {
  if (!BdPowerMessageServer) {
    void *selected = nullptr;
    ScGetSimutilFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &selected);
    BdPowerMessageServer = static_cast<ScMessageServer *>(selected);
    if (BdPowerMessageServer)
      BdPowerMessageServer->Release();
  }
  return BdPowerMessageServer;
}

// Retain the city services shared by power-plant operations in the original initialization order.
// returns a low-byte success value; the power-layer initializer discards it.
u8 BdPowerPlant::CitySpecificStaticInit(CtCity *city) {
  BdPowerOccupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  BdPowerTerrain = ScAddRefNotNull(city->DirtBag());
  BdPowerBuildings = ScAddRefNotNull(city->BuildingLayer());
  return 1;
}

// Release the shared power-building services in reverse acquisition order and clear their cached pointers.
// returns a low-byte success value; the power-layer shutdown discards it.
u8 BdPowerPlant::CitySpecificStaticShutdown() {
  ScRelease(&BdPowerBuildings);
  ScRelease(&BdPowerTerrain);
  ScRelease(&BdPowerOccupants);
  return 1;
}

// Read the three common power-plant tuning entries while retaining each caller's separate original globals.
static void BdReadPlantTuning(const char *section, u32 *maximum, u32 *decline, f32 *variation) {
  GeIni ini;
  ScString path, value;
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SC3PowerLayer.INI", 22);
  ini.SetPath(path);
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  ScString name(section);
  if (ini.ReadEntry(name, ScString("MaxLifeSpan"), &value))
    *maximum = GeParseNumber(value.ToChar());
  if (!*maximum)
    *maximum = 1;
  if (ini.ReadEntry(name, ScString("DeclineAge"), &value))
    *decline = GeParseNumber(value.ToChar());
  if (*decline >= *maximum)
    *decline = *maximum >> 1;
  if (ini.ReadEntry(name, ScString("PercentageVariationInLongevity"), &value))
    *variation = f32(f64(GeParseNumber(value.ToChar())) * 0.01);
}

u8 BdPowerPlant::StaticInit() {
  GeIni ini;
  ScString path, value;
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SC3PowerLayer.INI", 22);
  ini.SetPath(path);
  ScGetSimutilApplication()->GetAppDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SetCompressedPath(path);
  ScString section("MiscPowerPlantTunables");
  if (ini.ReadEntry(section, ScString("MinOverworkMonths"), &value))
    BdMinimumOverworkMonths = GeParseNumber(value.ToChar());
  if (ini.ReadEntry(section, ScString("MaxOverworkMonths"), &value))
    BdMaximumOverworkMonths = GeParseNumber(value.ToChar());
  return 1;
}

// Initialize the module's application cache once, publishing the guard before querying IID4002.
// Failed queries retain their original null cache and are not retried.
ApGameInterface *ScGetSimutilApplication() {
  if (!ScSimutilApplicationInitialized) {
    ScSimutilApplicationInitialized = 1;
    ScGetSimutilFramework()->Application()->QueryInterface(IID_ApGameInterface, &ScSimutilApplication);
  }
  return static_cast<ApGameInterface *>(ScSimutilApplication);
}

// Borrow the current city from the cached application when that application exists.
CtCity *ScGetSimutilCity() {
  ApGameInterface *application = ScGetSimutilApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the application's occupant-attribute cache, or return null when the application is unavailable.
OcAttribCache *ScGetSimutilAttribCache() {
  ApGameInterface *application = ScGetSimutilApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Initialize plant counters and aging defaults, then set the construction date from the current city when available.
// The original starts with month/day zero and year1900 when there is no city.
BdPowerPlant::BdPowerPlant()
    : usage(0), output(0), capacity(0), overwork(0), constructionDate(), maximumAge(2), declineAge(1), retired(0) {
  u32 month = 0, day = 0, year = 1900;
  CtCity *city = ScGetSimutilCity();
  if (city)
    city->GetDate(&month, &day, &year);
  constructionDate.Set(month, day, year);
}

// Publish the power-plant primary for its aging and power identities, then retain through the live override.
// Unsupported identities clear the output.
bool BdPowerPlant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdMortal, BdPowerPlant);
  HANDLE_QUERY_INTERFACE(IID_BdPowerPlant, BdPowerPlant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

void BdPowerPlant::SetMaximumAge(u32 value) {
  maximumAge = value;
}

u32 BdPowerPlant::GetMaximumAge() {
  return maximumAge;
}

void BdPowerPlant::SetDeclineAge(u32 value) {
  declineAge = value;
}

u32 BdPowerPlant::GetDeclineAge() {
  return declineAge;
}

u32 BdPowerPlant::GetAge() {
  u32 month, day, year;
  ScGetSimutilCity()->GetDate(&month, &day, &year);
  return 12 * year - 12 * constructionDate.Year() + month - constructionDate.Month();
}

// Check the live building's occupancy and decline age after reading the plant's current age.
// Release the queried building view before asking the plant for its decline threshold.
bool BdPowerPlant::IsAging() {
  u32 age = GetAge();
  void *selected = nullptr;
  if (!QueryInterface(IID_BdBuilding, &selected))
    return false;
  BdBuilding *building = static_cast<BdBuilding *>(selected);
  u32 occupancy = building->GetOccupancyState();
  building->Release();
  return occupancy == 1 && age >= GetDeclineAge();
}

void BdPowerPlant::Retire() {
  retired = 1;
}

u8 BdPowerPlant::GetRetired() {
  return retired;
}

u8 BdPowerPlant::DecideWhetherToExplode() {
  i32 threshold = BdPowerRandom.Range(i32(BdMinimumOverworkMonths), i32(BdMaximumOverworkMonths));
  if (threshold >= i32(overwork))
    return 0;
  Explode();
  return 1;
}

// Create the corresponding rubble building and copy the original occupant's cell position to its occupant view.
// The returned building retains the reference supplied by the building layer, including on partial query failure.
BdBuilding *BdPowerPlant::GetCorrespondingBuildingRubble() {
  void *selected;
  BdBuilding *rubble = nullptr;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return nullptr;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  occupant->Release();
  if (QueryInterface(IID_BdBuilding, &selected)) {
    BdBuilding *building = static_cast<BdBuilding *>(selected);
    GeAttrib *attributes = building->GetBaseAttribData();
    building->Release();
    void *created = nullptr;
    bool made = BdPowerBuildings->CreateBuilding(attributes, IID_BdBuilding, &created);
    rubble = static_cast<BdBuilding *>(created);
    if (made) {
      rubble->SetOccupancyState(8);
      if (rubble->QueryInterface(IID_ScOccupant, &selected)) {
        occupant = static_cast<ScOccupant *>(selected);
        occupant->SetLocationInCellCoords(x, z);
        occupant->Release();
      }
    }
  }
  return rubble;
}

u32 BdPowerPlant::GetInfoStringCount() {
  return 4;
}

u8 BdPowerPlant::GetPowerQueryRow(ScStringView *label, ScStringView *value, u32 index, u8 *warning) {
  u32 maximum = MaxCapacity(), actual = ActualCapacity(), used = Usage();
  if (index >= GetInfoStringCount())
    return 0;
  ScLanguageUtility *utility = ScGetSimutilLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  ScString caption, pattern, number, second, formatted;
  if (index == 0 || index == 1) {
    caption = ScUtilityQueryText(index == 0 ? 408 : 86);
    label->Copy(caption);
    utility->MakeNumberString(i32(index == 0 ? maximum : actual), &number);
    pattern = ScUtilityQueryText(87);
    formatted.Sprintf(pattern.Data(), number.Data());
    value->Copy(formatted);
    if (warning)
      *warning = 0;
  } else if (index == 2) {
    if (warning)
      *warning = 0;
    caption = ScUtilityQueryText(88);
    label->Copy(caption);
    utility->MakeNumberString(i32(used), &number);
    pattern = ScUtilityQueryText(439);
    formatted.Sprintf(pattern.Data(), number.Data());
    value->Copy(formatted);
    if (warning)
      *warning = used >= actual;
  } else {
    caption = ScUtilityQueryText(kRsCityServiceAgeMaxText);
    label->Copy(caption);
    utility->MakeNumberString(GetAge() / 12, &number);
    utility->MakeNumberString(GetMaximumAge() / 12, &second);
    pattern = ScUtilityQueryText(kRsCityServiceYrsYrsFormatText);
    formatted.Sprintf(pattern.Data(), number.Data(), second.Data());
    value->Copy(formatted);
    if (warning)
      *warning = GetAge() >= GetMaximumAge();
  }
  return 1;
}

// Add the four power information rows before delegating the static building's generic query content.
u8 BdPowerBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  u8 warning = 0;
  if (query) {
    for (u32 index = 0; index < 3; ++index)
      if (GetPowerQueryRow(&label, &value, index, &warning))
        query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, warning);
    if (GetPowerQueryRow(&label, &value, 3, &warning))
      query->AddText(&label, &value, 8, warning);
  }
  return OcUtilityBuilding::QueryTile(message, query);
}

u8 BdPowerBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcUtilityBuilding::QueryDetail(message, query);
}

// Add the same power information rows for the animated building before the generic query content.
// The installed animated body is; the static building uses the same behavior.
u8 BdPowerBuildingAnimated::QueryTile(ScMessage *message, UiQuery *query) {
  ScString label, value;
  u8 warning = 0;
  if (query) {
    for (u32 index = 0; index < 3; ++index)
      if (GetPowerQueryRow(&label, &value, index, &warning))
        query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, warning);
    if (GetPowerQueryRow(&label, &value, 3, &warning))
      query->AddText(&label, &value, 8, warning);
  }
  return OcUtilityBuilding::QueryTile(message, query);
}

u8 BdPowerBuildingAnimated::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcUtilityBuilding::QueryDetail(message, query);
}

u8 BdWastePowerPlant::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcUtilityBuilding::QueryDetail(message, query);
}

void BdPowerPlant::BecomeDefunctDueToAge() {
  void *selected;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  BdBuilding *rubble = GetCorrespondingBuildingRubble();
  if (!rubble)
    return;
  GeDate date;
  GetBirthdate(&date);
  rubble->SetBirthdate(date);
  if (rubble->QueryInterface(IID_BdMortal, &selected)) {
    BdPowerPlant *power = static_cast<BdPowerPlant *>(selected);
    power->SetMaximumAge(GetMaximumAge());
    power->SetDeclineAge(GetDeclineAge());
    power->Retire();
    power->Release();
  }
  BdPowerOccupants->RemoveOccupant(occupant);
  BdPowerBuildings->InsertBuilding(rubble);
  rubble->Release();
  occupant->Release();
}

void BdPowerPlant::Explode() {
  void *selected;
  if (!QueryInterface(IID_ScOccupant, &selected))
    return;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  u32 type = occupant->TypeId();
  void *created = nullptr;
  if (!BdGetPowerCom()->GetClassObject(kSpExplosionViewClassId, IID_SpExplosionView, &created) || !created)
    return;
  SpExplosionView *fire = static_cast<SpExplosionView *>(created);
  ScGetSimutilApplication()->GetBoxX()->Event(kAuEventPlayBuildingSound, 0, 0, 0, 0);
  u8 altitude;
  BdPowerTerrain->GetVertexAltitude(x, z, &altitude);
  fire->Init(i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift),
      i32(u32(altitude) << kCtMicroCoordinateShift), 3, 0, 0);
  fire->Release();
  BdBuilding *rubble = GetCorrespondingBuildingRubble();
  BdPowerOccupants->RemoveOccupant(occupant);
  if (rubble) {
    BdPowerBuildings->InsertBuilding(rubble);
    rubble->Release();
  }
  occupant->Release();
  ScMessage message(kBdMsg_PowerPlantDestroyed, type, x, z);
  ScGetSimutilMessageServer()->MessagePost(message, false);
}

u32 BdPowerPlant::MaxCapacity() {
  return capacity;
}

u32 BdPowerPlant::ActualCapacity() {
  return output;
}

// Record the requested usage as and update the consecutive overwork counter.
void BdPowerPlant::SetUsage(u32 value) {
  usage = value;
  if (value <= output)
    overwork = 0;
  else
    ++overwork;
}

u32 BdPowerPlant::Usage() {
  return usage;
}

// Follow the original per-tick branch order.
// A retired plant recalculates capacity; a breakdown stops this update before age-based retirement.
void BdPowerPlant::Update() {
  if (GetRetired()) {
    DetermineActualCapacity();
    return;
  }
  if (DecideWhetherToExplode())
    return;
  if (GetAge() <= maximumAge)
    DetermineActualCapacity();
  else
    BecomeDefunctDueToAge();
}

BdPowerPlant::~BdPowerPlant() {
}

bool BdPowerPlant::HasBirthdate() const {
  return true;
}

bool BdPowerPlant::GetBirthdate(CtDateInterface *out) const {
  out->Set(constructionDate.serial);
  return true;
}

bool BdPowerPlant::SetBirthdate(const CtDateInterface &value) {
  constructionDate.serial = value.DayNumber();
  return true;
}

// Read the four original plant fields from the persistent descriptor.
// The first failed read still stores zero in overwork; later failures leave the remaining fields untouched.
bool BdPowerPlant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return 0;
  u32 value = 0;
  u8 result = info->GetIntData(kBdPowerPlantOverworkKey, &value);
  overwork = u16(value);
  if (result) {
    result = info->GetIntData(kBdPowerPlantMaximumAgeKey, &maximumAge);
    if (result) {
      result = info->GetIntData(kBdPowerPlantDeclineAgeKey, &declineAge);
      if (result) {
        value = 0;
        result = info->GetIntData(kBdPowerPlantRetiredKey, &value);
        if (result)
          retired = value != 0;
      }
    }
  }
  return result != 0;
}

// Write the original plant fields in descriptor order.
// Stop on the first refused write and report the success required by the occupant persistence contract.
bool BdPowerPlant::Save(CtCity *, OcPersistInfo *info) {
  u8 result = info->SetIntData(kBdPowerPlantOverworkKey, overwork);
  if (result) {
    result = info->SetIntData(kBdPowerPlantMaximumAgeKey, maximumAge);
    if (result) {
      result = info->SetIntData(kBdPowerPlantDeclineAgeKey, declineAge);
      if (result)
        result = info->SetIntData(kBdPowerPlantRetiredKey, retired);
    }
  }
  return result != 0;
}

void BdPowerPlant::DetermineActualCapacity() {
  output = CalculateCapacityBasedOnAge(capacity);
}

// Preserve full capacity through the decline age, then reduce it linearly to zero at maximum age.
// The multiplication uses the original wrapped 32-bit unsigned result before division.
u32 BdPowerPlant::CalculateCapacityBasedOnAge(u32 value) {
  u32 age = GetAge();
  if (age >= maximumAge)
    return 0;
  if (age > declineAge)
    return value - (value * (age - declineAge)) / (maximumAge - declineAge);
  return value;
}

// Set the two lifespan fields, using one shared uniform variation when requested.
// Preserve the original final correction when decline is not less than the resulting maximum.
void BdPowerPlant::DecideLifeSpanAndDeclineAge(u32 maximum, u32 decline, f32 variation) {
  if (variation == 0.0f) {
    maximumAge = maximum;
    declineAge = decline;
  } else {
    f64 factor = BdPowerRandom.RandomDoubleRangeUniform(1.0 - f64(variation), 1.0 + f64(variation));
    maximumAge = u32(i64(f64(maximum) * factor));
    declineAge = u32(i64(f64(decline) * factor));
  }
  if (declineAge >= maximumAge)
    maximumAge = declineAge + 1;
}

BdPowerBuilding::BdPowerBuilding() {
}

BdPowerBuilding::~BdPowerBuilding() {
}

bool BdPowerBuilding::QueryInterface(u32 iid, void **out) {
  return BdPowerPlant::QueryInterface(iid, out) || OcUtilityBuilding::QueryInterface(iid, out);
}

u32 BdPowerBuilding::AddRef() {
  return OcUtilityBuilding::AddRef();
}

u32 BdPowerBuilding::Release() {
  return OcUtilityBuilding::Release();
}

// Bind the generic building attributes and refresh plant output after successful initialization.
bool BdPowerBuilding::Init(GeAttrib *value) {
  if (!OcUtilityBuilding::Init(value))
    return false;
  DetermineActualCapacity();
  return true;
}

// Restore the building and plant descriptor fields in order, then recompute the plant's live output.
bool BdPowerBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!OcUtilityBuilding::Init(city, info) || !BdPowerPlant::Init(city, info))
    return false;
  DetermineActualCapacity();
  return true;
}

bool BdPowerBuilding::Save(CtCity *city, OcPersistInfo *info) {
  return OcUtilityBuilding::Save(city, info) && BdPowerPlant::Save(city, info);
}

u32 BdPowerBuilding::GetGeomFlag() {
  return kBdPowerBuildingGeometry;
}

u8 BdPowerBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 text = 0;
  if (GetOccupancyState() == 1 && IsAging())
    text = 406;
  else if (GetRetired())
    text = 407;
  if (text) {
    ScString status = ScUtilityQueryText(text);
    query->SetStatusText(status);
  }
  return 1;
}

// Borrow the occupant view embedded within the utility building.
ScOccupant *BdPowerBuilding::AsISC3Occupant() {
  return static_cast<OcUtilityOccupant *>(this);
}

// Borrow the adjusted utility-building primary without another reference.
BdBuilding *BdPowerBuilding::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the adjusted city-agent receiver without another reference.
CtAgent *BdPowerBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool BdPowerBuilding::HasBirthdate() const {
  return BdPowerPlant::HasBirthdate();
}

bool BdPowerBuilding::GetBirthdate(CtDateInterface *out) const {
  return BdPowerPlant::GetBirthdate(out);
}

bool BdPowerBuilding::SetBirthdate(const CtDateInterface &value) {
  return BdPowerPlant::SetBirthdate(value);
}

BdPowerBuildingAnimated::BdPowerBuildingAnimated() {
}

BdPowerBuildingAnimated::~BdPowerBuildingAnimated() {
}

bool BdPowerBuildingAnimated::QueryInterface(u32 iid, void **out) {
  return BdPowerPlant::QueryInterface(iid, out) || OcUtilityBuilding::QueryInterface(iid, out);
}

u32 BdPowerBuildingAnimated::AddRef() {
  return OcUtilityBuilding::AddRef();
}

u32 BdPowerBuildingAnimated::Release() {
  return OcUtilityBuilding::Release();
}

// Bind the animated building attributes and refresh plant output after successful initialization.
bool BdPowerBuildingAnimated::Init(GeAttrib *value) {
  if (!OcUtilityBuilding::Init(value))
    return false;
  DetermineActualCapacity();
  return true;
}

// Restore the animated building and plant descriptor fields, then refresh the plant's live output.
bool BdPowerBuildingAnimated::Init(CtCity *city, OcPersistInfo *info) {
  if (!OcUtilityBuilding::Init(city, info) || !BdPowerPlant::Init(city, info))
    return false;
  DetermineActualCapacity();
  return true;
}

bool BdPowerBuildingAnimated::Save(CtCity *city, OcPersistInfo *info) {
  return OcUtilityBuilding::Save(city, info) && BdPowerPlant::Save(city, info);
}

u32 BdPowerBuildingAnimated::GetGeomFlag() {
  return kBdPowerBuildingGeometry;
}

u8 BdPowerBuildingAnimated::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  OcUtilityBuilding::QueryBasic(message, query);
  u32 text = 0;
  if (GetOccupancyState() == 1 && IsAging())
    text = 406;
  else if (GetRetired())
    text = 407;
  if (text) {
    ScString status = ScUtilityQueryText(text);
    query->SetStatusText(status);
  }
  return 1;
}

// Borrow the occupant view embedded within the animated utility building.
ScOccupant *BdPowerBuildingAnimated::AsISC3Occupant() {
  return static_cast<OcUtilityOccupant *>(this);
}

// Borrow the animated utility-building primary without another reference.
BdBuilding *BdPowerBuildingAnimated::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the adjusted city-agent receiver without another reference.
CtAgent *BdPowerBuildingAnimated::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool BdPowerBuildingAnimated::HasBirthdate() const {
  return BdPowerPlant::HasBirthdate();
}

bool BdPowerBuildingAnimated::GetBirthdate(CtDateInterface *out) const {
  return BdPowerPlant::GetBirthdate(out);
}

bool BdPowerBuildingAnimated::SetBirthdate(const CtDateInterface &value) {
  return BdPowerPlant::SetBirthdate(value);
}

void BdCoalPlant::LoadTunableParameters() {
  BdReadPlantTuning("CoalPowerPlant", &BdCoalMaximum, &BdCoalDecline, &BdCoalVariation);
  BdCoalLoaded = 1;
}

// Initialize coal tuning and the live city capacity before applying the shared lifespan variation.
BdCoalPlant::BdCoalPlant() {
  if (!BdCoalLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12111, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdCoalMaximum, BdCoalDecline, BdCoalVariation);
}

BdCoalPlant::~BdCoalPlant() {
}

void BdFusionPlant::LoadTunableParameters() {
  BdReadPlantTuning("FusionPowerPlant", &BdFusionMaximum, &BdFusionDecline, &BdFusionVariation);
  BdFusionLoaded = 1;
}

// Initialize fusion tuning and the live city capacity before applying the shared lifespan variation.
BdFusionPlant::BdFusionPlant() {
  if (!BdFusionLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12116, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdFusionMaximum, BdFusionDecline, BdFusionVariation);
}

BdFusionPlant::~BdFusionPlant() {
}

void BdGasPlant::LoadTunableParameters() {
  BdReadPlantTuning("GasPowerPlant", &BdGasMaximum, &BdGasDecline, &BdGasVariation);
  BdGasLoaded = 1;
}

// Initialize gas tuning and the live city capacity before applying the shared lifespan variation.
BdGasPlant::BdGasPlant() {
  if (!BdGasLoaded)
    LoadTunableParameters();
  CtPowerLayer *power = ScGetSimutilCity()->PowerLayer();
  if (power)
    power->GetPlantCapacity(12113, &capacity);
  BdPowerPlant::DecideLifeSpanAndDeclineAge(BdGasMaximum, BdGasDecline, BdGasVariation);
}

BdGasPlant::~BdGasPlant() {
}
