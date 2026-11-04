# Architecture

OpenSc3k reconstructs the original executable and its DLLs as modules inside **one** executable. The original DLL
boundaries survive as directories under `src/`, as separate director/factory registrations
(`src/application/builtin_modules.cpp`), and as the `.def` export lists kept beside each module.

## Modules

| Directory | Prefix | Original binaries |
|---|---|---|
| `application` | `Ap` | `SC3U.EXE`, `UV` |
| `common` | `Sc` | `GZServiceD`, `SIMVARIABLES` (services, strings, COM, threading, paths) |
| `resources` | `Rs` | `GZResourceD` (archives, INI, QFS, files) |
| `graphics`, `fonts` | `Gx`, `Ft` | `GZGraphicD`, `GIMEX` |
| `window`, `ui` | `Wn`, `Ui` | `GZWIND`, `GZTOOLSD`, `SIMUI` |
| `audio` | `Au` | `AUDIO`, `GZSOUNDD` |
| `city`, `economy`, `advisors`, `disasters` | `Ct`, `Ec`, `Ad`, `Ds` | `SIMCITY`, `SIMSERV`, `SIMINIT`, `SCENARIO`, `SIMECO`, `SIMRCI`, `SIMADV`, `SIMDSTR` |
| `terrain`, `geometry`, `sprites` | `Tr`, `Ge`, `Sp` | `SIMDIRT`, `SIMGEOM`, `SIMSPR` |
| `network` | `Nw` | `SIMNTWRK`, `SIMTRANSIT`, `STRTSIM` (roads, rails, power, water, vehicles) |
| `buildings` | `Bd` | `SIMBABLD`, `SIMUTIL` (building architect, power plants) |
| `misc`, `occupants`, `environment` | | `SIMMISC` and small remainders |

Directory prefixes are the two-letter class/function prefixes used throughout the code.

## Dependencies

The original DLLs call each other heavily, so several module pairs depend on each other in both directions
(`application`/`ui`, `city`/`ui`, `city`/`economy`). `common` should not include higher modules; the few exceptions
(`com.cpp`, `framework.cpp`, `value.cpp`, `array.cpp`, `services_module.cpp`) host services tied to the application.
`builtin_modules.cpp` in `application` is the composition root that registers every module's director.

## The unity build

`scripts/make_unity.py` includes every `src/**/*.cpp`, in the order listed in `scripts/unity_order.txt` (files
not listed follow in sorted order), into one generated translation unit (`<build>/game.cpp`) that is compiled once.
Consequences:

- Order matters. File-static helpers must be defined before use in that order, and dynamic static
  initializers run in that order.
- Large files are split into numbered parts (`window_controls.cpp`, `window_controls_2_*.cpp`, …). The
  numeric infix names the part; `unity_order.txt` keeps the parts adjacent and in order. Parts rely on helpers from earlier parts, so they do not
  compile alone.
- Private names must not collide across files; rename the private symbol rather than sharing it.

For a very fast check after editing, syntax-check the generated unit (a couple of seconds) before doing a full
build, for example:

```bash
c++ -DSC3_MONOLITHIC -DSC3_STATIC -Isrc -I<sdl-include> -std=c++23 -w -fsyntax-only build/release/game.cpp
```

## Conventions that matter when editing

- Keep original signatures, return types and ownership/callback order.
- Objects follow the original reference-counting model (`AddRef`/`Release`, `ScUnknown`); a function that returns
  an interface pointer through an output parameter hands the caller one reference.
- `ScNotImpl` marks genuinely missing reconstruction; it never stands in for original behavior.
- Interface, class, director, service and message identifiers are enums in `src/common/ids.h`; do not add raw hex IDs.
