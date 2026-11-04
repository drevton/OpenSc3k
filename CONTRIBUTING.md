# Contributing

- Use restricted C++23 and the module prefixes listed in `docs/architecture.md` (for example `Ct` city, `Nw` network,
  `Ui` ui); keep original function signatures and ownership order; format with `.clang-format`.
- Production code lives in `src/<module>/` and must not depend on emulator or comparison code.
- Keep each recovered method's original signature.
- Use assertions only for behavior the original leaves undefined; `ScNotImpl` marks genuinely
  missing reconstruction and must not return invented results.
- Build and run: see `README.md`. Keep related classes in one cohesive source file; avoid many tiny files.
