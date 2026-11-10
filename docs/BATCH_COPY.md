<!-- Recent Screenshot Batch Copy: additive feature added on branch feature/recent-batch-copy. The upstream LICENSE (MIT, (c) 2025 JYAARU) and all notices are unchanged; the new files are offered under the same MIT terms. -->

# Recent Screenshot Batch Copy

Copies your latest saved screenshots to the Windows clipboard as **one file list** (CF_HDROP, the same thing Explorer's Ctrl+C puts there), so a single Ctrl+V pastes several independent image files where the target supports it. Images are never read, merged, stitched or re-encoded, and the originals are never modified.

## Use
1. Settings -> **Batch Copy** -> turn on **Batch Copy Master Switch** (OFF by default). Or tray -> **Batch Copy** -> **Enable Batch Copy**.
2. Make sure **Auto-save screenshots** is on (Screenshot settings). Batch copy reads the files jietuba saves (default `Pictures\jietuba_photos`). If it is off, the Batch Copy page, the window and the copy result say so; **nothing is changed for you**.
3. Take screenshots, press **Ctrl+Shift+V** (default). The latest N (default 10) files are on the clipboard; paste with Ctrl+V.
4. Other entry points: tray -> Batch Copy -> Copy Latest 5/10/15/20, Copy Selected, Recent Screenshots...; optional extra hotkeys (empty by default) on the settings page; the **Recent Screenshots** window (thumbnails, newest first, select by hand or "Latest N", copy).

## Master switch behaviour
Switch OFF: no folder watcher, no hotkey registered, every copy request refused without touching the clipboard. jietuba's own capture, clipboard history and hotkeys are not started, stopped or changed by this feature. Independent switches: monitoring, hotkeys, notifications. "Windows startup" is jietuba's own setting (Other page): this feature lives inside jietuba and starts with it.

## Source layout
- `cpp/src/batch/recent_screenshots.*` — newest-first file selection (skips `*_original` copies, empty and hidden files).
- `cpp/src/batch/batch_clipboard.*` — CF_HDROP + Preferred DropEffect=COPY.
- `cpp/src/batch/batch_copy_controller.*` — master switch, watcher, hotkey bindings, conflict detection.
- `cpp/src/batch/batch_window.*` — Recent Screenshots window.
- `cpp/src/ui/settings_ui/page_batch.cpp` — settings page.
- Hooks: `main_app.*` (create controller, register hotkeys in `update_hotkey`, open window), `ui/tray_menu.cpp`, `settings_ui/dialog.cpp`/`pages.h` (page 13), `settings/app_settings.inc` (13 `batch/*` rows), CMake test target, translations (en, zh, zh_TW, ja, ko).
- Tests: `cpp/tests/test_batch.cpp`.

## Limits
- Only files jietuba saves (or files in extra folders) are listed; Win+Shift+S / Print Screen images from other tools appear only if they are saved into a watched folder.
- Whether a paste target accepts several files at once depends on that application.
- The hotkey is intercepted globally while active (Ctrl+Shift+V is "paste as plain text" in some applications).
