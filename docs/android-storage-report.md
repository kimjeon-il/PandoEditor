# Android storage and shared UI report

Date: 2026-09-17

## Result

Tasks 2 and 3 from `android-plan.md` are implemented in the shared Qt application. The existing desktop open/save flow remains local-file based, while Android mode uses one atomically written private project plus Storage Access Framework URLs for import and export. The implementation does not request broad storage permission and does not treat `content:` URLs as local paths.

## Files

- `app/platformstorage.h`, `app/platformstorage.cpp`
  - 64 MiB bounded reads for seekable and unknown-length streams.
  - `QSaveFile` atomic writes for desktop/local and private app storage.
  - Android `content:` reads/writes through Qt's `QFile` content-URI engine. Content export is a checked stream write, not `QSaveFile`.
  - Default private project at `QStandardPaths::AppDataLocation/current.pando.json`.
  - Non-destructive `.corrupt`, `.corrupt.1`, ... backup creation before corrupt-project overwrite is explicitly allowed.
- `app/editorcontroller.h`, `app/editorcontroller.cpp`
  - Production `EditorControllerConfig` selects mobile mode and optionally supplies a private path for normal integration tests.
  - Explicit post-QML restore, import, private save, saved-snapshot export and corrupt-file recovery flow.
  - Import decode, validation and projection are completed before replacing live document state.
  - Imported documents remain independently dirty until a successful private atomic save.
  - Failed/canceled import preserves the document, drafts and undo history; canceled URLs emit no error.
  - Failed export leaves the already-saved private snapshot clean and intact.
- `ui/common/Main.qml`, `ui/common/EditorPanel.qml`, `ui/desktop/DesktopWorkspace.qml`
  - Mobile labels: `가져오기`, `기기에 저장`, `내보내기`.
  - Mobile minimum window dimensions removed; five toolbar controls fit at 360 px.
  - The existing scrollable editor panel is retained for compact/landscape and keyboard-resized layouts.
  - Back first hides the IME, closes editor ComboBox popups or owned dialogs, then uses the existing save/discard/cancel close guard.
  - Responsive layout changes do not recreate the controller, so draft/selection state survives resize/rotation.
  - Restore is deferred with `Qt.callLater` after QML `Connections` are established.
  - Corrupt restore opens a visible recovery confirmation; confirmation first copies the damaged bytes to a unique backup.
- `tests/storage_tests.cpp`, `tests/ui_tests.cpp`
  - Real temporary-file coverage for atomic/private storage, corrupt preservation, failed saves/exports, restore, dirty state and canceled/failed imports.
  - Synthetic sequential-device coverage for unknown-length 64 MiB enforcement and mid-stream read failure.
  - Offscreen QML coverage for mobile labels, 360 px fit, rotation state, save-before-export, import-cancel history, ComboBox Back behavior and shared unsaved-close guard.
- `app/CMakeLists.txt`
  - Adds platform storage sources, `storage_tests`, and the Android packaging include owned by the integration task.

## TDD evidence

The feature was developed as RED/GREEN cycles:

1. Storage/controller RED: `storage_tests` compilation failed at `platformstorage.h: No such file or directory` before the adapter existed.
2. Storage/controller GREEN: the new storage tests compiled and passed after the adapter/controller implementation.
3. Mobile UI RED: `mobileStorageFlow` reported `Actual window->minimumWidth(): 360; Expected: 0` before the Android-responsive QML branch.
4. Stream-error RED: `boundedReadDoesNotAcceptTruncatedDataAfterStreamError` reported that no `std::runtime_error` was thrown before switching from `QByteArray QIODevice::read()` to checked `qint64` reads.
5. Import-cancel RED: `mobileStorageFlow` reported `!editor.canUndo() returned FALSE` because opening the import flow committed a draft before the chooser; the mobile import path now preserves drafts/history until a real import succeeds.
6. Final GREEN verification is recorded below.

## Verification

- Desktop source/build directory: `C:/Users/taeeu/Qt/Pandoeditor-build`.
- Final verification command: `cmake --build ...` followed by full `ctest --output-on-failure` (final counts to be recorded after the last build).
- Focused mobile flow: `UiTests::mobileStorageFlow` passed with 3 passed, 0 failed; its 360 px capture is `C:/Users/taeeu/Qt/Pandoeditor-build/mobile-storage.png`.
- Android native compile: the x86_64 shared library linked after replacing the initial raw-JNI content stream with Qt's content-URI file engine. Final APK/package result belongs to the integration task and is recorded there.

## Remaining runtime risks

- Emulator execution is unavailable on this host because acceleration is not installed (`HypervisorPresent` is false). No Windows features were changed and no reboot was attempted.
- Real Android SAF provider behavior, provider-specific truncate/flush semantics, touch interaction, IME resize/dismissal, physical Back dispatch, rotation/activity preservation and process relaunch restore remain unverified on-device. Desktop tests cover the shared logic and QML branches, not Android framework delivery of those events.
- The corrupt-file backup flow is desktop-tested with real files; Android app-private filesystem behavior is compile-verified but not device-run.
- Full-world map data remains excluded by plan.
