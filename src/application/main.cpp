#define WIN32_LEAN_AND_MEAN
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "application/application_core.h"
#include "common/api.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_error.h"

// Construct the executable's process-owned application with its original application name.
static ApBaseApp *ApCreateApplication() {
  return new ApApplication("SC3U.exe");
}

static void ApDestroyApplication(ApBaseApp *application) {
  delete application;
}

// Install allocation failure handling before later game globals allocate, then register the original callbacks.
// This entry source comes first in the single executable unit; no unloadable module installs the handler.
static const bool ApCallbacksRegistered = (ScInstallNewHandler(), ApApplicationFactory = ApCreateApplication,
    ApApplicationDestroy = ApDestroyApplication, true);

static bool ApIsDirectory(const ScString &path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.Data(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

// Accept an installation root or its Apps folder; the executable's data lives in a Sys directory beside SC3U.exe.
// On success the executable-relative Apps directory is returned through apps.
static bool ApFindInstalledGame(const char *root, ScString *apps) {
  if (!root || !*root)
    return false;
  const char *suffixes[] = {"/Apps", ""};
  for (const char *suffix : suffixes) {
    ScString candidate;
    candidate.Sprintf("%s%s", root, suffix);
    ScString sys;
    sys.Sprintf("%s/Sys", candidate.Data());
    if (ApIsDirectory(candidate) && ApIsDirectory(sys)) {
      *apps = candidate;
      return true;
    }
  }
  return false;
}

// Collect the folder chosen in the native dialog, leaving empty on cancel or when no dialog service exists.
struct ApFolderChoice {
  bool done = false;
  ScString path;
};

static void SDLCALL ApFolderChosen(void *context, const char *const *files, int) {
  ApFolderChoice *choice = static_cast<ApFolderChoice *>(context);
  if (files && files[0])
    choice->path = ScString(files[0]);
  choice->done = true;
}

// Ask for the installation folder with the platform's folder dialog, falling back to a terminal prompt.
static ScString ApAskForGameFolder(const char *message) {
  ApFolderChoice choice;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SimCity 3000", message, nullptr);
  SDL_ShowOpenFolderDialog(ApFolderChosen, &choice, nullptr, nullptr, false);
  while (!choice.done) {
    SDL_PumpEvents();
    SDL_Delay(20);
  }
  if (choice.path.Strlen())
    return choice.path;
  char line[1024];
  fprintf(stderr, "%s\nInstallation folder (blank to quit): ", message);
  if (fgets(line, sizeof(line), stdin)) {
    usize length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
      line[--length] = 0;
    return ScString(line);
  }
  return ScString();
}

// The executable-relative lookups follow SC3_APP_DIR, so the build can run from any directory.
static bool ApSelectGameDirectory() {
  ScString apps;
  const char *environment = SDL_getenv("SC3_GAME_DIR");
  const char *preferences = SDL_GetPrefPath("SC3U", "SC3U");
  ScString saved_file;
  if (preferences)
    saved_file.Sprintf("%sgame_dir.txt", preferences);
  ScString root;
  if (environment && ApFindInstalledGame(environment, &apps))
    root = ScString(environment);
  else if (preferences) {
    usize size = 0;
    char *data = static_cast<char *>(SDL_LoadFile(saved_file.Data(), &size));
    if (data) {
      ScString candidate(data, u32(size));
      SDL_free(data);
      if (ApFindInstalledGame(candidate.Data(), &apps))
        root = candidate;
    }
  }
  for (int attempt = 0; !root.Strlen() && attempt < 3; ++attempt) {
    ScString chosen = ApAskForGameFolder(attempt ? "That folder does not contain SimCity 3000 (no Sys folder). "
                                                     "Choose the folder where the game is installed."
                                                   : "Choose the folder where SimCity 3000 Unlimited is installed.");
    if (!chosen.Strlen())
      return false;
    if (ApFindInstalledGame(chosen.Data(), &apps)) {
      root = chosen;
      if (preferences)
        SDL_SaveFile(saved_file.Data(), root.Data(), root.Strlen());
    }
  }
  if (!root.Strlen())
    return false;
  SDL_setenv_unsafe("SC3_GAME_DIR", root.Data(), 1);
  SDL_setenv_unsafe("SC3_APP_DIR", apps.Data(), 1);
  return true;
}

// Start SDL on the main thread and keep the original framework and command-line owners alive until shutdown.
int main(int argc, char **argv) {
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO))
    return EXIT_FAILURE;
  if (!ApSelectGameDirectory()) {
    SDL_Quit();
    return EXIT_FAILURE;
  }
  ScCommandLine arguments("");
  for (int index = argc - 1; index >= 0; --index) {
    ScString value(argv[index]);
    arguments.InsertArgument(value, 0);
  }
  int result = ScFrameworkCore::Main(&arguments, false);
  SDL_Quit();
  return result;
}

// Expose the real executable director to the original C loader entry without acquiring another reference.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return ApGetDirector();
}
