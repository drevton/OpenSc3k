#ifndef SC3_APPLICATION_LOCAL_ALBUM_H
#define SC3_APPLICATION_LOCAL_ALBUM_H

#include <map>
#include <vector>

#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "network/api.h"
#include "resources/file.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_window.h"
#include "window/window.h"

class RsIndexedFile;
class NwTransferObject;

// Executable cSC3Internet, with its separate message-target identity and original owned collections.
// Unrecovered networking, preference and message operations remain inherited pure virtual methods.
class NwInternetObject : public NwInternet, public ScMessageTarget {
public:
  NwInternetObject();
  virtual ~NwInternetObject();
  bool Init() override;
  bool Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  ConnectionState GetConnectionState() override;
  bool IsConnected() override;
  bool ConnectionIsDialup() override;
  bool Connect() override;
  bool Disconnect() override;
  bool ReadPreferences() override;
  bool WritePreferences() override;
  bool GetMainServerURL(ScStringView *out) override;
  bool SetMainServerURL(const ScStringView &value) override;
  void SetParentWindow(WnWindow *window) override;
  NwChatSystem *GetChatSystem() override;
  NwTransferSystem *GetTransferSystem() override;
  NwInternetPrefs *GetInternetPrefs() override;
  bool GetInternetPrefsFilePath(ScStringView *out) override;
  bool SetInternetPrefs(NwInternetPrefs *prefs) override;
  void DoIdleProcessing() override;
  bool DoUserStringCommand(const ScStringView &command) override;
  bool ViewExternalBrowser(const ScStringView &url) override;
  bool ViewInternalBrowser(const ScStringView &url, const ScString *caption, bool show_when_loaded) override;
  bool IsExternalBrowserAvailable() override;
  bool GetCityExchangeUserInfo(ScStringView *first, ScStringView *second, ScStringView *third) override;
  bool GetCityExchangeURL(ScStringView *out) override;
  bool RegisterURLMessage(const ScStringView &key, const ScStringView &url, bool flag) override;
  bool UnregisterURLMessage(const ScStringView &key) override;
  bool ReadPreferences(RsIndexedFile *file);
  bool WritePreferences(RsIndexedFile *file);
  bool ReadPreferenceOverrides();
  u32 references;
  bool initialized;
  WnWindow *parent;
  NwTransferSystem *transfer;
  NwInternetPrefs preferences;
};

bool NwReadInternetPrefRecord(RsIndexedFile *file, u32 key, char **bytes, u32 *size, bool allocate);
bool NwWriteInternetPrefRecord(RsIndexedFile *file, u32 key, const char *bytes, u32 size);
bool NwSaveLocalPreferences(const ScStringView &path, NwInternetPrefs *preferences, NwTransferObject *transfer);

class RsIndexedFile;
class BdImageIoInterface;
class WnAppListControl;
class WnAppEditorControl;
class WnButton;
class FtFont;

bool NwLoadWindowSettingsFromPrefs(WnWindow *window, i32 index);
bool NwSaveWindowSettingsToPrefs(WnWindow *window, i32 index);

// SC3U's bitmap window has its own application-window base and the real secondary bitmap interface.
class WnAppBitmap : public UiWindow, public WnBitmapControl {
public:
  WnAppBitmap();
  ~WnAppBitmap() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  void SetImage(GxBuffer *image) override;
  void SetAlpha(bool enabled, f32 opacity) override;
  bool GetBitmapFlag(i32 mask) const override;
  void SetBitmapFlag(i32 mask, bool enabled) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using UiWindow::SetArea;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  void SetSourceBufferAreaFromWindowArea(const ScRect &rectangle);
  GxBuffer *image;
  bool alpha_enabled;
  f32 opacity;
  ScRect source_area;
  u32 bitmap_flags;
};

// Original cIWinBMPView begins directly with SetAlignment and has no ScUnknown prefix.
class WnBitmapView {
public:
  enum class Alignment : i32;
  virtual void SetAlignment(Alignment alignment) = 0;
  virtual bool SetZoom(f32 value) = 0;
  virtual bool ZoomIn(f32 factor) = 0;
  virtual bool ZoomOut(f32 factor) = 0;
  virtual bool SetZoomRangeMinMax(f32 minimum, f32 maximum) = 0;
  virtual void SetShouldFitImageInView(bool enabled) = 0;
  virtual void EnableZooming(bool enabled) = 0;
  virtual bool SetOrigin(i32 x, i32 y) = 0;
  virtual bool CenterOnImagePixel(i32 x, i32 y) = 0;
  virtual bool CenterOnWindowPixel(i32 x, i32 y) = 0;
  virtual bool WindowPixelToImagePixel(i32 x, i32 y, i32 *image_x, i32 *image_y) = 0;
  virtual bool Scroll(i32 x, i32 y) = 0;
  virtual void SetMinMsPerScroll(u32 milliseconds) = 0;
  virtual void EnableScrolling(bool enabled) = 0;
  virtual void ClearBMP() = 0;
  virtual void GetBMP(GxBuffer **out) = 0;
  virtual void SetBMP(GxBuffer *buffer) = 0;
  virtual void FreeMemory(void *memory) = 0;
  virtual u32 GetImageWidth() = 0;
  virtual u32 GetImageHeight() = 0;
  virtual bool SetBMPFromFile(const ScStringView &path) = 0;
  virtual bool WriteBMPToFile(u32 quality, ScStringView *path, u32 flags, const ScString &data) = 0;
  virtual bool SetBMPFromMemory(void *data, u32 size) = 0;
  virtual bool WriteBMPToMemory(u32 *size, void **out, u32 *written, u32 quality, const ScString &data) = 0;
};

// SC3U's bitmap viewer adds its independent zoom, origin and image-loading interface to the bitmap window.
class WnAppBitmapView : public WnAppBitmap, public WnBitmapView {
public:
  WnAppBitmapView();
  ~WnAppBitmapView() override;
  bool QueryInterface(u32 iid, void **out) override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnAppBitmap::SetArea;
  void SetImage(GxBuffer *image) override;
  void SetAlignment(Alignment alignment) override;
  bool SetZoom(f32 value) override;
  bool SetOrigin(i32 x, i32 y) override;
  bool CenterOnImagePixel(i32 x, i32 y) override;
  bool CenterOnWindowPixel(i32 x, i32 y) override;
  bool WindowPixelToImagePixel(i32 x, i32 y, i32 *image_x, i32 *image_y) override;
  bool Scroll(i32 x, i32 y) override;
  void SetShouldFitImageInView(bool enabled) override;
  void EnableZooming(bool enabled) override;
  void EnableScrolling(bool enabled) override;
  void SetMinMsPerScroll(u32 milliseconds) override;
  void GetBMP(GxBuffer **out) override;
  u32 GetImageWidth() override;
  u32 GetImageHeight() override;
  bool SetZoomRangeMinMax(f32 minimum, f32 maximum) override;
  bool ZoomIn(f32 factor) override;
  bool ZoomOut(f32 factor) override;
  void ClearBMP() override;
  void SetBMP(GxBuffer *buffer) override;
  void FreeMemory(void *memory) override;
  bool SetBMPFromFile(const ScStringView &path) override;
  bool WriteBMPToFile(u32 format, ScStringView *path, u32 quality, const ScString &data) override;
  bool SetBMPFromMemory(void *data, u32 size) override;
  bool WriteBMPToMemory(u32 *format, void **out, u32 *size, u32 quality, const ScString &data) override;
  static bool SetBMPFromFile(GxBuffer **out, const ScStringView &path);
  static bool SetBMPFromMemory(GxBuffer **out, void *data, u32 size);
  static bool WriteBMPToFile(const GxBuffer *buffer, u32 format, ScStringView *path, u32 quality,
      const ScString &data);
  static bool WriteBMPToMemory(const GxBuffer *buffer, u32 *format, void **out, u32 *size, u32 quality,
      const ScString &data);
  static void FreeMemoryStatic(void *memory);
  static bool GetCustomDataFromFile(const ScStringView &path, void **data, u32 *size);
  static bool SetCustomDataInFile(const ScStringView &path, const void *data, u32 size);

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  void CalculateAllMetrics();
  static BdImageIoInterface *image_factory;
  bool zoom_enabled, scrolling_enabled, fit_image;
  f32 minimum_zoom, maximum_zoom;
  u32 field216;
  Alignment alignment;
  f32 zoom;
  ScRect image_area;
  i32 origin_x, origin_y, mouse_x, mouse_y;
  u32 last_scroll_time, min_scroll_time;
};

// The viewer preview adds the two window sizes and a timer for double-click handling.
class WnAppBitmapViewExtra : public WnAppBitmapView {
public:
  WnAppBitmapViewExtra();
  ~WnAppBitmapViewExtra() override;
  bool Init() override;
  bool DoMessage(ScMessage *message) override;
  void SetSizeInAndOutAreas(const ScRect &size_in, const ScRect &size_out);
  void SetShouldFitImageAtSizeIn(bool enabled);
  bool SetSizeInOut(bool size_in);

protected:
  friend class NwGraphicsViewerObject;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool fit_at_size_in, size_in;
  ScRect size_in_area, size_out_area;
  ScStopwatch double_click_timer;
};

// SC3U's viewer has distinct window-procedure and graphics-viewer identities. Unrecovered methods remain abstract.
class NwGraphicsViewerObject : public UiWindowCallback, public NwGraphicsViewer {
public:
  using NwGraphicsViewer::GoToAlbum;
  using NwGraphicsViewer::DeleteAlbum;
  NwGraphicsViewerObject();
  virtual ~NwGraphicsViewerObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateGraphicsViewerWindow(WnWindow **window) override;
  bool DestroyGraphicsViewerWindow() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  WnWindow *GetIGZWin() override;
  void GetViewerBaseDirectoryPath(ScStringView *out) override;
  bool SetViewerBaseDirectoryPath(const ScStringView &path) override;
  void GetViewerTransferDirectoryName(ScStringView *out) override;
  void GetViewerTransferDirectoryPath(ScStringView *out) override;
  void GetCurrentViewerDirectoryName(ScStringView *out) override;
  bool SetCurrentViewerDirectoryName(const ScStringView &name) override;
  bool GoToAlbumForCurrentCity(bool create) override;
  bool GoToAlbum(u32 index) override;
  bool GoToAlbum(const ScString &name) override;
  bool GoToAlbum(const ScString &name, u32 index, bool create) override;
  bool GoToTransferAlbum() override;
  bool AlbumExists(const ScString &name, u32 index) override;
  bool CreateAlbum(const ScString &name, u32 index) override;
  bool DeleteAlbum(const ScString &name) override;
  bool DeleteAlbum(u32 index) override;
  bool AddNewTransferGraphic(const ScString &path, bool remove_source) override;
  bool AddNewGraphic(const ScString &path, const ScString &information, const ScString &description,
      const ScString &city, u32 identifier, bool remove_source) override;
  bool MakeSureCurrentAlbumDirectoryExists(bool reset) override;
  void SetIdleProcessingInterval(u32 interval) override;
  void DoIdleProcessing() override;
  bool GetNewTempFileNameForCurrentAlbum(ScString *out) override;
  bool GetNewTempFileName(const ScString &directory, ScString *out) override;
  bool GetInformationOfFile(const ScString &path, ScString *out) override;
  bool GetInformationOfTransferFile(const ScString &path, ScString *out) override;
  bool GetInformationOfStandardFile(const ScString &path, ScString *out) override;
  bool GetDescriptionOfFile(const ScString &path, ScString *out) override;
  bool SaveInformationOfStandardFile(const ScString &path, const ScString &information) override;
  bool SaveDescriptionOfFile(const ScString &path, const ScString &description) override;
  bool SaveDescriptionOfFile() override;
  bool GoToGraphic(const ScString &name) override;
  bool GoToNextGraphic() override;
  bool GoToPreviousGraphic() override;
  bool DeleteCurrentGraphic() override;
  bool SetInitialFile() override;
  bool SetCurrentFile(const ScString &name, bool refresh) override;
  bool RefreshGraphicList(bool *changed) override;
  bool CityNameToAlbumName(const ScString &city, ScString *out);
  static bool EncryptData(const ScString &source, ScString *out);
  static bool DecryptData(const ScString &source, ScString *out);

protected:
  bool LoadWindowSettingsFromPrefs(WnWindow *window);
  bool SaveWindowSettingsToPrefs(WnWindow *window);
  bool OnGetMinMaxInfo(WnWindow *window, ScRect *bounds);
  bool OnUserAreaChanged(WnWindow *window, const ScRect *area);
  bool OnShutdown(WnWindow *window);
  bool OnInit(WnWindow *window);
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  void DisplayErrorMessage(const ScString &message, const ScString &title);
  bool EndDialog(WnWindow *window, u32 result);
  bool AlbumDirectoryHasAlbumEntries(const ScString &directory, bool require_images);
  u32 UpdateListOfAlbumDirectories(bool *changed, bool flag);
  void SetImageCountText(u32 current, u32 count);
  void SetImageCountText();
  bool DebugOutputImageFileList();
  void SetToNoImageFile(bool display);
  bool ConvertAlbumDirectoryNameToAlbumName(const ScString &directory, ScString *out);
  bool ConvertAlbumIndexToAlbumDirectoryName(u32 index, ScString *out);
  bool ConvertAlbumDirectoryNameToAlbumIndex(const ScString &directory, u32 *out);
  bool IsCurrentAlbumDirectoryTransferDirectory();
  static void GetPathForInfoFile(const ScString &path, ScString *out);
  static void GetPathForDescriptionFile(const ScString &path, ScString *out);
  u32 references = 0;
  bool viewer_initialized = false;
  WnWindow *window = nullptr;
  GxBuffer *no_image = nullptr;
  GxBuffer *field24 = nullptr, *field28 = nullptr;
  WnAppBitmapView *field32 = nullptr;
  WnAppBitmapViewExtra *preview = nullptr;
  WnAppListControl *album_list = nullptr;
  WnLabelControl *file_label = nullptr;
  WnLabelControl *image_count_label = nullptr;
  WnAppEditorControl *description_editor = nullptr;
  WnButton *field56 = nullptr;
  WnButton *field60 = nullptr;
  bool can_navigate = false, ensure_transfer_directory = true, include_transfer_album = false;
  ScString base_directory;
  std::vector<ScString> album_directories;
  ScString transfer_directory, current_directory, current_file, current_path, last_description;
  std::list<ScString> graphic_files;
  u32 last_idle, idle_interval;
  std::vector<ScString> image_extensions;
};

// SC3U's snapshot window extends the application UI base and owns its selected size and retained interfaces.
class ApSnapshotTaker : public UiWindow {
public:
  ApSnapshotTaker();
  ~ApSnapshotTaker() override;
  static bool CreateWinSnapshotTaker(WnWindow *parent, u32 kind);
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool GZOnCharacter(char character) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  void RemoveSelf();
  bool CycleSize();
  bool GoToSizeIndex(u32 index);
  bool SaveWindowSettingsToPrefs();
  bool LoadWindowSettingsFromPrefs();
  bool TakeSnapshot(bool flag);
  bool AddNewGraphicForCurrentCity(NwGraphicsViewer *viewer, const ScString &path, bool flag);
  static bool GetGraphicInfoForCurrentCity(ScString *information);
  bool AddWatermarkToBuffer(GxBuffer *buffer);
  static bool GetGraphicDescriptionForCurrentCity(ScString *description);

protected:
  static std::vector<ScSize> sizes;
  static std::vector<ScString> help_text;
  static u32 max_text_width;
  bool snapshot_initialized;
  GxBuffer *field168;
  FtFont *field172;
  u32 size_index;
  GxBuffer *field180;
};

// Executable cSC3TransferSystem; browser, screenshot, and transfer operations remain abstract.
class NwTransferObject : public NwTransferSystem, public ScMessageTarget {
public:
  NwTransferObject();
  virtual ~NwTransferObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool TakeSnapshot(u32 kind) override;
  bool DoUserStringCommand(const ScStringView &command, NwInternetCommand type,
      const NwChatStrings &arguments) override;
  bool Init() override;
  bool Shutdown() override;
  void SetSC3Internet(NwInternet *internet) override;
  void SetParentWindow(WnWindow *window) override;
  bool GetDownloadDirectory(ScStringView *out) override;
  bool GetNameOfScreen(TransferType type, ScStringView *out) override;
  bool SetDownloadDirectory(const ScStringView &directory, bool create) override;
  bool CopyFileIntoViewerDirectory(const ScStringView &file) override;
  bool CopyFileFromViewerDirectoryToAnotherDirectory(const ScStringView &file, const ScStringView &directory) override;
  bool ProcessSendRecvRequest(bool send, TransferType type, const ScStringView &user, const ScStringView &filename,
      u32 address, u32 port, u32 size) override;
  bool MakeSureDownloadDirectoryExists(bool reset) override;
  u32 GetGraphicsQuality0to100() override;
  void SetGraphicsQuality0to100(u32 quality) override;
  void AbortAllTransfers() override;
  GxBuffer *GetScreen(TransferType type, bool capture) override;
  bool IsImageBrowserShown() override;
  bool ShowImageBrowser(bool show, bool transfer, ScStringView *name, bool visible) override;
  NwGraphicsViewer *GetImageBrowser() override;
  void DoIdleProcessing() override;
  bool ReadPreferences(RsIndexedFile *file);
  bool WritePreferences(RsIndexedFile *file);
  void CleanUpUnfinishedTransfers();
  void CleanUpTempFiles();
  u32 references;
  bool initialized;
  u32 quality, field24;
  NwInternet *internet;
  ScString download_directory;
  WnWindow *parent;
  NwGraphicsViewerObject *viewer;
  u32 field60;
};

#endif
