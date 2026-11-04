#!/usr/bin/env python3
"""Generate the single translation unit that compiles every game source: make_unity.py OUTPUT_DIRECTORY

Sources are included in the order listed in scripts/unity_order.txt (new files follow in sorted order), so
file-static helpers and dynamic initializers follow that order.
Each source is wrapped so macros it defines cannot leak into later sources, and Win32 SDK macros that would
rewrite game method names are cleared around the include.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
# Win32 macros that conflict with ordinary game names; cleared only around the affected source.
CLEAR_MACROS = {
    'src/ui/graph_series.cpp': {'near', 'far'},
    'src/window/controls.cpp': {'PlaySound'},
}


def source_order():
  existing = {path.relative_to(ROOT).as_posix() for path in (ROOT / 'src').rglob('*.cpp')}
  listed = [line.strip() for line in (ROOT / 'scripts/unity_order.txt').read_text().splitlines()
            if line.strip() and not line.startswith('#')]
  listed = [source for source in listed if source in existing]
  return listed + sorted(existing - set(listed))


def generate(directory):
  directory = Path(directory)
  directory.mkdir(parents=True, exist_ok=True)
  lines = ['// Generated from the executable source list.']
  for source in source_order():
    path = ROOT / source
    text = path.read_text(encoding='utf-8')
    macros = set(re.findall(r'^\s*#\s*(?:define|undef)\s+([A-Za-z_]\w*)', text, re.M))
    clear = CLEAR_MACROS.get(source, set())
    macros.update(clear)
    lines.extend(['#ifdef _MSC_VER', '#pragma warning(push)', '#endif', '#pragma pack(push)'])
    lines.extend(f'#pragma push_macro("{name}")' for name in sorted(macros))
    if '_CRT_SECURE_NO_WARNINGS' in macros:
      lines.append('#undef _CRT_SECURE_NO_WARNINGS')
    lines.extend(f'#undef {name}' for name in sorted(clear))
    lines.extend(['#undef LoadCursor', '#undef small', '#undef PlaySound', '#undef GetObject',
        f'#include "{path.as_posix()}"'])
    lines.extend(f'#undef {name}' for name in sorted(macros, reverse=True))
    lines.extend(f'#pragma pop_macro("{name}")' for name in sorted(macros, reverse=True))
    lines.extend(['#pragma pack(pop)', '#ifdef _MSC_VER', '#pragma warning(pop)', '#endif'])
  target = directory / 'game.cpp'
  text = '\n'.join(lines) + '\n'
  # Keep the timestamp stable when nothing changed so the compiler's dependency cache stays valid.
  if not target.exists() or target.read_text(encoding='utf-8') != text:
    target.write_text(text, encoding='utf-8', newline='\n')


if __name__ == '__main__':
  if len(sys.argv) != 2:
    sys.exit(__doc__)
  generate(sys.argv[1])
