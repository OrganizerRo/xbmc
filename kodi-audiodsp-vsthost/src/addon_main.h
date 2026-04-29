#pragma once
/*
 * addon_main.h — Kodi ADSP addon entry point declarations
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 *
 * All required Kodi ADSP C callback functions are declared here.
 * Implementations are in addon_main.cpp.
 * The get_addon() function defined in kodi_adsp_dll.h wires them all.
 *
 * Global addon instance (one per addon load, shared across streams via
 * per-stream handles).  Per-stream state is stored in DSPProcessor objects
 * pointed to by ADDON_HANDLE::dataAddress.
 */

#include "kodi_adsp_dll.h"   // Kodi will find this via include path
#include "util/VSTLog.h"

// Called by CActiveAEDSP::Init() (via GetProcAddress) to route all addon log
// messages into kodi.log.  Must be exported from the DLL as a plain C symbol.
extern "C" void ADDON_SetLogCallback(VSTLogCallback_t cb);

// Called by CActiveAEDSP::Init() (via GetProcAddress) after ADDON_Create to
// retrieve the recovery-timer settings that were read from chain.json.
// delayMs and maxAttempts are set to the current globals; null pointers are ignored.
extern "C" void ADDON_GetRecoveryParams(int* delayMs, int* maxAttempts);

// DllAddon compatibility stubs — ADDON_GetTypeVersion is required (non-optional)
// by DllAddon::Load() via RESOLVE_METHOD_RENAME.  Returning the compiled DSP API
// version satisfies the resolver without introducing a false version mismatch.
extern "C" const char* ADDON_GetTypeVersion(int type);
extern "C" const char* ADDON_GetTypeMinVersion(int type);
