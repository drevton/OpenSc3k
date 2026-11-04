#include <stdio.h>
#include <stdlib.h>

#include <new>

#include "common/sc3_error.h"
#include <SDL3/SDL.h>

static thread_local ScFatalHandler ScErrorHandler;

static void ScNewFailure() {
  ScFatal(SC_ERROR_NOMEM, "Out of memory.");
}

void ScInstallNewHandler() {
  std::set_new_handler(ScNewFailure);
}

// Replace this thread's fatal-error display hook. Return the previous hook so callers can restore it.
ScFatalHandler ScSetFatalHandler(ScFatalHandler handler) {
  ScFatalHandler previous = ScErrorHandler;
  ScErrorHandler = handler;
  return previous;
}

// Report one unrecoverable error and terminate. Test hooks must escape or terminate themselves.
// Stop re-entrant failures immediately so the modal message pump cannot open cascading game-looking error windows.
SC_NORETURN void ScFatal(ScError error, const char *message) {
  if (!message)
    message = "Fatal game error.";
  if (ScErrorHandler)
    ScErrorHandler(error, message);
  fprintf(stderr, "SimCity 3000: %s\n", message);
  fflush(stderr);
  static SDL_AtomicInt dialog_active;
  if (!SDL_CompareAndSwapAtomicInt(&dialog_active, 0, 1))
    _Exit(EXIT_FAILURE);
  if (SDL_IsMainThread())
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SimCity 3000", message, nullptr);
  // Partially initialized game singletons may not support shutdown after a fatal error.
  _Exit(EXIT_FAILURE);
}

SC_NORETURN void ScNotImpl(const char *message) {
  ScFatal(SC_ERROR_NOT_IMPLEMENTED, message ? message : "Unimplemented reconstructed game code.");
}
