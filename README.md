# schemereload

Small TF2 (64-bit) DLL for HUD editing. After you inject it, `hud_reloadscheme` reloads everything:

| What | Stock `hud_reloadscheme` | With schemereload |
|---|---|---|
| `.res` layouts, `hudanimations`, `hud_textures.txt` | yes | yes |
| `ClientScheme.res` / `SourceScheme.res` + their `#base` files (colors, fonts, borders) | **no** | yes |
| `.vtf` / `.vmt` images under `tf\custom\<any>\materials` (also the `.vtf` page of a `.vbf` bitmap font) | **no** | yes |
| Localization files (`chat_english.txt`, `tf_english.txt`, ...) | **no** | yes |
| Main menu, top bar, GameUI dialogs pick up the changes | **no** (only the in-game HUD is reapplied) | yes |

No restart needed for scheme or image edits.

## Use it

1. Add `-insecure` to TF2's launch options (Steam > TF2 > Properties). This turns VAC off. Do **not** inject into a VAC-secured game.
2. Start TF2. Any time works (boot, main menu, in a map).
3. Inject `schemereload.dll` into `tf_win64.exe` with any x64 LoadLibrary injector, e.g. System Informer (right-click process > Miscellaneous > Inject DLL) or Xenos64.
4. Open the console. You should see:
   `[schemereload] loaded (schemes ok, materials ok, strings ok, panels ok). hud_reloadscheme now reloads everything.`
5. Edit your HUD files in `tf\custom\<yourhud>\`, then type `hud_reloadscheme`. Each run prints one line per scheme, then a summary:
   ```
   [schemereload]   resource/ClientScheme.res (ClientScheme): reloaded
   [schemereload]   resource/SourceScheme.res (SourceScheme): reloaded
   [schemereload] 2 schemes, 37 materials, strings reloaded, all panels reapplied (850 ms)
   ```
   If something doesn't update, send these lines.

Tip: `bind F5 hud_reloadscheme`.

## Build

Needs Visual Studio 2022 or VS Build Tools 2022, with the "Desktop development with C++" workload. Nothing else.

- **Easiest:** double-click `build.bat`. Output: `build\schemereload.dll`.
- **CMake:** run `cmake -B build -A x64`, then `cmake --build build --config Release`. Output: `build\Release\schemereload.dll`.

## Dependencies

There are no third-party libraries. Everything the code uses comes with Visual Studio:

| Used | From | Why |
|---|---|---|
| `<windows.h>`, `kernel32.lib` | Windows SDK (installed with the C++ workload) | `CreateThread`, `GetModuleHandleA`, `GetProcAddress`, `GetModuleFileNameA`, `GetTickCount`, `Sleep` |
| `<filesystem>`, `<string>`, `<vector>`, `<cstdio>`, `<cstring>`, `<cstdarg>` | MSVC C++17 standard library | walk `tf\custom\*\materials`, string handling, console text |
| Static CRT (`/MT`) | MSVC | baked into the DLL, so no `vcruntime140.dll` is needed |

At runtime it only talks to DLLs TF2 has already loaded, by name: `vstdlib.dll` (console commands), `vgui2.dll` (schemes, strings), `materialsystem.dll` (images), `tier0.dll` (console print). TF2 SDK headers aren't needed, because the vtable slots and offsets are hardcoded at the top of `schemereload.cpp`. The built DLL imports only `KERNEL32.dll`.

## Notes

- **How it works.** vgui never rereads a scheme it has already loaded. So the DLL hides the old scheme's name for a moment, makes vgui load the same file again, then swaps the old and new data in place. Panels keep their handle and get the fresh fonts, colors and borders. The old data stays in memory, because live panels may still point at it. After that it triggers the same refresh as a resolution change (to the size you already have): every font gets rebuilt and every panel reapplies its settings, including the main menu. Stock `hud_reloadscheme` only reapplies the in-game HUD.
- **Side effects of the full refresh.** Open in-game menus such as the scoreboard or class select close, the same as after changing resolution. The main menu stays.
- **Memory.** Each reload keeps the old scheme data in memory (a few KB), and its fonts get rebuilt on every refresh, so reloads slowly get slower. Restart TF2 after a few hundred reloads.
- **Images** are reread only if the game has already loaded them. A brand-new file shows up once something uses it. Edits inside a `.vpk` are not picked up, so use a loose folder while editing.
- **Not reloaded:** `.ttf` font files (Windows keeps them locked until TF2 restarts), the `.vbf` glyph layout itself (its `.vtf` image does reload), and sounds (type `snd_restart` yourself).
- **Slower path.** The game also runs `hud_reloadscheme` on its own when `cl_hud_minmode` changes, so that toggle now does the full reload too.
- **Unload.** If your injector can unload (FreeLibrary), the command goes back to stock. Otherwise restart TF2.
- **If a TF2 update breaks it:** the console shows `MISSING`, `NOT hooked`, or `-1 schemes`. The vtable slots and offsets are listed at the top of `schemereload.cpp`.
