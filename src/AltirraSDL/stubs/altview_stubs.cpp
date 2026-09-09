//	AltirraSDL - "Video Outputs" alt-view stubs for the headless bridge server
//
//	The SDL3 frontend implements these in source/ui/core/ui_altview.cpp;
//	the bridge server has no display, so the selection is held but never
//	shown.

#include <stdafx.h>
#include <string.h>
#include <vd2/system/VDString.h>
#include "uiaccessors.h"

static VDStringA s_altOutputName;
const char *ATUIGetCurrentAltOutputName() { return s_altOutputName.c_str(); }
void ATUISetCurrentAltOutputName(const char *s) { s_altOutputName = s ? s : ""; }
void ATUIToggleAltOutput(const char *) {}
bool ATUIIsAltOutputAvailable() { return false; }

bool ATUIIsXEPViewEnabled() { return false; }
void ATUISetXEPViewEnabled(bool) {}

static bool s_altViewEnabled = false;
bool ATUIGetAltViewEnabled() { return s_altViewEnabled; }
void ATUISetAltViewEnabled(bool v) { s_altViewEnabled = v; }

sint32 ATUIGetCurrentAltViewIndex() { return -1; }
void ATUISetAltViewByIndex(sint32) {}
void ATUISelectPrevAltOutput() {}
void ATUISelectNextAltOutput() {}

static bool s_altViewAutoSwitch = true;
bool ATUIGetAltViewAutoswitchingEnabled() { return s_altViewAutoSwitch; }
void ATUISetAltViewAutoswitchingEnabled(bool v) { s_altViewAutoSwitch = v; }

