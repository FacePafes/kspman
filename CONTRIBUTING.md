# contributing to kspman
(for whatever reason youd waste your time)

---

## rules
- **keep it small**: no massive libraries.
- **no secrets**: all bugs are handled publicly in the issues tab for full transparency.
- **security exceptions**: if you discover a critical vulnerability that could cause severe harm (e.g: directory traversal via downloads, RCE, or privilege escalation), do not open a public issue. Email me with the email on my profile. this is only for the "worse of the worse" any none critical reports sent via email will be redirected to the public issues tab.

---

## before you start
if you have are unsure of a idea or a new feature, open an issue or discussion first so less time is wasted

---

## how to help
1. **bug reports**: open an issue. be specific about what happened and supply as much information as you can.
2. **feature requests**: if it adds bloat or background automation, the answer is probably no in that state. open an issue to discuss it first.
3. **pull requests**: 
   - explain exactly what you changed.
   - helping optimise the C logic. 
   - if you find a spot where `_wfopen` or `W` functions are missing, that's a priority.
   - update the build command in the readme/release if you add new linker flags.

---

## building
use gcc (mingw-w64). the current build command (as of v0.3) is:
```bash
gcc -o kspman.exe kspman.c -mwindows -lcomctl32 -lwinhttp -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -ladvapi32 -s
