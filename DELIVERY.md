# M3.2 delivery

Local branch: codex/m3-2-properties
Local commit: 4457b11505a275f8ba2d0e16b7610fb777ff5642 (NOT pushed)
Tracked source tree: ac0edf583a08afd4f1610ece314738660b6718b7

The full source ZIP includes M1/M2/M3.1 QML/M3.2. It excludes SDKs, build output, Git internals and fonts.
`PandoEditor-M3.2.patch` applies ONLY on the previous completed M3.1 QML source.
`PandoEditor-M3.2-from-remote-1aeb400.patch` includes BOTH the unpushed M3.1 QML work and M3.2.
Apply the cumulative patch ONLY on a clean source tree at remote commit
1aeb400e9738b518deef1c5c4806bfc76ad6a40a. Do NOT apply it directly to main.
The cumulative patch and incremental patch are alternatives, not sequential steps.
Both apply checks and full source tree equality were verified.

This delivery made no remote write attempt or remote commit. Remote integration remains separate.
Local full CTest: 23/23; property UI: 10 scenarios x desktop1100/mobile360.
Source property comparisons: 2330. Existing source selection comparisons: 2066.
ASan/UBSan selected suites: 8/8 with leak detection disabled.

Qt v4 output; v1-v4 input. Old applications cannot open v4.
The source browser DOM check was blocked by administrator policy.
Actual screen sampler, Windows native and Android builds/devices are NOT verified.
Offscreen screen_color_tests only verifies unsupported-host capability behavior.
Read docs/qt-property-implementation.md and the validation manifest for details.
