# Android first-run plan

Scope approved in chat: Qt 6.8.3, one Android 35 x86_64 emulator/debug APK, shared C++ core and v2 project format. Full world map is last and excluded. Do not touch the old web repository, commit, push, change global PATH/Java, enable Windows features, or reboot.

## Tasks

1. Install private JDK 17 and exact Qt Android toolchain dependencies; add reproducible environment/build/install scripts. Preserve free disk space; check emulator acceleration without changing Windows configuration.
2. Add a Qt storage adapter and mobile controller flow with test-first coverage. Windows local atomic save unchanged. Android imports streams (content URIs), saves one private project atomically, restores on startup, exports after successful internal save. Failed/canceled imports preserve document/drafts/history; imported data remains dirty until internal save. Preserve corrupt internal files and report failure without silently overwriting them.
3. Adapt shared QML: import/device save/export labels, no desktop minimum size on Android, responsive keyboard layout, Back dismisses keyboard/popups before unsaved close guard.
4. Regress desktop/core; build APK; run emulator only if acceleration available. Record actual evidence separately from pending runtime checks (SAF, touch/rotation/IME/back/relaunch).

## Execution record

- Baseline 2026-09-17: desktop CTest 3/3 passed.
- Existing repo has no commits, all initial files untracked. User explicitly selected this directory as the chat workspace; work in place, without committing or creating a second project/worktree.
- Storage/UI implementation and toolchain setup have independent file ownership. Build integration is serialized after implementation.
- Platform runtime checks may be blocked by missing hypervisor; APK build still proceeds, as agreed.
- Environment installed: Qt Android 6.8.3 x86_64, Temurin 17.0.20.1+1, SDK tools 22.0, NDK r26b, SDK/build-tools 35. Emulator acceleration reports missing hypervisor driver (no settings changed).
- Work is on unborn `android-prototype` branch; initial files preserved, no commits.
- Android CMake configure succeeded. First cross-compile caught an Android-only typed JNI context error; assigned to storage implementer before rebuilding. Build-tools version now set on target (Qt 6.8.3 variable alone generated an empty deployment value).
- Storage switched from handwritten JNI streams to the Qt content-URI file engine, with explicit read/write error checks. Second Android native compile/link passed; Gradle packaging is running.
- API35 default x86_64 image revision 2 installed and `Pandoeditor_API35` AVD created/listed. avdmanager printed a missing optional devices.xml warning but returned 0 and generated Pixel 5 configuration; boot is still unverified due acceleration blocker.
- Core-only build/test regression: 1/1 passed. Script parser and unavailable-acceleration guard verified; persistent user PATH unchanged.
