# M7.7 physical platform evidence

The machine-readable status is `docs/platform-validation.json`. Neither
desktop offscreen Qt tests nor a software scene graph run substitute for a
visible Windows or physical Android run.

| Platform | Current status | Required evidence |
|---|---|---|
| Windows | NOT RUN | OS/device/CPU/RAM/GPU, Qt version, QSG_INFO backend, D3D11 visible run, OpenGL smoke, software fallback, startup/flat/globe/pan/zoom/selection/labels/flags/hydro/distributions/edit/history/GIS/save/reopen, mouse/wheel/shortcuts/Korean IME/file dialogs, screenshots and logs. |
| Android | NOT RUN | Physical model/OS/RAM/GPU/DPR/backend, offline full-world startup, SAF open/save, tap/long press/pinch/pan/globe/chooser/sheets/Back/rotation/Korean IME, hydro/GIS access, edit, screenshots and logs. |

Each device run must attach its exact build and world asset versions. A
simulator or desktop QML touch emulation is separate evidence. The
`tools/collect-render-metrics.mjs` input requires measured frame samples and
device metadata; `docs/performance-budget.json` intentionally has no accepted
thresholds until those measurements and the user's acceptance.
