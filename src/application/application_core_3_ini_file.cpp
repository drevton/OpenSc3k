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

// Capture the original CPU speed, rounded physical memory and their corresponding game performance grades.
// The original version helper supplies the processor and memory measurements before classification.
ApPerformanceInfo::ApPerformanceInfo() {
  ScVersion version;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  i32 processor = version.GetCPUVersion();
#endif
  processor_mhz = static_cast<i16>(version.GetCPUSpeed());
  if (processor_mhz < 100)
    processor_mhz = 180;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
  if (processor < 5)
    processor_power = 1;
  else if (processor == 5)
    processor_power = processor_mhz < 190 ? 2 : 3;
  else if (processor == 6)
    processor_power = processor_mhz < 260 ? 4 : 5;
  else if (processor > 9)
    processor_power = 7;
  else
    processor_power = processor_mhz < 300 ? 5 : 6;
#else
  // The x86 family classification is unavailable; retain the lowest original performance grade.
  processor_power = 1;
#endif
  u32 megabytes = 16, available = 16;
  version.GetSystemMemory(&megabytes, &available);
  memory_mb = static_cast<i16>(megabytes > INT16_MAX ? INT16_MAX : megabytes);
  memory_power = static_cast<i8>((log(static_cast<f64>(megabytes)) + 0.1) / log(2.0));
}

#if defined(_WIN32)
#pragma warning(push)
#pragma warning(disable : 4996)

LPTOP_LEVEL_EXCEPTION_FILTER ScExceptionHandler::previous_filter;
char ScExceptionHandler::log_filename[MAX_PATH], ScExceptionHandler::exception_buffer[512];
HANDLE ScExceptionHandler::report_file;
decltype(&::SymInitialize) ScExceptionHandler::sym_initialize;
decltype(&::SymCleanup) ScExceptionHandler::sym_cleanup;
#if defined(_WIN64)
decltype(&::StackWalk64) ScExceptionHandler::stack_walk;
decltype(&::SymFunctionTableAccess64) ScExceptionHandler::sym_function_table;
decltype(&::SymGetModuleBase64) ScExceptionHandler::sym_module_base;
decltype(&::SymGetSymFromAddr64) ScExceptionHandler::sym_get_symbol;
decltype(&::SymLoadModule64) ScExceptionHandler::sym_load_module;
#else
decltype(&::StackWalk) ScExceptionHandler::stack_walk;
decltype(&::SymFunctionTableAccess) ScExceptionHandler::sym_function_table;
decltype(&::SymGetModuleBase) ScExceptionHandler::sym_module_base;
decltype(&::SymGetSymFromAddr) ScExceptionHandler::sym_get_symbol;
decltype(&::SymLoadModule) ScExceptionHandler::sym_load_module;
#endif

static ScExceptionHandler *ApProcessExceptionHandler;

void ApInitializeExceptionHandler() {
  ApProcessExceptionHandler = new ScExceptionHandler;
}

void ApShutdownExceptionHandler() {
  delete ApProcessExceptionHandler;
  ApProcessExceptionHandler = nullptr;
}

// Install this process filter and derive the report filename by replacing the executable's final extension.
// The original ignores the filename query result and assumes the returned C string and appended suffix fit.
ScExceptionHandler::ScExceptionHandler() {
  previous_filter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
  GetModuleFileNameA(nullptr, log_filename, MAX_PATH);
  assert(memchr(log_filename, 0, sizeof(log_filename)));
  char *extension = strrchr(log_filename, '.');
  if (extension) {
    assert(static_cast<usize>(extension - log_filename) + sizeof("_stkdmp.txt") <= sizeof(log_filename));
    *extension = 0;
    strcat(extension, "_stkdmp.txt");
  }
}

ScExceptionHandler::~ScExceptionHandler() {
  SetUnhandledExceptionFilter(previous_filter);
}

void ScExceptionHandler::SetLogFileName(char *name) {
  assert(strlen(name) < sizeof(log_filename));
  strcpy(log_filename, name);
}

// Open or create the report, append a report and close its handle before instructing Windows to handle the exception.
// Preserve the original nonnull test, including its defined platform calls on INVALID_HANDLE_VALUE.
LONG WINAPI ScExceptionHandler::UnhandledExceptionFilter(EXCEPTION_POINTERS *exception) {
  report_file = CreateFileA(log_filename, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_WRITE_THROUGH, nullptr);
  if (report_file) {
    SetFilePointer(report_file, 0, nullptr, FILE_END);
    GenerateExceptionReport(exception);
    CloseHandle(report_file);
    report_file = nullptr;
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// Return the original exception-name literals, falling back to NTDLL's message table in one shared512-byte buffer.
// A failed message lookup preserves the buffer's previous contents as in the installed code.
char *ScExceptionHandler::GetExceptionString(u32 code) {
  switch (code) {
  case EXCEPTION_GUARD_PAGE:
    return const_cast<char *>("GUARD_PAGE");
  case EXCEPTION_DATATYPE_MISALIGNMENT:
    return const_cast<char *>("DATATYPE_MISALIGNMENT");
  case EXCEPTION_BREAKPOINT:
    return const_cast<char *>("BREAKPOINT");
  case EXCEPTION_SINGLE_STEP:
    return const_cast<char *>("SINGLE_STEP");
  case EXCEPTION_ACCESS_VIOLATION:
    return const_cast<char *>("ACCESS_VIOLATION");
  case EXCEPTION_IN_PAGE_ERROR:
    return const_cast<char *>("IN_PAGE_ERROR");
  case EXCEPTION_INVALID_HANDLE:
    return const_cast<char *>("INVALID_HANDLE");
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    return const_cast<char *>("ILLEGAL_INSTRUCTION");
  case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    return const_cast<char *>("NONCONTINUABLE_EXCEPTION");
  case EXCEPTION_INVALID_DISPOSITION:
    return const_cast<char *>("INVALID_DISPOSITION");
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    return const_cast<char *>("ARRAY_BOUNDS_EXCEEDED");
  case EXCEPTION_FLT_DENORMAL_OPERAND:
    return const_cast<char *>("FLT_DENORMAL_OPERAND");
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    return const_cast<char *>("FLT_DIVIDE_BY_ZERO");
  case EXCEPTION_FLT_INEXACT_RESULT:
    return const_cast<char *>("FLT_INEXACT_RESULT");
  case EXCEPTION_FLT_INVALID_OPERATION:
    return const_cast<char *>("FLT_INVALID_OPERATION");
  case EXCEPTION_FLT_OVERFLOW:
    return const_cast<char *>("FLT_OVERFLOW");
  case EXCEPTION_FLT_STACK_CHECK:
    return const_cast<char *>("FLT_STACK_CHECK");
  case EXCEPTION_FLT_UNDERFLOW:
    return const_cast<char *>("FLT_UNDERFLOW");
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    return const_cast<char *>("INT_DIVIDE_BY_ZERO");
  case EXCEPTION_INT_OVERFLOW:
    return const_cast<char *>("INT_OVERFLOW");
  case EXCEPTION_PRIV_INSTRUCTION:
    return const_cast<char *>("PRIV_INSTRUCTION");
  case EXCEPTION_STACK_OVERFLOW:
    return const_cast<char *>("STACK_OVERFLOW");
  default:
    FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleA("NTDLL.DLL"), code, 0,
        exception_buffer, sizeof(exception_buffer), nullptr);
    return exception_buffer;
  }
}

// Resolve a loaded module and its inclusive PE section range, publishing the one-based section and relative offset.
// Failure leaves logical outputs untouched; a successfully queried module name can still be present on section failure.
i32 ScExceptionHandler::GetLogicalAddress(void *address, char *filename, u32 capacity, u32 *section, u32 *offset) {
  MEMORY_BASIC_INFORMATION information;
  if (!VirtualQuery(address, &information, sizeof(information)))
    return 0;
  u8 *base = static_cast<u8 *>(information.AllocationBase);
  DWORD length = GetModuleFileNameA(static_cast<HMODULE>(information.AllocationBase), filename, capacity);
  if (!length)
    return 0;
  auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  IMAGE_SECTION_HEADER *headers = IMAGE_FIRST_SECTION(nt);
  uptr relative = reinterpret_cast<uptr>(address) - reinterpret_cast<uptr>(base);
  for (u32 i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    u32 size = headers[i].Misc.VirtualSize;
    if (headers[i].SizeOfRawData > size)
      size = headers[i].SizeOfRawData;
    if (relative >= headers[i].VirtualAddress && relative <= headers[i].VirtualAddress + size) {
      *section = i + 1;
      *offset = static_cast<u32>(relative - headers[i].VirtualAddress);
      return 1;
    }
  }
  return 0;
}

// Format through the original Windows formatter and write its returned byte count.
// Reserve its maximum 1024 output bytes and the terminator, including on native 64-bit hosts.
i32 SC3_CDECL ScExceptionHandler::Print(const char *format, ...) {
  char text[1025];
  va_list arguments;
  va_start(arguments, format);
  i32 length = wvsprintfA(text, format, arguments);
  va_end(arguments);
  DWORD written;
  WriteFile(report_file, text, static_cast<DWORD>(length), &written, nullptr);
  return length;
}

// Prefer Tool Help module enumeration, announcing and attempting PSAPI only when that first path returns false.
bool ScExceptionHandler::FillModuleList(std::vector<ScExceptionModule> *modules, u32 process_id, HANDLE process) {
  if (FillModuleListTH32(modules, process_id))
    return true;
  Print("Could not load modules using TH32; reverting to PSAPI\r\n");
  return FillModuleListPSAPI(modules, process_id, process);
}

// Resolve Tool Help from kernel32 or its historical fallback, append native module entries and close the snapshot/library.
// The original snapshot-failure branch retains the loaded library reference; an empty enumeration tests the live vector.
bool ScExceptionHandler::FillModuleListTH32(std::vector<ScExceptionModule> *modules, u32 process_id) {
  MODULEENTRY32 entry = {};
  entry.dwSize = sizeof(entry);
  ScExceptionModule module;
  HMODULE library = nullptr;
  decltype(&CreateToolhelp32Snapshot) snapshot_create = nullptr;
  decltype(&Module32First) module_first = nullptr;
  decltype(&Module32Next) module_next = nullptr;
  const char *libraries[] = {"kernel32.dll", "tlhelp32.dll"};
  for (const char *name : libraries) {
    library = LoadLibraryA(name);
    if (!library)
      continue;
    snapshot_create = reinterpret_cast<decltype(snapshot_create)>(GetProcAddress(library, "CreateToolhelp32Snapshot"));
    module_first = reinterpret_cast<decltype(module_first)>(GetProcAddress(library, "Module32First"));
    module_next = reinterpret_cast<decltype(module_next)>(GetProcAddress(library, "Module32Next"));
    if (snapshot_create && module_first && module_next)
      break;
    FreeLibrary(library);
    library = nullptr;
  }
  if (!library) {
    Print("Never could find Tool Help!\r\n");
    return false;
  }
  HANDLE snapshot = snapshot_create(TH32CS_SNAPMODULE, process_id);
  if (snapshot == INVALID_HANDLE_VALUE) {
    Print("CreateToolhelp32Snapshot failed!\r\n");
    return false;
  }
  if (module_first(snapshot, &entry)) {
    do {
#if defined(_WIN64)
      Print("%016IXh %6lu %-15.15s %s\r\n", reinterpret_cast<u64>(entry.modBaseAddr), entry.modBaseSize,
          entry.szModule, entry.szExePath);
#else
      Print("%08lXh %6lu %-15.15s %s\r\n", reinterpret_cast<DWORD>(entry.modBaseAddr), entry.modBaseSize,
          entry.szModule, entry.szExePath);
#endif
      module.image_name = ScString(entry.szExePath);
      module.module_name = ScString(entry.szModule);
      module.base = reinterpret_cast<uptr>(entry.modBaseAddr);
      module.size = entry.modBaseSize;
      modules->push_back(module);
    } while (module_next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  FreeLibrary(library);
  return !modules->empty();
}

// Resolve PSAPI, clear the caller's module vector and enumerate through the original64K handle and filename allocations.
// Failed/missing exports return false; successful enumeration reports the resulting vector's nonempty state.
// The native output record persists across iterations, including calls that leave previous fields untouched.
// Its consumed fields must have been written by the current query or retained from an earlier initialized result.
bool ScExceptionHandler::FillModuleListPSAPI(std::vector<ScExceptionModule> *modules, u32, HANDLE process) {
  ScExceptionModule module;
  HMODULE library = LoadLibraryA("PSAPI.DLL");
  if (!library)
    return false;
  modules->clear();
  auto enumerate = reinterpret_cast<decltype(&EnumProcessModules)>(GetProcAddress(library, "EnumProcessModules"));
  auto filename = reinterpret_cast<decltype(&GetModuleFileNameExA)>(GetProcAddress(library, "GetModuleFileNameExA"));
  auto basename = reinterpret_cast<decltype(&GetModuleBaseNameA)>(GetProcAddress(library, "GetModuleBaseNameA"));
  auto information = reinterpret_cast<decltype(&GetModuleInformation)>(GetProcAddress(library, "GetModuleInformation"));
  if (!enumerate || !filename || !basename || !information) {
    FreeLibrary(library);
    return false;
  }
  HMODULE *handles = new HMODULE[0x10000 / sizeof(HMODULE)];
  char *text = new char[0x10000];
  DWORD required;
  if (enumerate(process, handles, 0x10000, &required)) {
    if (required <= 0x10000) {
      MODULEINFO details;
      for (u32 i = 0; i < required / sizeof(HMODULE); ++i) {
        information(process, handles[i], &details, sizeof(details));
        module.base = reinterpret_cast<uptr>(details.lpBaseOfDll);
        module.size = details.SizeOfImage;
        *text = 0;
        filename(process, handles[i], text, 0x10000);
        module.image_name = ScString(text);
        *text = 0;
        basename(process, handles[i], text, 0x10000);
        module.module_name = ScString(text);
#if defined(_WIN64)
        Print("%016IXh %6lu %-15.15s %s\r\n", static_cast<u64>(module.base), static_cast<DWORD>(module.size),
            module.module_name.Data(), module.image_name.Data());
#else
        Print("%08lXh %6lu %-15.15s %s\r\n", static_cast<DWORD>(module.base), static_cast<DWORD>(module.size),
            module.module_name.Data(), module.image_name.Data());
#endif
        modules->push_back(module);
      }
    } else {
      Print("More than %lu module handles. Huh?\r\n", 1ul);
    }
  } else {
    Print("EPM failed, GetLastError() = %lu\r\n", GetLastError());
  }
  FreeLibrary(library);
  delete[] text;
  delete[] handles;
  return !modules->empty();
}

// Enumerate native modules and load each copied image/module name through the original optional symbol-loading import.
// Each returned symbol base decides the success/error report; copied name buffers are retired after that call.
void ScExceptionHandler::EnumAndLoadModuleSymbols(HANDLE process, u32 process_id) {
  std::vector<ScExceptionModule> modules;
  if (!sym_load_module) {
    Print(" _SymLoadModule was NULL!\r\n");
    return;
  }
  FillModuleList(&modules, process_id, process);
  for (const ScExceptionModule &module : modules) {
    char *image = new char[module.image_name.Strlen() + 1], *name = new char[module.module_name.Strlen() + 1];
    strcpy(image, module.image_name.Data());
    strcpy(name, module.module_name.Data());
    if (sym_load_module(process, nullptr, image, name, module.base, module.size))
      Print("Symbols loaded: \"%s\"\r\n", module.module_name.Data());
    else
      Print("Error %lu loading symbols for \"%s\"\r\n", GetLastError(), module.module_name.Data());
    delete[] image;
    delete[] name;
  }
}

// Load the actual image-helper exports in their original order, initialize without automatic invasion and load modules.
// Native64 uses the corresponding64-bit platform exports so real code/frame addresses are never truncated.
i32 ScExceptionHandler::InitImagehlpFunctions() {
  HMODULE library = LoadLibraryA("IMAGEHLP.DLL");
  if (!library) {
    Print("Could not load IMAGEHLP.DLL\r\n");
    return 0;
  }
  sym_initialize = reinterpret_cast<decltype(sym_initialize)>(GetProcAddress(library, "SymInitialize"));
  if (!sym_initialize) {
    Print("Could not loadproc _SymInitialize\r\n");
    return 0;
  }
  sym_cleanup = reinterpret_cast<decltype(sym_cleanup)>(GetProcAddress(library, "SymCleanup"));
  if (!sym_cleanup) {
    Print("Could not loadproc _SymCleanup\r\n");
    return 0;
  }
#if defined(_WIN64)
  stack_walk = reinterpret_cast<decltype(stack_walk)>(GetProcAddress(library, "StackWalk64"));
#else
  stack_walk = reinterpret_cast<decltype(stack_walk)>(GetProcAddress(library, "StackWalk"));
#endif
  if (!stack_walk) {
    Print("Could not loadproc _StackWalk\r\n");
    return 0;
  }
#if defined(_WIN64)
  sym_function_table =
      reinterpret_cast<decltype(sym_function_table)>(GetProcAddress(library, "SymFunctionTableAccess64"));
#else
  sym_function_table =
      reinterpret_cast<decltype(sym_function_table)>(GetProcAddress(library, "SymFunctionTableAccess"));
#endif
  if (!sym_function_table) {
    Print("Could not loadproc _SymFunctionTableAccess\r\n");
    return 0;
  }
#if defined(_WIN64)
  sym_module_base = reinterpret_cast<decltype(sym_module_base)>(GetProcAddress(library, "SymGetModuleBase64"));
#else
  sym_module_base = reinterpret_cast<decltype(sym_module_base)>(GetProcAddress(library, "SymGetModuleBase"));
#endif
  if (!sym_module_base) {
    Print("Could not loadproc _SymGetModuleBase\r\n");
    return 0;
  }
#if defined(_WIN64)
  sym_get_symbol = reinterpret_cast<decltype(sym_get_symbol)>(GetProcAddress(library, "SymGetSymFromAddr64"));
#else
  sym_get_symbol = reinterpret_cast<decltype(sym_get_symbol)>(GetProcAddress(library, "SymGetSymFromAddr"));
#endif
  if (!sym_get_symbol) {
    Print("Could not loadproc _SymGetSymFromAddr\r\n");
    return 0;
  }
#if defined(_WIN64)
  sym_load_module = reinterpret_cast<decltype(sym_load_module)>(GetProcAddress(library, "SymLoadModule64"));
#else
  sym_load_module = reinterpret_cast<decltype(sym_load_module)>(GetProcAddress(library, "SymLoadModule"));
#endif
  if (!sym_initialize(GetCurrentProcess(), nullptr, false)) {
    Print("_SymInitialize failed!\r\n");
    return 0;
  }
  EnumAndLoadModuleSymbols(GetCurrentProcess(), GetCurrentProcessId());
  return 1;
}

// A native stack-frame read supplies both actual pointer words together, excluding inaccessible or short platform reads.
// This is the report's memory boundary rather than an invented game callback or synthetic address map.
struct ScExceptionFrame {
  uptr previous, return_address;
};

// Read only a complete real frame from this process; callers never consume a partial output record.
static bool ScReadExceptionFrame(uptr address, ScExceptionFrame *out) {
  SIZE_T count;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), out, sizeof(*out), &count) &&
      count == sizeof(*out);
}

// Print the current instruction and ascending frame chain with module-relative addresses, stopping at the original
// checks.
void ScExceptionHandler::IntelStackWalk(CONTEXT *context) {
  Print("\r\nCall stack:\r\n");
  Print("Address   Frame     Logical addr  Module\r\n");
#if defined(_WIN64)
  uptr address = context->Rip, frame = context->Rbp;
#else
  uptr address = context->Eip, frame = context->Ebp;
#endif
  char filename[MAX_PATH] = {};
  u32 section = 0, offset = 0;
  GetLogicalAddress(reinterpret_cast<void *>(address), filename, MAX_PATH, &section, &offset);
  assert(memchr(filename, 0, sizeof(filename)));
#if defined(_WIN64)
  Print("%016IX  %016IX  %04X:%08X %s\r\n", static_cast<u64>(address), static_cast<u64>(frame), section, offset,
      filename);
#else
  Print("%08X  %08X  %04X:%08X %s\r\n", address, frame, section, offset, filename);
#endif
  for (;;) {
    ScExceptionFrame current;
    if (!ScReadExceptionFrame(frame, &current))
      break;
    uptr previous = frame;
    address = current.return_address;
    frame = current.previous;
    if ((frame & (sizeof(uptr) - 1)) || frame <= previous ||
        IsBadWritePtr(reinterpret_cast<void *>(frame), sizeof(current)))
      break;
    memset(filename, 0, sizeof(filename));
    section = 0;
    offset = 0;
    GetLogicalAddress(reinterpret_cast<void *>(address), filename, MAX_PATH, &section, &offset);
    assert(memchr(filename, 0, sizeof(filename)));
#if defined(_WIN64)
    Print("%016IX  %016IX  %04X:%08X %s\r\n", static_cast<u64>(address), static_cast<u64>(frame), section, offset,
        filename);
#else
    Print("%08X  %08X  %04X:%08X %s\r\n", address, frame, section, offset, filename);
#endif
  }
}

// Walk actual platform frames through the loaded image-helper imports and print symbols or module-relative fallbacks.
// The64-bit platform variant uses its own real CONTEXT/STACKFRAME64 and symbol records without truncating code addresses.
void ScExceptionHandler::ImagehlpStackWalk(CONTEXT *context) {
  Print("\r\nCall stack:\r\n");
  Print("Address   Frame\r\n");
#if defined(_WIN64)
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = context->Rip;
  frame.AddrFrame.Offset = context->Rbp;
  frame.AddrStack.Offset = context->Rsp;
  constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
  STACKFRAME frame = {};
  frame.AddrPC.Offset = context->Eip;
  frame.AddrFrame.Offset = context->Ebp;
  frame.AddrStack.Offset = context->Esp;
  constexpr DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  while (stack_walk(machine, GetCurrentProcess(), GetCurrentThread(), &frame, context, nullptr, sym_function_table,
             sym_module_base, nullptr) &&
      frame.AddrFrame.Offset) {
#if defined(_WIN64)
    Print("%016IX  %016IX  ", frame.AddrPC.Offset, frame.AddrFrame.Offset);
    alignas(IMAGEHLP_SYMBOL64) u8 storage[sizeof(IMAGEHLP_SYMBOL64) + 512];
    auto *symbol = reinterpret_cast<IMAGEHLP_SYMBOL64 *>(storage);
    DWORD64 displacement = 0;
#else
    Print("%08X  %08X  ", static_cast<u32>(frame.AddrPC.Offset), static_cast<u32>(frame.AddrFrame.Offset));
    alignas(IMAGEHLP_SYMBOL) u8 storage[sizeof(IMAGEHLP_SYMBOL) + 512];
    auto *symbol = reinterpret_cast<IMAGEHLP_SYMBOL *>(storage);
    DWORD displacement = 0;
#endif
    symbol->SizeOfStruct = sizeof(storage);
    symbol->MaxNameLength = 512;
    if (sym_get_symbol(GetCurrentProcess(), frame.AddrPC.Offset, &displacement, symbol)) {
#if defined(_WIN64)
      Print("%hs+%IX\r\n", symbol->Name, displacement);
#else
      Print("%hs+%X\r\n", symbol->Name, displacement);
#endif
    } else {
      char filename[MAX_PATH] = {};
      u32 section = 0, offset = 0;
      GetLogicalAddress(
          reinterpret_cast<void *>(static_cast<uptr>(frame.AddrPC.Offset)), filename, MAX_PATH, &section, &offset);
      assert(memchr(filename, 0, sizeof(filename)));
      Print("%04X:%08X %s\r\n", section, offset, filename);
    }
  }
}

// Append the original exception identity, time, fault module and register record, then choose symbol or frame-chain walking.
// The original consumes the fault module/section outputs as a terminated string and initialized scalars.
void ScExceptionHandler::GenerateExceptionReport(EXCEPTION_POINTERS *exception) {
  Print("//=====================================================\r\n");
  EXCEPTION_RECORD *record = exception->ExceptionRecord;
  Print("Exception code: %08X %s\r\n", record->ExceptionCode, GetExceptionString(record->ExceptionCode));
  Print("Thread ID: %d", GetCurrentThreadId());
  SYSTEMTIME time;
  GetLocalTime(&time);
  Print("Crash occurred: %02d/%02d/%d, @ %02d:%02d:%02d\r\n", time.wMonth, time.wDay, time.wYear, time.wHour,
      time.wMinute, time.wSecond);
  char filename[MAX_PATH];
  u32 section, offset;
  [[maybe_unused]] i32 found = GetLogicalAddress(record->ExceptionAddress, filename, MAX_PATH, &section, &offset);
  assert(found);
  assert(memchr(filename, 0, sizeof(filename)));
#if defined(_WIN64)
  Print("Fault address:  %016IX %02X:%08X %s\r\n", reinterpret_cast<u64>(record->ExceptionAddress), section, offset,
      filename);
#else
  Print("Fault address:  %08X %02X:%08X %s\r\n", reinterpret_cast<u32>(record->ExceptionAddress), section, offset,
      filename);
#endif
  CONTEXT *context = exception->ContextRecord;
  Print("\r\nRegisters:\r\n");
#if defined(_WIN64)
  Print("RAX:%016IX\r\nRBX:%016IX\r\nRCX:%016IX\r\nRDX:%016IX\r\nRSI:%016IX\r\nRDI:%016IX\r\n", context->Rax,
      context->Rbx, context->Rcx, context->Rdx, context->Rsi, context->Rdi);
  Print("CS:RIP:%04X:%016IX\r\n", context->SegCs, context->Rip);
  Print("SS:RSP:%04X:%016IX  RBP:%016IX\r\n", context->SegSs, context->Rsp, context->Rbp);
#else
  Print("EAX:%08X\r\nEBX:%08X\r\nECX:%08X\r\nEDX:%08X\r\nESI:%08X\r\nEDI:%08X\r\n", context->Eax, context->Ebx,
      context->Ecx, context->Edx, context->Esi, context->Edi);
  Print("CS:EIP:%04X:%08X\r\n", context->SegCs, context->Eip);
  Print("SS:ESP:%04X:%08X  EBP:%08X\r\n", context->SegSs, context->Esp, context->Ebp);
#endif
  Print("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n", context->SegDs, context->SegEs, context->SegFs, context->SegGs);
  Print("Flags:%08X\r\n", context->EFlags);
  if (InitImagehlpFunctions()) {
    ImagehlpStackWalk(context);
    sym_cleanup(GetCurrentProcess());
    Print("\r\n");
  } else {
    Print("IMAGEHLP.DLL or its exported procs not found\r\n");
    IntelStackWalk(context);
  }
}

#pragma warning(pop)
#endif

ScCallbackList::ScCallbackList() : head(new ScCallbackNode) {
  head->next = head;
  head->previous = head;
}

ScCallbackList::~ScCallbackList() {
  Clear();
  delete head;
}

// Copy actual job fields into a new tail node, preserving insertion order without copying source padding.
void ScCallbackList::Append(const ScCallbackJob &job) {
  auto *node = new ScCallbackNode;
  node->job = {job.identifier, job.callback, job.context, job.once, job.delay, job.deadline, job.fixed};
  node->next = head;
  node->previous = head->previous;
  node->previous->next = node;
  head->previous = node;
}

// Unlink one real job before releasing its storage.
void ScCallbackList::Erase(ScCallbackNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

void ScCallbackList::Clear() {
  while (head->next != head)
    Erase(head->next);
}

// Construct list, elapsed timer, worker and lock in source order, then initialize flags around the original priority change.
ScCallbackThread::ScCallbackThread() : elapsed(ScStopwatch::Milliseconds, 0) {
  u32 id;
  worker.GetThreadId(&id);
  stop = 0;
  next_id = 1;
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  ScThread::SetCurrentPriority(2);
  running = 0;
}

// Request termination before checking worker liveness, then resume/wake and retain the source's one-second active-flag wait.
ScCallbackThread::~ScCallbackThread() {
  u32 id;
  worker.GetThreadId(&id);
  stop = 1;
  if (worker.IsRunning()) {
    ScStopwatch waiting(ScStopwatch::Milliseconds, 0);
    worker.Resume();
    worker.Wake();
    waiting.Restart();
    while (waiting.GetElapsedTime() < 1000 && running)
      ;
  }
}

// Register a callback on the actual global scheduler, consuming its ID before taking the list lock.
// Empty queues restart the clock and start/resume the worker; populated queues receive a wake request after append.
i32 ScCallbackThread::AddEvent(ScTimerDispatch callback, ScCallbackTimer *context, u32 delay, bool once, bool fixed) {
  u32 identifier = next_id++;
  lock.Lock();
  bool empty = list.head->next == list.head;
  if (empty)
    elapsed.Restart();
  u32 deadline = delay + elapsed.GetElapsedTime();
  ScCallbackJob job{identifier, callback, context, once, delay, deadline, fixed};
  list.Append(job);
  if (empty) {
    if (!running) {
      running = 1;
      worker.Start(this);
    }
    worker.WakeForWork();
  } else
    worker.Wake();
  lock.Unlock();
  return (i32)identifier;
}

// Remove the first matching integer registration while holding the real queue lock, preserving the found/not-found result.
// High-bit IDs retain their original bit-pattern matching without a signed range check.
bool ScCallbackThread::RemoveEvent(i32 identifier) {
  lock.Lock();
  bool found = false;
  for (ScCallbackNode *node = list.head->next; node != list.head; node = node->next)
    if (node->job.identifier == (u32)identifier) {
      list.Erase(node);
      found = true;
      break;
    }
  lock.Unlock();
  return found;
}

// Capture due jobs under the list lock, update repeating deadlines, then invoke the copied callbacks after unlocking.
// Preserve unsigned deadline saturation, elapsed sleep deduction, empty-queue suspension and the source worker status queries.
u32 ScCallbackThread::StartFunction(void *) {
  u32 id;
  worker.GetThreadId(&id);
  ScCallbackList batch;
  running = 1;
  while (!stop) {
    if (list.head->next != list.head) {
      if (stop)
        break;
      lock.Lock();
      ScCallbackNode *node = list.head->next;
      u32 minimum = UINT32_MAX, now = elapsed.GetElapsedTime();
      batch.Clear();
      while (node != list.head) {
        ScCallbackNode *next = node->next;
        if (node->job.deadline <= now) {
          batch.Append(node->job);
          if (node->job.once) {
            list.Erase(node);
            node = next;
            continue;
          }
          node->job.deadline = node->job.delay + (node->job.fixed ? node->job.deadline : now);
          if (node->job.deadline < now)
            node->job.deadline = UINT32_MAX;
        }
        u32 delta = node->job.deadline - now;
        if (delta < minimum)
          minimum = delta;
        node = next;
      }
      lock.Unlock();
      for (node = batch.head->next; node != batch.head; node = node->next)
        node->job.callback(node->job.context);
      if (list.head->next != list.head) {
        if (stop)
          break;
        u32 spent = elapsed.GetElapsedTime() - now, wait = spent < minimum ? minimum - spent : 10;
        ScThread::SleepCurrentThread(wait * 1000u);
        worker.GetThreadId(&id);
        continue;
      }
    }
    if (stop)
      break;
    worker.GetThreadId(&id);
    worker.WaitForWake();
    worker.GetThreadId(&id);
  }
  worker.GetThreadId(&id);
  running = 0;
  return 0;
}

ScCallbackThread ScCallbacks;

static ScLock ScCallbackTimerLock;
static ScArray<ScCallbackTimer *> ScCallbackTimers;

// Locate a registered timer by identity while the caller holds the original recursive registry lock.
static u32 ScFindCallbackTimer(ScCallbackTimer *timer) {
  for (u32 i = 0; i < ScCallbackTimers.size; ++i)
    if (ScCallbackTimers.data[i] == timer)
      return i;
  return ScCallbackTimers.size;
}

// Initialize inactive timing state before registering this live timer under the shared recursive lock.
// The source leaves its callback context unwritten until SetCallbackFunction, so construction does not synthesize one.
ScCallbackTimer::ScCallbackTimer()
    : owner(nullptr), callback(nullptr), resolution(100), period(1000), running(false), once(false), event(0) {
  ScCallbackTimerLock.Lock();
  *ScCallbackTimers.Append() = this;
  ScCallbackTimerLock.Unlock();
}

// Remove registry membership before stopping an active schedule, then release any remaining owner and clear callback state.
ScCallbackTimer::~ScCallbackTimer() {
  ScCallbackTimerLock.Lock();
  u32 index = ScFindCallbackTimer(this);
  if (index != ScCallbackTimers.size) {
    memmove(ScCallbackTimers.data + index, ScCallbackTimers.data + index + 1,
        (ScCallbackTimers.size - index - 1) * sizeof(ScCallbackTimer *));
    --ScCallbackTimers.size;
  }
  ScCallbackTimerLock.Unlock();
  if (running)
    ScCallbackTimer::Stop();
  ScUnknown *remaining = owner;
  if (remaining) {
    remaining->Release();
    owner = nullptr;
  }
  callback = nullptr;
  argument = 0;
}

// Retain the captured owner and snapshot running before changing resolution, then restart through live virtual methods.
// The final conditional release tests the live owner but releases the captured identity, matching the source ordering.
bool ScCallbackTimer::SetResolution(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool active = running;
  resolution = value;
  if (active) {
    Stop();
    if (once)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return true;
}

bool ScCallbackTimer::GetResolution(u32 *value) {
  *value = resolution;
  return true;
}

// Capture running before changing the stored repeat period, protecting the saved owner through the virtual restart.
// Stop may clear the one-shot flag before the restart decision; do not cache its earlier value.
bool ScCallbackTimer::SetPeriod(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool active = running;
  period = value;
  if (active) {
    Stop();
    if (once)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return true;
}

bool ScCallbackTimer::GetPeriod(u32 *value) {
  *value = period;
  return true;
}

// Start only an inactive timer with a callback, retaining the supplied owner before publishing running state.
// A nonzero period replaces the stored period; failed scheduling retires the supplied hold and clears the live owner.
bool ScCallbackTimer::StartPeriodic(u32 requested, ScUnknown *incoming) {
  if (running || !callback)
    return false;
  if (incoming) {
    incoming->AddRef();
    owner = incoming;
  }
  if (requested)
    period = requested;
  running = true;
  event = ScCallbacks.AddEvent(InternalCallbackFunction, this, period, 0, 0);
  if (!event) {
    running = false;
    if (incoming)
      incoming->Release();
    owner = nullptr;
    return false;
  }
  return true;
}

// Schedule one shot using the supplied delay or stored period without replacing that period.
// Publish both mode bytes before scheduling; failure clears both and releases only the supplied owner hold.
bool ScCallbackTimer::StartOneShot(u32 delay, ScUnknown *incoming) {
  if (running || !callback)
    return false;
  if (!delay)
    delay = period;
  if (incoming) {
    incoming->AddRef();
    owner = incoming;
  }
  running = true;
  once = true;
  event = ScCallbacks.AddEvent(InternalCallbackFunction, this, delay, 1, 0);
  if (!event) {
    running = false;
    once = false;
    if (incoming)
      incoming->Release();
    owner = nullptr;
    return false;
  }
  return true;
}

// Remove a nonzero schedule before releasing its live owner and clearing the event and mode bytes.
// A zero event leaves every field untouched, even if a reentrant caller changed the mode bytes.
bool ScCallbackTimer::Stop() {
  if (event) {
    ScCallbacks.RemoveEvent(event);
    ScUnknown *held = owner;
    if (held) {
      held->Release();
      owner = nullptr;
    }
    event = 0;
    once = false;
    running = false;
  }
  return true;
}

bool ScCallbackTimer::IsRunning() {
  return running;
}

// Publish the original callback and its native-width argument without stopping or restarting the schedule.
// Store the function before its context; pending schedule state is unchanged.
bool ScCallbackTimer::SetCallbackFunction(ScTimerCallback incoming, uptr value) {
  callback = incoming;
  argument = value;
  return true;
}

// Capture a registered timer's callback and owner under the registry lock, then invoke outside it.
// Read context only for a captured callback; retire one-shot state before delivery and retain the null-callback owner hold.
u32 ScCallbackTimer::InternalCallbackFunction(ScCallbackTimer *timer) {
  ScCallbackTimerLock.Lock();
  if (ScFindCallbackTimer(timer) == ScCallbackTimers.size) {
    ScCallbackTimerLock.Unlock();
    return 0;
  }
  ScUnknown *saved = timer->owner;
  ScTimerCallback invoke = timer->callback;
  uptr value = invoke ? timer->argument : 0;
  bool one_shot = timer->once;
  if (saved)
    saved->AddRef();
  if (one_shot) {
    ScUnknown *live = timer->owner;
    if (live) {
      live->Release();
      timer->owner = nullptr;
    }
    timer->running = false;
    timer->once = false;
    timer->event = 0;
  }
  ScCallbackTimerLock.Unlock();
  if (invoke) {
    invoke(value);
    if (saved)
      saved->Release();
  }
  return 0;
}

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#include <list>
#include <utility>
#include <vector>

ApIniIndex::~ApIniIndex() {
  Clear();
}

// Clear all cached keys while keeping the native array allocation for later indexing.
void ApIniIndex::Clear() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i].name;
  entries.Clear();
}

// Compare complete String ranges, including bytes after an embedded zero. Return the first matching section.
ApIniSection *ApIniIndex::Find(const ScString *name) {
  usize length = name->end - name->begin;
  for (u32 i = 0; i < entries.size; ++i) {
    ScString *candidate = entries.data[i].name;
    if ((usize)(candidate->end - candidate->begin) == length && !memcmp(candidate->begin, name->begin, length))
      return &entries.data[i];
  }
  return nullptr;
}

void ApIniIndex::Insert(const ScString *name, u32 position) {
  if (!Find(name))
    *entries.Append() = {new ScString(*name), position};
}

static void ApIniUppercase(ScString *text) {
  text->MakeUpper();
}

// Trim only ASCII spaces at the left boundary; tabs and all other whitespace remain literal parser data.
static void ApIniTrimLeft(ScString *text) {
  u32 length = (u32)(text->end - text->begin), count = 0;
  while (count < length && text->begin[count] == ' ')
    ++count;
  if (count)
    text->Erase(0, count);
}

// Remove trailing ASCII spaces within the initialized String range. Do not reproduce the original all-space underread.
static void ApIniTrimRight(ScString *text) {
  while (text->end != text->begin && text->end[-1] == ' ')
    --text->end;
  *text->end = 0;
}

ApIniFile::ApIniFile() : keep_open(1), indexed(0) {
}

ApIniFile::ApIniFile(const ScString &source) : name(source), keep_open(1), indexed(0) {
}

ApIniFile::~ApIniFile() {
  if (file.IsOpen())
    file.Close();
}

// Borrow the actual filename member without adding a reference or copying its contents.
const ScString *ApIniFile::GetPath() const {
  return &name;
}

// Close a loaded reader before assigning the caller's complete concrete String. The original setter returns void.
void ApIniFile::SetPath(const ScString &source) {
  if (indexed)
    Close();
  name = source;
}

bool ApIniFile::Close() {
  if (file.IsOpen())
    file.Close();
  sections.Clear();
  indexed = 0;
  return true;
}

// Read the platform wall clock used to limit retries of an existing INI file.
static i32 ApIniRetryTime() {
#if defined(_WIN32)
  return static_cast<i32>(_time32(nullptr));
#else
  return static_cast<i32>(std::time(nullptr));
#endif
}

// Wait between INI open attempts using the platform's original alertable or SDL delay.
static void ApIniRetryDelay() {
#if defined(_WIN32)
  SleepEx(250, TRUE);
#else
  SDL_Delay(250);
#endif
}

// Retry the actual file open while the named path exists, capturing retry time before each alertable250ms sleep.
// Read/write access, creation and sharing masks follow the original byte argument.
bool ApIniFile::Open(bool write) {
  i32 start = ApIniRetryTime();
  while (!file.Open(write ? 3 : 1, write ? 3 : 2, write ? 0 : 1)) {
    if (RsFile::FileExists(name)) {
      i32 now = ApIniRetryTime();
      ApIniRetryDelay();
      if (now - start < 3)
        continue;
    }
    return false;
  }
  return true;
}

// Read forty-byte chunks, append each C-string prefix, consume the whole CR/LF run and restore its first following byte.
// A failed later read retains earlier text and the successful-read predicate.
bool ApIniFile::GetFileLine(ScString *out) {
  ScString text;
  u32 position = file.Position(), remaining = file.Size() - position;
  out->ScString::FromChar("", 0);
  char bytes[42] = {};
  bool read_any = false;
  while (remaining) {
    u32 count = remaining <= 40 ? remaining : 40, index = 0;
    if (!file.Read(bytes, count))
      break;
    read_any = true;
    remaining -= count;
    while (index < count && bytes[index] != 13 && bytes[index] != 10)
      ++index;
    bool newline = index < count;
    if (newline) {
      bytes[index] = 0;
      file.SeekAbsolute((i32)(position + index));
      char next;
      while (file.Read(&next, 1))
        if (next != 13 && next != 10) {
          file.SeekRelative(-1);
          break;
        }
    }
    bytes[index] = 0;
    text.Append(bytes, (u32)strlen(bytes));
    if (newline)
      break;
    position = file.Position();
  }
  out->ScString::FromChar(text.begin, (u32)(text.end - text.begin));
  return read_any;
}

// Index uppercase bracketed section names at their physical line starts, retaining the first duplicate.
// Reuse an open file or select/open its stored path before scanning through EOF.
bool ApIniFile::LoadSectionNames() {
  sections.Clear();
  if (!file.IsOpen()) {
    file.SetName(name);
    if (!Open(0))
      return false;
  }
  ScString line, section;
  file.SeekBegin();
  u32 position = file.Position();
  while (GetFileLine(&line)) {
    ApIniTrimLeft(&line);
    u32 length = (u32)(line.end - line.begin);
    if (length && line.begin[0] == '[') {
      u32 end = 1;
      while (end < length && line.begin[end] != ']')
        ++end;
      if (end < length && end >= 2) {
        section.ScString::FromChar(line.begin + 1, end - 1);
        ApIniUppercase(&section);
        sections.Insert(&section, position);
      }
    }
    position = file.Position();
  }
  indexed = 1;
  return true;
}

// Find the first case-insensitive key in the cached section, preserving literal value bytes and output on a miss.
// Seek/read order and close-on-query behavior follow the original live file and keep-open flag.
bool ApIniFile::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  bool found = false;
  if (indexed || LoadSectionNames()) {
    ScString section_name(section), key_name(key);
    ApIniUppercase(&section_name);
    ApIniUppercase(&key_name);
    ApIniSection *entry = sections.Find(&section_name);
    if (entry && file.SeekAbsolute((i32)entry->position) < file.Size()) {
      ScString candidate, line;
      if (GetFileLine(&line))
        while (GetFileLine(&line)) {
          ApIniTrimLeft(&line);
          u32 length = (u32)(line.end - line.begin);
          if (!length || line.begin[0] == ';')
            continue;
          if (line.begin[0] == '[')
            break;
          u32 delimiter = 0;
          while (delimiter < length && line.begin[delimiter] != '=')
            ++delimiter;
          candidate.ScString::FromChar(delimiter == length ? "" : line.begin, delimiter == length ? 0 : delimiter);
          if (delimiter != length)
            line.Erase(0, delimiter + 1);
          ApIniTrimRight(&candidate);
          ApIniUppercase(&candidate);
          u32 count = (u32)(candidate.end - candidate.begin);
          if (count == (u32)(key_name.end - key_name.begin) && !memcmp(candidate.begin, key_name.begin, count)) {
            ApIniTrimLeft(&line);
            ApIniTrimRight(&line);
            *out = line;
            found = true;
            break;
          }
        }
    }
  }
  if (!keep_open)
    Close();
  return found;
}

// Insert or replace a key while retaining surrounding file bytes and the original section-cache invalidation.
// File writes keep the original ignored results; only indexing, opening and initial section reads gate success.
bool ApIniFile::WriteEntry(const ScString &section, const ScString &key, const ScString &value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  ScString heading, entry;
  file.SetName(name);
  if (!RsFile::FileExists(name)) {
    indexed = 0;
    if (file.IsOpen())
      file.Close();
    if (!Open(true))
      return false;
    heading.Sprintf("[%s]\r\n", section.Data());
    file.Write(heading.Data(), heading.Strlen());
    entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(entry.Data(), entry.Strlen());
    file.Close();
    return true;
  }
  if (!indexed && !LoadSectionNames())
    return false;
  if (file.IsOpen())
    file.Close();
  if (!Open(true))
    return false;
  ScString section_name(section), key_name(key);
  ApIniUppercase(&section_name);
  ApIniUppercase(&key_name);
  ApIniSection *cached = sections.Find(&section_name);
  if (!cached) {
    file.SeekEnd();
    u32 position = file.Position();
    heading.Sprintf("\r\n[%s]\r\n", section.Data());
    file.Write(heading.Data(), heading.Strlen());
    entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
    file.Write(entry.Data(), entry.Strlen());
    file.Flush();
    sections.Insert(&section_name, position + 2);
    if (!keep_open)
      Close();
    return true;
  }
  if (file.SeekAbsolute(cached->position) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString candidate, search_key(key), line;
  ApIniUppercase(&search_key);
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  while (true) {
    u32 position = file.Position();
    if (!GetFileLine(&line))
      break;
    ApIniTrimLeft(&line);
    u32 length = line.Strlen();
    if (!length || line.begin[0] == ';')
      continue;
    bool next_section = line.begin[0] == '[';
    if (!next_section) {
      u32 delimiter = 0;
      while (delimiter < length && line.begin[delimiter] != '=')
        ++delimiter;
      candidate = line.Left(delimiter);
      ApIniTrimRight(&candidate);
      ApIniUppercase(&candidate);
      if (candidate.Strlen() != search_key.Strlen() || memcmp(candidate.begin, search_key.begin, candidate.Strlen()))
        continue;
    }
    u32 tail_position = next_section ? position : file.Position(), size = file.Size() - tail_position;
    u8 *tail = new u8[size];
    if (next_section)
      file.SeekAbsolute(position);
    if (file.Read(tail, size)) {
      file.SeekAbsolute(position);
      entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
      file.Write(entry.Data(), entry.Strlen());
      file.Write(tail, size);
      file.Resize(file.Position());
    }
    delete[] tail;
    indexed = 0;
    if (!keep_open)
      Close();
    return true;
  }
  file.SeekEnd();
  file.Position();
  entry.Sprintf("%s = %s\r\n", key.Data(), value.Data());
  file.Write(entry.Data(), entry.Strlen());
  file.Flush();
  if (!keep_open)
    Close();
  return true;
}

// Reject empty section/key names, invalidate the section index and format the signed value as decimal.
// Delegate through the actual virtual text writer and return its Boolean result.
bool ApIniFile::WriteNumber(const ScString &section, const ScString &key, i32 value) {
  if (section.begin == section.end || key.begin == key.end)
    return false;
  indexed = 0;
  ScString text;
  text.Sprintf("%ld", static_cast<long>(value));
  return WriteEntry(section, key, text);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
// Clear the caller's integer before reading text, then use the original decimal scanf conversion.
// A missing entry or failed conversion returns false while retaining the initialized zero output.
bool ApIniFile::ReadNumber(const ScString &section, const ScString &key, i32 *out) {
  ScString text;
  *out = 0;
  return ReadEntry(section, key, &text) && sscanf(text.Data(), "%" SCNd32, out) == 1;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Encode each byte as two uppercase hexadecimal characters, followed by the original trailing '0'.
void ScConvertBinaryDataToASCIIArray(const u8 *bytes, u32 length, char *out) {
  for (u32 i = 0; i < length; ++i) {
    u8 high = static_cast<u8>((bytes[i] >> 4) + '0'), low = static_cast<u8>((bytes[i] & 15) + '0');
    *out++ = static_cast<char>(high > '9' ? high + 7 : high);
    *out++ = static_cast<char>(low > '9' ? low + 7 : low);
  }
  *out = '0';
}

// Decode pairs of hexadecimal characters while replacing invalid digits with zero and reporting their presence.
bool ScConvertASCIIArrayToBinaryData(const char *text, u32 length, u8 *out) {
  bool valid = true;
  for (u32 i = 0; i < length; i += 2) {
    u8 byte = 0;
    for (i32 shift = 4; shift >= 0; shift -= 4) {
      i32 digit = *text++;
      if (digit < '0' || (digit > '9' && digit < 'A') || (digit > 'F' && digit < 'a') || digit > 'f') {
        digit = '0';
        valid = false;
      } else if (digit > 'F') {
        digit -= 39;
      } else if (digit > '9') {
        digit -= 7;
      }
      byte = static_cast<u8>(byte + ((digit - '0') << shift));
    }
    *out++ = byte;
  }
  return valid;
}

// Encode a nonempty byte array and delegate its text to the original virtual entry writer.
bool ApIniFile::WriteBinaryEntry(const ScString &section, const ScString &key, const u8 *bytes, u32 size) {
  if (!size)
    return false;
  char *encoded = new char[static_cast<usize>(size) * 2 + 1];
  ScConvertBinaryDataToASCIIArray(bytes, size, encoded);
  encoded[static_cast<usize>(size) * 2] = 0;
  bool result;
  {
    ScString text(encoded);
    result = WriteEntry(section, key, text);
  }
  delete[] encoded;
  return result;
}

// Read and decode an entry into newly owned bytes; leave the caller's pointer unchanged when the entry is absent.
// Extra space for an odd digit count excludes the original out-of-bounds write on that undefined path.
u32 ApIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 **out) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = static_cast<u32>(text.end - text.begin), count = length >> 1;
  *out = new u8[count + (length & 1)];
  if (!ScConvertASCIIArrayToBinaryData(text.begin, length, *out)) {
    delete[] *out;
    *out = nullptr;
    return 0;
  }
  return count;
}

// Decode at most twice the supplied byte capacity and return the number of complete pairs on success.
u32 ApIniFile::ReadBinaryEntry(const ScString &section, const ScString &key, u8 *out, u32 size) {
  ScString text;
  if (!ReadEntry(section, key, &text))
    return 0;
  u32 length = static_cast<u32>(text.end - text.begin), limit = size * 2;
  if (limit > length)
    limit = length;
  return ScConvertASCIIArrayToBinaryData(text.begin, limit, out) ? limit >> 1 : 0;
}

// Collect complete key/value copies before invoking callbacks, preserving section order and parser position during
// callbacks.
bool ApIniFile::EnumEntries(const ScString &section, ApIniCallback callback, ApIniContext *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString section_name(section);
  ApIniUppercase(&section_name);
  ApIniSection *cached = sections.Find(&section_name);
  if (!cached || file.SeekAbsolute(cached->position) >= file.Size()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString key, line;
  if (!GetFileLine(&line)) {
    if (!keep_open)
      Close();
    return false;
  }
  std::vector<std::pair<ScString, ScString>> entries;
  while (GetFileLine(&line)) {
    ApIniTrimLeft(&line);
    u32 length = static_cast<u32>(line.end - line.begin);
    if (!length || line.begin[0] == ';')
      continue;
    if (line.begin[0] == '[')
      break;
    u32 delimiter = 0;
    while (delimiter < length && line.begin[delimiter] != '=')
      ++delimiter;
    key.ScString::FromChar(delimiter == length ? "" : line.begin, delimiter == length ? 0 : delimiter);
    if (delimiter != length)
      line.Erase(0, delimiter + 1);
    ApIniTrimRight(&key);
    ApIniTrimLeft(&line);
    ApIniTrimRight(&line);
    if (key.begin != key.end && line.begin != line.end) {
      std::pair<ScString, ScString> entry(key, line);
      entries.push_back(entry);
    }
  }
  u32 count = static_cast<u32>(entries.size());
  for (u32 i = 0; i < count; ++i)
    callback(entries[i].first, entries[i].second, context);
  if (!keep_open)
    Close();
  return true;
}

// Snapshot section names in the original index order before invoking callbacks with an empty value.
bool ApIniFile::EnumSections(ApIniCallback callback, ApIniContext *context) {
  if (!indexed && !LoadSectionNames()) {
    if (!keep_open)
      Close();
    return false;
  }
  ScString empty;
  std::list<ScString> names;
  for (u32 i = 0; i < sections.entries.size; ++i) {
    const ScString &section_name = *sections.entries.data[i].name;
    auto position = names.begin();
    for (; position != names.end(); ++position) {
      usize left = static_cast<usize>(section_name.end - section_name.begin),
            right = static_cast<usize>(position->end - position->begin);
      usize length = left < right ? left : right;
      int order = memcmp(section_name.begin, position->begin, length);
      if (order < 0 || (!order && left < right))
        break;
    }
    names.insert(position, section_name);
  }
  for (const ScString &section_name : names)
    callback(section_name, empty, context);
  if (!keep_open)
    Close();
  return true;
}

// Load and uppercase the requested section before applying the keep-open policy and searching the live index.
// Closing clears that index before lookup, matching the original close-on-query behavior.
bool ApIniFile::SectionExists(const ScStringView &section) {
  if (!indexed && !LoadSectionNames())
    return false;
  ScString key(section);
  ApIniUppercase(&key);
  if (!keep_open)
    Close();
  return sections.Find(&key) != nullptr;
}

// Change the keep-open preference only when necessary, closing immediately when it becomes false.
// The original returns true regardless of the close result.
bool ApIniFile::SetLeaveFileOpenBetweenOperations(bool leave_open) {
  if (keep_open != leave_open) {
    keep_open = leave_open;
    if (!leave_open)
      Close();
  }
  return true;
}

static void ApPackageUppercase(ScString *value) {
  ApIniUppercase(value);
}

// Extract the prefix before the first equals sign, retaining the input unchanged when the delimiter is missing.
static ScString ApPackageTakeKey(ScString *line) {
  u32 length = static_cast<u32>(line->end - line->begin), position = 0;
  while (position < length && line->begin[position] != '=')
    ++position;
  if (position == length)
    return ScString();
  ScString key(line->begin, position);
  line->Erase(0, position + 1);
  return key;
}

// Form the uppercase basename and extension used by the package index, with the CRT component limits.
static ScString ApPackageKey(const ScString &name) {
  const char *begin = name.begin, *base = begin, *dot = nullptr, *end = begin + strlen(begin);
  if (begin != end && begin[1] == ':')
    base += 2;
  for (const char *byte = base; byte != end; ++byte)
    if (*byte == '/' || *byte == '\\')
      base = byte + 1;
    else if (*byte == '.')
      dot = byte;
  if (!dot || dot < base)
    dot = end;
  usize stem = static_cast<usize>(dot - base), extension = static_cast<usize>(end - dot);
  if (stem > 255)
    stem = 255;
  if (extension > 255)
    extension = 255;
  ScString result(base, static_cast<u32>(stem));
  result.Append(dot, static_cast<u32>(extension));
  ApPackageUppercase(&result);
  return result;
}

ApCompressedIni::ApCompressedIni() : mode(0), buffer(nullptr), buffer_capacity(0) {
}

ApCompressedIni::~ApCompressedIni() {
  delete[] buffer;
}

void ApCompressedIni::SetCompressedPath(const ScString &path) {
  mode = 0;
  package_name = path;
}

// Return the borrowed package-path String.
const ScString *ApCompressedIni::GetCompressedPath() const {
  return &package_name;
}

// Read the length-prefixed package string and reuse prior storage for a zero-length record.
// Only the allocation terminator is initialized before the file read; an earlier NUL also makes a short read defined.
void ApCompressedIni::ReadString(ScString *out) {
  u32 length = RsReadPackageWord(&package_file);
  if (length > buffer_capacity) {
    if (length == UINT32_MAX) {
      assert(false && "The original packaged INI allocation would wrap");
      abort();
    }
    delete[] buffer;
    buffer = new char[static_cast<usize>(length) + 1];
    buffer[length] = 0;
  }
  u32 received = length;
  if (!package_file.opened)
    received = 0;
  else
    package_file.Read(buffer, &received);
  if (!buffer || (received < length && !memchr(buffer, 0, received))) {
    assert(false && "The original packaged INI string would consume undefined storage");
    abort();
  }
  ScString value(buffer);
  *out = value;
}

// Prefer the package's matching uppercase filename entry, otherwise select the original loose-file fallback.
void ApCompressedIni::InitFileType() {
  if (mode)
    return;
  if (!package_name.Strlen() || !RsFile::FileExists(package_name)) {
    mode = 1;
    return;
  }
  package_file.SetName(package_name);
  if (!package_file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    mode = 1;
    return;
  }
  package_offset = 0;
  ScString expected = ApPackageKey(name);
  package_file.SeekBegin();
  // The original uses the count word as the next offset read buffer, retaining untouched bytes on a short read.
  u32 count = RsReadPackageWord(&package_file), offset = count;
  ScString entry;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&entry);
    ApPackageUppercase(&entry);
    package_file.Read(&offset, 4);
    if (entry.Strlen() == expected.Strlen() && !memcmp(entry.begin, expected.begin, entry.Strlen())) {
      package_offset = offset;
      mode = 2;
      return;
    }
  }
  mode = 1;
}

// Read a matching packaged section and case-insensitive key, or delegate to the loose reader when selected.
bool ApCompressedIni::ReadEntry(const ScString &section, const ScString &key, ScString *out) {
  InitFileType();
  if (mode == 1)
    return ApIniFile::ReadEntry(section, key, out);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(package_offset);
  ScString header("[");
  header.Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.Append("]");
  u32 count = RsReadPackageWord(&package_file);
  ScString line, wanted(key);
  ApPackageUppercase(&wanted);
  wanted.LTrim();
  wanted.RTrim();
  bool selected = false;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (selected) {
      if (line.begin != line.end && line.begin[0] == '[')
        break;
      ScString candidate = ApPackageTakeKey(&line);
      ApPackageUppercase(&candidate);
      candidate.LTrim();
      candidate.RTrim();
      if (candidate.Strlen() == wanted.Strlen() && !memcmp(candidate.begin, wanted.begin, candidate.Strlen())) {
        *out = line;
        return true;
      }
    } else if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen())) {
      selected = true;
    }
  }
  return false;
}

// Enumerate packaged rows immediately, stopping at the next section or the first empty key/value pair.
// Loose-file enumeration uses the inherited callback and its original buffering behavior.
bool ApCompressedIni::EnumEntries(const ScString &section, ApIniCallback callback, ApIniContext *context) {
  InitFileType();
  if (mode == 1)
    return ApIniFile::EnumEntries(section, callback, context);
  if (mode != 2)
    return false;
  package_file.SeekAbsolute(package_offset);
  ScString header("[");
  header.Append(section.begin, static_cast<u32>(section.end - section.begin));
  header.Append("]");
  u32 count = RsReadPackageWord(&package_file);
  ScString line;
  bool selected = false;
  for (u32 index = 0; index < count; ++index) {
    ReadString(&line);
    if (!selected) {
      if (line.Strlen() == header.Strlen() && !memcmp(line.begin, header.begin, line.Strlen()))
        selected = true;
      continue;
    }
    if (line.begin != line.end && line.begin[0] == '[')
      break;
    ScString key = ApPackageTakeKey(&line);
    key.LTrim();
    key.RTrim();
    line.LTrim();
    line.RTrim();
    if (key.begin == key.end || line.begin == line.end)
      break;
    callback(key, line, context);
  }
  return true;
}

bool ApCompressedIni::WriteEntry(const ScString &, const ScString &, const ScString &) {
  return false;
}

bool ApCompressedIni::WriteNumber(const ScString &, const ScString &, i32) {
  return false;
}

bool ApCompressedIni::WriteBinaryEntry(const ScString &, const ScString &, const u8 *, u32) {
  return false;
}

bool ApCompressedIni::ReadNumber(const ScString &, const ScString &, i32 *) {
  return false;
}

u32 ApCompressedIni::ReadBinaryEntry(const ScString &, const ScString &, u8 **) {
  return 0;
}

u32 ApCompressedIni::ReadBinaryEntry(const ScString &, const ScString &, u8 *, u32) {
  return 0;
}

bool ApCompressedIni::SectionExists(const ScStringView &) {
  return false;
}

bool ApCompressedIni::EnumEntriesOld(const ScString &, ApIniCallback, ApIniContext *) {
  return false;
}

ApRuntime::ScRefCount::ScRefCount() : references(0) {
}

ApRuntime::ScRefCount::~ScRefCount() {
}

// Invoke the virtual nondeleting decrement, then delete the complete owner when that operation returns zero.
u32 ApRuntime::ScRefCount::Release() {
  u32 remaining = RemoveRef();
  if (!remaining)
    delete this;
  return remaining;
}

u32 ApRuntime::ScRefCount::AddRef() {
  ApGetDirector()->AddRef();
  return ++references;
}

// Release the executable director before decrementing a nonzero count; zero remains zero without module effects.
u32 ApRuntime::ScRefCount::RemoveRef() {
  if (references) {
    ApGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ApRuntime::ScRefCount::RefCount() {
  return references;
}

ApRuntime::ScGameCommandFactory::ScGameCommandFactory() : references(0) {
}

// A null child terminates the original release walk before the owned list storage is destroyed.
ApRuntime::ScGameCommandFactory::~ScGameCommandFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

// Publish the factory identity for Unknown and the actual command-factory IID, then retain through its virtual method.
bool ApRuntime::ScGameCommandFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining the executable director, then return the live object count.
u32 ApRuntime::ScGameCommandFactory::AddRef() {
  ++references;
  ApGetDirector()->AddRef();
  return references;
}

// Decrement before releasing the executable director; delete the complete factory if the live count is zero.
u32 ApRuntime::ScGameCommandFactory::Release() {
  --references;
  ApGetDirector()->RemoveRef();
  u32 value = references;
  if (!value) {
    delete this;
    return 0;
  }
  return value;
}

// Ask child factories in order with the original three arguments, then dispatch to the concrete factory.
bool ApRuntime::ScGameCommandFactory::GetCommand(u32 command, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 i = 0; i < children.size; ++i, ++cursor)
    if ((*cursor)->GetCommand(command, iid, out))
      return true;
  return CreateCommand(command, iid, out);
}

// Reject a null child; otherwise retain it before appending its real interface pointer to the child sequence.
bool ApRuntime::ScGameCommandFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

ApRuntime::ScGameCommand::ScGameCommand() {
}

// Drain pending inputs before outputs, transferring each pointer out of its queue before releasing it.
// The descriptor sequences and backing storage retire after the queued argument callbacks finish.
ApRuntime::ScGameCommand::~ScGameCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool ApRuntime::ScGameCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ApRuntime::ScGameCommand::AddRef() {
  return ApRuntime::ScRefCount::AddRef();
}

u32 ApRuntime::ScGameCommand::Release() {
  return ApRuntime::ScRefCount::Release();
}

// Retain the supplied input before publishing it at the back of the pending argument sequence.
bool ApRuntime::ScGameCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Leave an empty output untouched; otherwise publish the oldest retained input before removing its queue entry.
bool ApRuntime::ScGameCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 ApRuntime::ScGameCommand::InputQueueSize() {
  return inputs.size;
}

// Enumerate input descriptors in insertion order, observing descriptors appended by the callback before testing the end.
bool ApRuntime::ScGameCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    ScCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain the supplied output before publishing it at the back of the result sequence.
bool ApRuntime::ScGameCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

bool ApRuntime::ScGameCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 ApRuntime::ScGameCommand::OutputQueueSize() {
  return outputs.size;
}

// Enumerate output descriptors in order and pass the original separate type, flags and context arguments.
bool ApRuntime::ScGameCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    ScCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool ApRuntime::ScGameCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool ApRuntime::ScGameCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

ApMainMenuCommand::~ApMainMenuCommand() {
}

bool ApMainMenuCommand::Execute() {
  ApGetGameApp()->GetSC3MainWindow()->DoMainMenu();
  return true;
}

// Construct the command base and register the three original signed input descriptors in order.
ApFireDisasterCommand::ApFireDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApTornadoDisasterCommand::ApTornadoDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApEarthquakeDisasterCommand::ApEarthquakeDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApRiotDisasterCommand::ApRiotDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApUfoDisasterCommand::ApUfoDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApToxicCloudDisasterCommand::ApToxicCloudDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApWhirlpoolDisasterCommand::ApWhirlpoolDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApLocustsDisasterCommand::ApLocustsDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApSpaceDebrisDisasterCommand::ApSpaceDebrisDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the command base and register the three original signed input descriptors in order.
ApSmiteDisasterCommand::ApSmiteDisasterCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

// Construct the parade command and register its three original integer input descriptors.
ApParadeCommand::ApParadeCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

ApParadeCommand::~ApParadeCommand() {
}

bool ApParadeCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsParadeTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

ApQuitCommand::ApQuitCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, kApQuitModeParameterFlags);
}

bool ApFireDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsFireTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApTornadoDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsTornadoTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApEarthquakeDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsTrackTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApRiotDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsRiotTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApUfoDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsUfoTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApToxicCloudDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsToxicTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApWhirlpoolDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsWaterTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApLocustsDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsLocustTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApSpaceDebrisDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsImpactTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

bool ApSmiteDisasterCommand::Execute() {
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_RequestDisaster, kDsEventTypeId, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

ApCommandFactory::~ApCommandFactory() {
}

// Construct supported local commands, query the requested identity and delete refused zero-reference objects.
bool ApCommandFactory::CreateCommand(u32 command, u32 iid, void **out) {
  ApRuntime::ScGameCommand *created;
  switch (command) {
  case kApMainMenuCommandClassId:
    created = new ApMainMenuCommand;
    break;
  case kApFireDisasterCommandClassId:
    created = new ApFireDisasterCommand;
    break;
  case kApTornadoDisasterCommandClassId:
    created = new ApTornadoDisasterCommand;
    break;
  case kApEarthquakeDisasterCommandClassId:
    created = new ApEarthquakeDisasterCommand;
    break;
  case kApRiotDisasterCommandClassId:
    created = new ApRiotDisasterCommand;
    break;
  case kApUfoDisasterCommandClassId:
    created = new ApUfoDisasterCommand;
    break;
  case kApToxicCloudDisasterCommandClassId:
    created = new ApToxicCloudDisasterCommand;
    break;
  case kApWhirlpoolDisasterCommandClassId:
    created = new ApWhirlpoolDisasterCommand;
    break;
  case kApLocustsDisasterCommandClassId:
    created = new ApLocustsDisasterCommand;
    break;
  case kApSpaceDebrisDisasterCommandClassId:
    created = new ApSpaceDebrisDisasterCommand;
    break;
  case kApSmiteDisasterCommandClassId:
    created = new ApSmiteDisasterCommand;
    break;
  case kApQuitCommandClassId:
    created = new ApQuitCommand;
    break;
  case kApEmergencyWarningCommandClassId:
    created = new ApEmergencyWarningCommand;
    break;
  case kApDispatchFirefightersCommandClassId:
    created = static_cast<ApRuntime::ScGameCommand *>(new ApDispatchFirefightersCommand);
    break;
  case kApDispatchPoliceCommandClassId:
    created = static_cast<ApRuntime::ScGameCommand *>(new ApDispatchPoliceCommand);
    break;
  case kApDispatchPlaneCommandClassId:
    created = static_cast<ApRuntime::ScGameCommand *>(new ApDispatchPlaneCommand);
    break;
  case kApToolbarCommandClassId:
    created = new ApToolbarCommand;
    break;
  default:
    *out = nullptr;
    return false;
  }
  if (created->QueryInterface(iid, out))
    return true;
  *out = nullptr;
  delete created;
  return false;
}

// Consume an optional unsigned quit mode, request the corresponding application exit and enqueue its Boolean result.
// Invalid input types are released and rejected; an unknown mode retains the original default successful result.
bool ApQuitCommand::Execute() {
  u32 mode = 0;
  bool answer = true;
  ApGameInterface *game = ApGetGameApp();
  if (InputQueueSize()) {
    ScVariant *argument = nullptr;
    if (!PopFrontInput(&argument))
      return false;
    if (argument->GetType() != kScValueUint32) {
      argument->Release();
      return false;
    }
    mode = *argument->AsUint32();
    argument->Release();
  }
  switch (mode) {
  case 0:
    answer = game->Quit(true, true);
    break;
  case 1:
    answer = game->Quit(false, true);
    break;
  case 2:
    answer = game->Quit(false, false);
    break;
  default:
    break;
  }
  ScValue *result = new ScValue(answer);
  return PushBackOutput(result);
}

// Construct the common command and append the three original signed input descriptors in order.
ApEmergencyWarningCommand::ApEmergencyWarningCommand() {
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
  ApRuntime::ScGameCommand::AddInputParameter(3, 0);
}

ApEmergencyWarningCommand::~ApEmergencyWarningCommand() {
}

// Reset the consumed-input count, enumerate descriptors and post the original warning message if a server exists.
// The original ignores both enumeration and post results and returns true.
bool ApEmergencyWarningCommand::Execute() {
  arguments_found = 0;
  EnumInputParameters(InputParameterCallback, this);
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    ScMessage message(kDsMsg_StartDisasterCounter, 0, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

// Consume and release one queued input for each zero-flags descriptor, updating the live count before release.
// Nonzero descriptor flags leave both the queue and count untouched.
void ApEmergencyWarningCommand::InputParameterCallback(i32, u32 flags, void *context) {
  if (!flags) {
    ApEmergencyWarningCommand *command = static_cast<ApEmergencyWarningCommand *>(context);
    ScVariant *argument = nullptr;
    command->PopFrontInput(&argument);
    ++command->arguments_found;
    argument->Release();
  }
}

ApDispatchFirefightersCommand::ApDispatchFirefightersCommand() {
}

ApDispatchFirefightersCommand::~ApDispatchFirefightersCommand() {
}

bool ApDispatchFirefightersCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return ApRuntime::ScGameCommand::QueryInterface(iid, out);
}

u32 ApDispatchFirefightersCommand::AddRef() {
  return ApRuntime::ScGameCommand::AddRef();
}

u32 ApDispatchFirefightersCommand::Release() {
  return ApRuntime::ScGameCommand::Release();
}

// Refuse dispatch without a city; otherwise attach the actual point-selection handler to the live city window.
bool ApDispatchFirefightersCommand::Execute() {
  if (!ApGetCity())
    return false;
  return Attach(ApGetCityWindow());
}

// Post the original Firefighters request only when the selected cell is not completely covered by water.
// Missing message servers leave the request unposted while preserving the original successful result.
bool ApDispatchFirefightersCommand::ExecuteOnMouseUp(const CtCoord &point) {
  i32 x = point.x >> 8, z = point.z >> 8;
  ScDirtBag *terrain = ApGetGameApp()->GetCity()->DirtBag();
  if (terrain->CellWaterVertCount(static_cast<u32>(x), static_cast<u32>(z)) != 4) {
    ScMessageServer *server = ApGetMessageServer();
    if (server) {
      ScMessage message(kDsMsg_DispatchFire, static_cast<u32>(x), static_cast<u32>(z), 0);
      server->MessagePost(message, false);
    }
  }
  return true;
}

bool ApDispatchFirefightersCommand::GetCursor(u32 *out) {
  *out = kSpDispatchFirefightersCursorId;
  return true;
}

bool ApDispatchFirefightersCommand::IsValidHandlerByView(i32 view_kind, u32) const {
  return view_kind == 0;
}

ApDispatchPoliceCommand::ApDispatchPoliceCommand() {
}

ApDispatchPoliceCommand::~ApDispatchPoliceCommand() {
}

bool ApDispatchPoliceCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return ApRuntime::ScGameCommand::QueryInterface(iid, out);
}

u32 ApDispatchPoliceCommand::AddRef() {
  return ApRuntime::ScGameCommand::AddRef();
}

u32 ApDispatchPoliceCommand::Release() {
  return ApRuntime::ScGameCommand::Release();
}

// Refuse dispatch without a city; otherwise attach the actual point-selection handler to the live city window.
bool ApDispatchPoliceCommand::Execute() {
  if (!ApGetCity())
    return false;
  return Attach(ApGetCityWindow());
}

// Post the original Police request only when the selected cell is not completely covered by water.
// Missing message servers leave the request unposted while preserving the original successful result.
bool ApDispatchPoliceCommand::ExecuteOnMouseUp(const CtCoord &point) {
  i32 x = point.x >> 8, z = point.z >> 8;
  ScDirtBag *terrain = ApGetGameApp()->GetCity()->DirtBag();
  if (terrain->CellWaterVertCount(static_cast<u32>(x), static_cast<u32>(z)) != 4) {
    ScMessageServer *server = ApGetMessageServer();
    if (server) {
      ScMessage message(kDsMsg_DispatchPolice, static_cast<u32>(x), static_cast<u32>(z), 0);
      server->MessagePost(message, false);
    }
  }
  return true;
}

bool ApDispatchPoliceCommand::GetCursor(u32 *out) {
  *out = kSpDispatchPoliceCursorId;
  return true;
}

bool ApDispatchPoliceCommand::IsValidHandlerByView(i32 view_kind, u32) const {
  return view_kind == 0;
}

ApDispatchPlaneCommand::ApDispatchPlaneCommand() {
}

ApDispatchPlaneCommand::~ApDispatchPlaneCommand() {
}

bool ApDispatchPlaneCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return ApRuntime::ScGameCommand::QueryInterface(iid, out);
}

u32 ApDispatchPlaneCommand::AddRef() {
  return ApRuntime::ScGameCommand::AddRef();
}

u32 ApDispatchPlaneCommand::Release() {
  return ApRuntime::ScGameCommand::Release();
}

// Refuse dispatch without a city; otherwise attach the actual point-selection handler to the live city window.
bool ApDispatchPlaneCommand::Execute() {
  if (!ApGetCity())
    return false;
  return Attach(ApGetCityWindow());
}

// Post the original Plane request only when the selected cell is not completely covered by water.
// Missing message servers leave the request unposted while preserving the original successful result.
bool ApDispatchPlaneCommand::ExecuteOnMouseUp(const CtCoord &point) {
  i32 x = point.x >> 8, z = point.z >> 8;
  ScDirtBag *terrain = ApGetGameApp()->GetCity()->DirtBag();
  if (terrain->CellWaterVertCount(static_cast<u32>(x), static_cast<u32>(z)) != 4) {
    ScMessageServer *server = ApGetMessageServer();
    if (server) {
      ScMessage message(kDsMsg_DispatchPlane, static_cast<u32>(x), static_cast<u32>(z), 0);
      server->MessagePost(message, false);
    }
  }
  return true;
}

bool ApDispatchPlaneCommand::GetCursor(u32 *out) {
  *out = kSpDispatchPlaneCursorId;
  return true;
}

bool ApDispatchPlaneCommand::IsValidHandlerByView(i32 view_kind, u32) const {
  return view_kind == 0;
}

ApToolbarCommand::~ApToolbarCommand() {
}

bool ApToolbarCommand::Execute() {
  return false;
}

ApGridToggleCommand::~ApGridToggleCommand() {
}

// Query the framework's application, obtain the live city terrain and invert its original grid flag.
// The original helper retains the queried application reference on every successful query.
bool ApGridToggleCommand::Execute() {
  ApGameInterface *game = nullptr;
  if (!ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&game)))
    return false;
  CtCity *city = game->GetCity();
  if (!city)
    return false;
  ScDirtBag *terrain = city->DirtBag();
  if (!terrain)
    return false;
  terrain->SetGrid(!terrain->GetGrid());
  return true;
}

ApSpriteFilterCommandFactory::~ApSpriteFilterCommandFactory() {
}

// Construct and query the sole original grid-toggle product; a refused query destroys that unretained product.
// Unsupported command IDs clear the output, while a rejected interface retains the query's own output behavior.
bool ApSpriteFilterCommandFactory::CreateCommand(u32 command, u32 iid, void **out) {
  if (command != kApGridToggleCommandId) {
    *out = nullptr;
    return false;
  }
  ApGridToggleCommand *created = new ApGridToggleCommand;
  if (created->QueryInterface(iid, out))
    return true;
  delete created;
  return false;
}

// Acquire the command through the real factory service, releasing the temporary factory after its lookup.
// The command field starts null and receives the factory's actual output even when its Boolean result is ignored.
ScAutoCommand::ScAutoCommand(u32 identifier) : command(nullptr) {
  ScFrameworkRef<ScCommandFactory> factory(nullptr);
  ApGetFramework()->GetSystemService(
      kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory.object));
  if (factory.object)
    factory.object->GetCommand(identifier, IID_UiCommandInterface, reinterpret_cast<void **>(&command));
}

ScAutoCommand::ScAutoCommand(const ScAutoCommand &source) : command(source.command) {
  if (command)
    command->AddRef();
}

// Release the acquired command before clearing its field. A missing command has no teardown effects.
ScAutoCommand::~ScAutoCommand() {
  if (command)
    command->Release();
  command = nullptr;
}

// Retain the source before releasing the old command, then reread the source after both possible callbacks.
// The original mutable receiver-reference result uses an explicit pointer under the project's reference policy.
ScAutoCommand *ScAutoCommand::operator=(const ScAutoCommand &source) {
  if (&source != this) {
    if (source.command)
      source.command->AddRef();
    if (command)
      command->Release();
    command = source.command;
  }
  return this;
}

bool ScAutoCommand::IsValid() const {
  return command != nullptr;
}

// Borrow the mutable command identity for the original direct member-call syntax.
UiCommandInterface *ScAutoCommand::operator->() {
  return command;
}

// Borrow the same command as a const input view without changing reference ownership.
const UiCommandInterface *ScAutoCommand::operator->() const {
  return command;
}

// Convert to the stored mutable command pointer without an extra query or retain.
ScAutoCommand::operator UiCommandInterface *() {
  return command;
}

// Convert a const scoped reference to its borrowed const command pointer.
ScAutoCommand::operator const UiCommandInterface *() const {
  return command;
}

bool ScAutoCommand::GetCmd(u32 iid, void **out) {
  return command ? command->QueryInterface(iid, out) : false;
}

// Original SC3U4FA86C is cSC3App::mpThis; the real application constructor publishes this borrowed pointer.
ApApplication *ApApplication::instance;

// Select the actual language-specific small or large filename; ordinary locales retain each supplied filename.
static const char *ApThemeFontName(i32 language, bool large, const char *ordinary) {
  switch (language) {
  case 15:
    return large ? "font16j.fon" : "font12j.fon";
  case 17:
    return "font16gh.fon";
  case 18:
    return "font16b5.fon";
  case 19:
    return "font20th.fon";
  case 20:
    return large ? "font16k.fon" : "font12k.fon";
  default:
    return ordinary;
  }
}

// Concatenate the real stored byte ranges without inserting or normalizing separators.
static void ApThemeFontPath(ScString *path, const ScString &text, const ScString &language, const char *filename) {
  ScString joined(text);
  joined.Append(language.ScString::Data(), language.ScString::Strlen());
  joined.Append(filename);
  *path = joined;
}

// Borrow the cache-held image after its scoped resource handle releases its temporary reference.
static GxBuffer *ApThemeImage(u32 instance) {
  const GxColorType format(GxColorType::Color::RGB16);
  GxBuffer *image;
  {
    UiImageHandle handle(instance, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = handle.GetBuffer();
  }
  return image;
}

// Initialize the UI provider, then install the original palette, localized fonts, images, cursors and dialog text.
bool ApApplication::InitUiTheme() {
  FtFontSystem *fonts = ApGetFontManager();
  WnResources *resources = ApGetUiResources();
  GxGraphics *graphics = ApGetGraphics();
  ScString path;
  if (!resources || !resources->Init())
    return false;
  ScString res(data_directory);
  res.Append("Res\\");
  ScString text(res);
  text.Append("Text\\");
  ScString sounds(res);
  sounds.Append("Sound\\Shared\\Sys\\");

  const struct {
    u32 identifier;
    u8 red, green, blue;
  } colors[] = {{kWnColorUiTransparency, 255, 0, 255}, {kWnColorUiBackground, 218, 221, 255},
      {kWnColorUiWhite, 255, 255, 255}, {21, 255, 255, 255}, {22, 26, 32, 107}, {23, 49, 54, 124}, {25, 255, 255, 255},
      {24, 160, 160, 160}, {kWnThemePanelFillColorId, 124, 144, 255}, {kWnThemeBorderColorId, 168, 180, 255},
      {kWnThemeBackgroundColorId, 218, 221, 255}, {kWnThemeOutlineMediumColorId, 108, 113, 185},
      {kWnThemeOutlineDarkColorId, 49, 54, 124}, {kWnThemeOutlineLightColorId, 179, 182, 224},
      {kWnThemeSecondaryMutedColorId, 125, 129, 189}, {kWnThemeTextColorId, 49, 54, 124},
      {kWnThemeMutedTextColorId, 125, 129, 181}, {kWnThemeFontColorId, 49, 54, 124}, {19, 127, 136, 255},
      {20, 255, 242, 255}};

  for (const auto &color : colors)
    resources->SetColor(color.identifier, color.red, color.green, color.blue);
  FtFont *system = nullptr;
  ScString language_directory;
  ScLanguageManager *first = ApGetLanguageManager(), *second = ApGetLanguageManager();
  second->GetLanguageDirectoryName(&language_directory, first->GetCurrentLanguage());
  language_directory.Append("\\");
  u32 color = resources->GetColor(kWnThemeFontColorId);
  i32 language = ApGetLanguageManager()->GetCurrentLanguage();
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, false, "system9.fbf"));
  FtFont *font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontDefault, font);
    resources->SetFont(kWnFontUiSystem, font);
    system = font;
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, false, "main10.fbf"));
  font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontEditor, font);
    resources->SetFont(kWnFontButton, font);
    resources->SetFont(kWnFontUiMain, font);
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, true, "Serif17.fbf"));
  font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontUiSerif, font);
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, false, "main9.fbf"));
  font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontUiSmall, font);
    resources->SetFont(kWnFontUiSmallAlias, font);
  } else {
    resources->SetFont(kWnFontUiSmall, system);
    resources->SetFont(kWnFontUiSmallAlias, system);
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, false, "title9.fbf"));
  font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontCaption, font);
    resources->SetFont(kWnFontUiTitle, font);
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, true, "systemloadsave.fbf"));
  font = fonts->FontAcquire(path);
  if (font) {
    font->SetColor(color);
    resources->SetFont(kWnFontUiFileDialog, font);
  }
  if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageThai || language == kScLanguageKorean) {
    ApThemeFontPath(&path, text, language_directory, "systemloadsave.fbf");
    font = fonts->FontAcquire(path);
    if (font) {
      font->SetColor(color);
      resources->SetFont(kWnFontUiFileDialogAlternate, font);
    }
  }
  ApThemeFontPath(&path, text, language_directory, ApThemeFontName(language, true, "title12.fbf"));
  font = fonts->FontAcquire(path);
  resources->SetFont(kWnFontUiHeadingAlias, font);
  resources->SetFont(kWnFontUiHeading, font);

  const struct {
    u32 instance, identifier;
  } kApThemePrimaryImages[] = {{kRsGenericWindowImageId, kRsGenericWindowImageId},
      {kRsDialogFrameAtlasImageId, kWnThemeDialogFrameImageId},
      {kRsMessageBoxButtonImageId, kWnThemeMessageBoxButtonImageId},
      {kApThemeHorizontalSliderArtworkId, kWnThemeHorizontalSliderImageId},
      {kApThemeSliderArtworkId, kWnThemeSliderImageId}, {kApThemeScrollbarArtworkId, kWnThemeScrollbarImageId}};

  for (const auto &entry : kApThemePrimaryImages) {
    GxBuffer *image = ApThemeImage(entry.instance);
    if (image)
      resources->SetImage(entry.identifier, image);
  }
  GxBuffer *image = ApThemeImage(kApThemeSpinnerArtworkId);
  if (image) {
    image->SetTransparency(graphics->ConvertRGBValueToNative16Bit(255, 0, 255));
    resources->SetImage(kWnThemeSpinnerImageId, image);
  }

  const struct {
    u32 instance, identifier;
  } kApThemeAdditionalImages[] = {{kApThemeComboArrowArtworkId, kWnThemeComboArrowImageId},
      {kApThemePopupBackgroundArtworkId, kWnThemePopupBackgroundImageId},
      {kApThemeGrayTextureArtworkId, kWnThemeGrayTextureImageId}, {kApThemeCheckBoxArtworkId, kWnThemeCheckBoxImageId}};

  for (const auto &entry : kApThemeAdditionalImages) {
    image = ApThemeImage(entry.instance);
    if (image)
      resources->SetImage(entry.identifier, image);
  }
  image = ApThemeImage(10);
  if (image) {
    image->SetTransparency(resources->GetColor(kWnColorUiTransparency));
    resources->SetImage(16, image);
  }
  WnCursorManager *cursors = ApGetDialogManager()->GetCursorManager();
  ScString cursor_directory(data_directory);
  cursor_directory.Append("res\\ui\\shared\\cursors\\");

  const struct {
    u32 identifier;
    const char *filename;
    bool remove;
  } cursor_files[] = {{0, "arrow.cur", true}, {14, "wait cursor.cur", true}, {2, "text edit cursor.cur", true},
      {15, "help arrow cursor.cur", true}, {16, "hand cursor.cur", false}, {17, "hand cursor.cur", false},
      {kSpBlankCursorId, "blank.cur", false}};

  for (const auto &entry : cursor_files) {
    if (entry.remove)
      cursors->RemoveCursor(entry.identifier);
    UiFileCursor *cursor;
    {
      ScString filename(cursor_directory);
      filename.Append(entry.filename);
      cursor = new UiFileCursor(filename);
    }
    cursors->AddCursor(cursor, entry.identifier);
  }
  UiTextHandle caption;

  const struct {
    u32 instance, identifier;
  } captions[] = {{192, kWnYesButtonId}, {194, kWnNoButtonId}, {180, kWnOkButtonId}, {182, kWnCancelButtonId},
      {276, kWnAbortButtonId}, {280, kWnIgnoreButtonId}, {278, kWnRetryButtonId}, {1606, kWnEllipsisTextId}};

  for (const auto &entry : captions) {
    {
      UiTextHandle loaded(entry.instance, kRsCommonTextGroup);
      caption = loaded;
    }
    if (caption.GetText())
      resources->SetText(entry.identifier, caption.GetText());
  }
  return true;
}

// Remove the six original application cursors in order, then shut down the control resource provider.
bool ApApplication::ShutdownUiTheme() {
  WnCursorManager *cursors = ApGetDialogManager()->GetCursorManager();
  cursors->RemoveCursor(0);
  cursors->RemoveCursor(14);
  cursors->RemoveCursor(2);
  cursors->RemoveCursor(15);
  cursors->RemoveCursor(16);
  cursors->RemoveCursor(17);
  ApGetUiResources()->Shutdown();
  return true;
}

// Register languages whose application string archive exists below the captured data-directory prefix.
// Reuse the name/path Strings across all43 lookups, retaining callback mutations even when a name lookup fails.
void ApApplication::FindAndRegisterAllAvailableLanguages() {
  ScLanguageManager *languages = ApGetLanguageManager();
  ScString prefix, name, path;
  prefix = data_directory;
  prefix.Append("Res\\Text\\");
  for (u32 language = 1; language < 44; ++language) {
    if (!languages->GetLanguageDirectoryName(&name, language))
      continue;
    path = prefix;
    path.Append(name.begin, name.ScString::Strlen());
    path.Append("\\");
    path.Append("SC3StringsApp.IXF");
    if (RsFile::FileExists(path))
      languages->AddAvailableLanguage(language);
  }
}

// Borrow the application's cached resource manager without acquiring a reference or performing a service lookup.
RsManager *ApApplication::GetResourceManager() {
  return resource_manager;
}
