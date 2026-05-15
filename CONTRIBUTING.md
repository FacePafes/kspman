# contributing to kspman
(for whatever reason youd waste your time)

---

## rules
- **keep it small**: no massive libraries.
- **no secrets**: all bugs, including security issues, are handled publicly in the issues tab full transparency . 
- **no background services**: the app only runs when the user opens it.

---

## how to help
1. **bug reports**: open an issue. be specific about what happened supply as much information you can.
2. **feature requests**: if it adds bloat or background automation, the answer is probably no in that state. open an issue to discuss it first.
3. **pull requests**: 
   - explain exactly what you changed.
   - helping optimise the C logic. 
   - if you find a spot where `_wfopen` or `W` functions are missing, that's a priority.
   - update the build command in the readme/release if you add new linker flags.

---

## building
use gcc (mingw-w64). the current build command (as of v0.2 release) is:
```bash
gcc -o kspman.exe kspman.c -mwindows -lcomctl32 -lwinhttp -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -ladvapi32 -s
