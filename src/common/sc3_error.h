#ifndef SC3_COMMON_SC3_ERROR_H
#define SC3_COMMON_SC3_ERROR_H

#include "common/sc3.h"

#if defined(_MSC_VER)
#define SC_NORETURN __declspec(noreturn)
#elif defined(__cplusplus)
#define SC_NORETURN __attribute__((noreturn))
#else
#define SC_NORETURN _Noreturn
#endif

typedef enum ScError {
  SC_ERROR_INVALID = 1,
  SC_ERROR_TRUNCATED,
  SC_ERROR_NOMEM,
  SC_ERROR_IO,
  SC_ERROR_NOT_FOUND,
  SC_ERROR_UNSUPPORTED,
  SC_ERROR_NOT_IMPLEMENTED
} ScError;

typedef void (*ScFatalHandler)(ScError error, const char *message);

// The executable installs this before creating game objects; DLLs do not own the process-wide allocation callback.
SC3_API void ScInstallNewHandler(void);

// An installed handler must not return. NULL restores the default error display and process exit.
SC3_API ScFatalHandler ScSetFatalHandler(ScFatalHandler handler);
SC3_API SC_NORETURN void ScFatal(ScError error, const char *message);

// Stop at a missing reconstruction explicitly; this is not an original game error or a recoverable load result.
SC3_API SC_NORETURN void ScNotImpl(const char *message);

#endif
