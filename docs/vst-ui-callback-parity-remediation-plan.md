# audiodsp.VST UI Compatibility Remediation Plan

## Objective

Close the remaining VST2 editor-host compatibility gaps in `kodi-audiodsp-vsthost` so plugins that depend on legacy host callbacks can reliably open and manage their UIs on branch `copilot/investigate-vst-ui-issues`.

## External Guides Used

- `Arakula/vsthost` (behavioral reference host for legacy VST2 UI paths):
  - `CVSTHost.cpp` callback dispatch and `OnOpenWindow` / `OnCloseWindow` routing
  - `SmpVSTHost.cpp` secondary window lifecycle and update handling
- `Xaymar/vst2sdk` (opcode semantics and compatibility notes):
  - host opcode definitions for directory/update/language/legacy window calls
  - deprecation context for legacy opcodes in modern VST2.4 hosts

## Confirmed Current Gaps (Branch Scope)

In `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp`, `audioMaster()` currently handles core host opcodes (version, time, block/sample rate, sizeWindow, etc.) but does not handle these legacy UI-sensitive callbacks:

1. `audioMasterNeedIdle`
2. `audioMasterOpenWindow`
3. `audioMasterCloseWindow`
4. `audioMasterGetDirectory`
5. `audioMasterUpdateDisplay`

This is the primary compatibility gap behind “some UIs do not load” behavior.

## Remediation Workstreams

### Workstream 1 — Callback Parity in VSTPlugin2 Host Dispatcher

**Target files**
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp`
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.h`
- `kodi-audiodsp-vsthost/src/vst2/vestige/aeffectx.h` (if ABI declarations are missing/incomplete)

**Plan**
- Add explicit handling in `VSTPlugin2::audioMaster()` for all five missing opcodes.
- Keep behavior compatible with existing host contract and thread model in this addon.
- Preserve current handling for already-implemented opcodes and existing logging.
- Keep unsupported/unknown opcodes on the existing default path with debug logging.

**Acceptance criteria**
- No missing-case fallthrough for the five opcodes above.
- Each handled opcode returns deterministic, spec-aligned values.
- Existing opcode behavior remains unchanged unless directly related to these five cases.

### Workstream 2 — Legacy Window Lifecycle Support (Secondary Windows)

**Target files**
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp/.h`
- `kodi-audiodsp-vsthost/src/bridge/EditorBridge.cpp/.h` (only where host/window coordination is required)

**Plan**
- Implement host-side routing for plugin requests to open/close additional windows.
- Distinguish primary editor open flow (`effEditOpen`) from plugin-requested auxiliary windows.
- Mirror VSTHost-style behavior at the contract level: host owns window creation/closure decisions and returns valid handles.
- Ensure window bookkeeping supports repeated open/close cycles and cleans up on editor close/unload.

**Acceptance criteria**
- Plugins requesting auxiliary windows no longer fail silently.
- No leaked host windows or orphaned handles after close/unload.
- Primary editor path remains functional for plugins that do not use auxiliary windows.

### Workstream 3 — Idle and Display Update Compatibility

**Target files**
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp/.h`
- `kodi-audiodsp-vsthost/src/bridge/EditorBridge.cpp` (timer/refresh integration as needed)

**Plan**
- Honor `audioMasterNeedIdle` in a way compatible with existing `idleEditor()` timer-driven updates.
- Implement `audioMasterUpdateDisplay` so plugin refresh requests trigger a safe host-side UI refresh path.
- Ensure the callback path does not block audio processing and avoids unsafe cross-thread UI calls.

**Acceptance criteria**
- Plugins requiring idle signaling no longer depend on undefined host behavior.
- UI refresh requests are observed and logged through a defined path.
- No regressions in current timer-based idle pumping.

### Workstream 4 — Plugin Directory Callback Support

**Target files**
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp/.h`

**Plan**
- Provide `audioMasterGetDirectory` response compatible with VSTHost expectations for plugin asset lookup.
- Guarantee lifetime/ownership semantics for returned directory data are stable for plugin use.
- Use the loaded plugin path already tracked by the host to derive directory consistently.

**Acceptance criteria**
- Plugins that resolve skin/resource files relative to plugin location can obtain a valid directory pointer.
- No dangling-pointer or temporary-buffer return behavior.

### Workstream 5 — Safety, Concurrency, and ABI Guardrails

**Target files**
- `kodi-audiodsp-vsthost/src/vst2/VSTPlugin2.cpp/.h`
- `kodi-audiodsp-vsthost/src/vst2/vestige/aeffectx.h`
- `kodi-audiodsp-vsthost/src/bridge/EditorBridge.cpp/.h` (if synchronization changes are needed)

**Plan**
- Validate that callback handling remains safe when invoked before/after editor open, during unload, and during chain teardown.
- Keep host↔plugin pointer ABI stable (`VstWindow` and related legacy structs/opcodes).
- Ensure cross-thread handoff is explicit where UI-thread execution is required.
- Retain SEH protections where plugin callbacks are known crash points.

**Acceptance criteria**
- No new races or invalid-window-handle usage introduced by callback additions.
- No ABI mismatch between dispatcher and vestige headers.

## Validation Plan

### 1) Build/Static Validation
- Build `kodi-audiodsp-vsthost` on Windows toolchain currently used by this branch.
- Confirm no signature/enum mismatch warnings in VST2 callback paths.

### 2) Runtime Functional Matrix
- Plugin group A: editor opens via plain `effEditOpen` only (baseline sanity).
- Plugin group B: plugin requests legacy host window operations.
- Plugin group C: plugin requires idle/update callbacks for responsive UI.
- Plugin group D: plugin loads editor assets from plugin-relative paths.

For each group:
- Open editor, interact, resize, close, reopen.
- Verify no blank editor, no orphan windows, no crashes.
- Confirm callback hit logs in `VSTLOG`.

### 3) Regression Coverage
- Verify existing working VST2 UIs still behave identically.
- Verify no adverse effect on DSP processing path.
- Verify editor open/close IPC flow in `EditorBridge` remains stable.

## Logging and Diagnostics Plan

- Add/retain structured `VSTLOG_DEBUG/INFO/WARN` lines for each newly handled opcode.
- Log window open/close requests with plugin name and resulting handle.
- Log directory callback returns at debug level (without exposing sensitive filesystem details beyond required path).
- Keep unknown opcode logging in place to discover additional plugin expectations.

## Rollout Strategy

1. Land callback parity changes first (minimal behavioral extension).
2. Land window lifecycle support and cleanup handling.
3. Land idle/update refinements.
4. Run full functional/regression matrix.
5. If regressions appear, gate optional legacy behaviors behind conservative checks while preserving core fixes.

## Done Criteria

This plan is complete when:

- All five missing legacy UI callbacks are implemented and validated.
- Affected plugins that previously showed missing/blank UI now open and function.
- No regressions are introduced for already-working VST2 plugins.
- Logs demonstrate deterministic handling for legacy callbacks and safe fallback for unknown opcodes.
