//	AltirraSDL - alternate video output view ("Video Outputs")
//
//	See ui_altview.h. The selection semantics mirror Windows Altirra
//	(uivideodisplaywindow.cpp): the selection is a device output name so
//	it survives device re-plugging and profile switches, "enabled" means a
//	name is selected even if the device is currently absent, auto-switching
//	follows the outputs' activity counters and is cancelled by a reset.

#include <stdafx.h>
#include <string.h>
#include <vd2/system/VDString.h>
#include <vd2/system/function.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixmapops.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/notifylist.h>
#include "devicemanager.h"
#include "simulator.h"
#include "simeventmanager.h"
#include "uiaccessors.h"
#include "ui_altview.h"

extern ATSimulator g_sim;

namespace {
	VDStringA g_altViewName;
	bool g_altViewIsXEP = false;

	// Windows default (main.cpp: g_xepViewAutoswitchingEnabled = false).
	bool g_altViewAutoswitch = false;

	uint32 g_altViewWarmResetBinding = 0;
	uint32 g_altViewColdResetBinding = 0;
	bool g_altViewRemovingHooked = false;
	vdfunction<void(uint32)> g_altViewRemovingFn;

	// Conversion state for the presented frame.
	IATDeviceVideoOutput *g_pAltViewLastOutput = nullptr;
	uint32 g_altViewLastLayoutChangeCount = 0;
	uint32 g_altViewLastChangeCount = 0;
	bool g_altViewForceUpload = true;
	VDPixmapBuffer g_altViewConvertBuffer;

	// Signal state of the last prepared frame, for the overlay.
	bool g_altViewShowing = false;
	bool g_altViewSignalValid = true;
	float g_altViewHorizScanRate = 0;
	float g_altViewVertScanRate = 0;

	IATDeviceVideoOutput *ResolveOutput() {
		if (g_altViewName.empty())
			return nullptr;

		IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

		return mgr ? mgr->GetOutputByName(g_altViewName.c_str()) : nullptr;
	}

	void OnReset() {
		// Matches ATUIVideoDisplayWindow::OnReset(): a reset returns to the
		// computer output when auto-switching, and re-arms the activity
		// detection so the output is picked up again when it restarts.
		if (g_altViewAutoswitch)
			ATUISetAltViewEnabled(false);

		if (IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager())
			mgr->CheckForNewlyActiveOutputs();
	}
}

///////////////////////////////////////////////////////////////////////////
// uiaccessors.h

const char *ATUIGetCurrentAltOutputName() {
	return g_altViewName.c_str();
}

void ATUISetCurrentAltOutputName(const char *name) {
	if (!name)
		name = "";

	if (g_altViewName != name) {
		g_altViewName = name;
		g_altViewIsXEP = !strcmp(name, "xep80");
		g_altViewForceUpload = true;
	}
}

void ATUIToggleAltOutput(const char *name) {
	if (!strcmp(ATUIGetCurrentAltOutputName(), name)) {
		ATUISetCurrentAltOutputName("");
	} else {
		IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

		if (mgr && mgr->GetOutputByName(name))
			ATUISetCurrentAltOutputName(name);
	}
}

bool ATUIIsAltOutputAvailable() {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	return mgr && mgr->GetOutputCount() > 0;
}

bool ATUIIsXEPViewEnabled() {
	return g_altViewIsXEP;
}

void ATUISetXEPViewEnabled(bool enabled) {
	ATUIToggleAltOutput("xep80");
}

bool ATUIGetAltViewEnabled() {
	return !g_altViewName.empty();
}

void ATUISetAltViewEnabled(bool enabled) {
	if (enabled == ATUIGetAltViewEnabled())
		return;

	if (enabled)
		ATUISelectNextAltOutput();
	else
		ATUISetCurrentAltOutputName("");
}

sint32 ATUIGetCurrentAltViewIndex() {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	return mgr ? mgr->IndexOfOutput(ResolveOutput()) : -1;
}

void ATUISetAltViewByIndex(sint32 idx) {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();
	IATDeviceVideoOutput *next = (mgr && idx >= 0) ? mgr->GetOutput((uint32)idx) : nullptr;

	if (next)
		ATUISetCurrentAltOutputName(next->GetName());
	else
		ATUISetCurrentAltOutputName("");
}

void ATUISelectPrevAltOutput() {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	if (!mgr)
		return;

	const sint32 idx = mgr->IndexOfOutput(ResolveOutput());

	ATUISetAltViewByIndex(idx < 0 ? (sint32)mgr->GetOutputCount() - 1 : idx - 1);
}

void ATUISelectNextAltOutput() {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	if (!mgr)
		return;

	ATUISetAltViewByIndex(mgr->IndexOfOutput(ResolveOutput()) + 1);
}

bool ATUIGetAltViewAutoswitchingEnabled() {
	return g_altViewAutoswitch;
}

void ATUISetAltViewAutoswitchingEnabled(bool enabled) {
	g_altViewAutoswitch = enabled;
}

///////////////////////////////////////////////////////////////////////////
// ui_altview.h

void ATUIAltViewInit() {
	ATSimulatorEventManager *sem = g_sim.GetEventManager();

	if (sem) {
		if (!g_altViewWarmResetBinding)
			g_altViewWarmResetBinding = sem->AddEventCallback(kATSimEvent_WarmReset, [] { OnReset(); });

		if (!g_altViewColdResetBinding)
			g_altViewColdResetBinding = sem->AddEventCallback(kATSimEvent_ColdReset, [] { OnReset(); });
	}

	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	if (mgr && !g_altViewRemovingHooked) {
		// Matches ATUIVideoDisplayWindow::OnRemovingVideoOutput(): unplugging
		// the selected device returns to the computer output.
		g_altViewRemovingFn = [](uint32 index) {
			IATDeviceVideoManager *mgr2 = ATUIAltViewGetVideoManager();
			IATDeviceVideoOutput *output = mgr2 ? mgr2->GetOutput(index) : nullptr;

			if (output && g_altViewName == output->GetName())
				ATUISetCurrentAltOutputName("");

			if (g_pAltViewLastOutput == output)
				g_pAltViewLastOutput = nullptr;
		};

		mgr->OnRemovingOutput().Add(&g_altViewRemovingFn);
		g_altViewRemovingHooked = true;
	}
}

void ATUIAltViewShutdown() {
	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	if (mgr && g_altViewRemovingHooked) {
		mgr->OnRemovingOutput().Remove(&g_altViewRemovingFn);
		g_altViewRemovingHooked = false;
	}

	ATSimulatorEventManager *sem = g_sim.GetEventManager();

	if (sem) {
		if (g_altViewWarmResetBinding) {
			sem->RemoveEventCallback(g_altViewWarmResetBinding);
			g_altViewWarmResetBinding = 0;
		}

		if (g_altViewColdResetBinding) {
			sem->RemoveEventCallback(g_altViewColdResetBinding);
			g_altViewColdResetBinding = 0;
		}
	}

	g_pAltViewLastOutput = nullptr;
	g_altViewConvertBuffer.clear();
}

void ATUIAltViewOnFrameTick() {
	if (!g_altViewAutoswitch)
		return;

	IATDeviceVideoManager *mgr = ATUIAltViewGetVideoManager();

	if (!mgr)
		return;

	const sint32 idx = mgr->CheckForNewlyActiveOutputs();

	if (idx >= 0) {
		IATDeviceVideoOutput *output = mgr->GetOutput((uint32)idx);

		if (output)
			ATUISetCurrentAltOutputName(output->GetName());
	}
}

IATDeviceVideoManager *ATUIAltViewGetVideoManager() {
	ATDeviceManager *dm = g_sim.GetDeviceManager();

	return dm ? dm->GetService<IATDeviceVideoManager>() : nullptr;
}

IATDeviceVideoOutput *ATUIAltViewGetActiveOutput() {
	return ResolveOutput();
}

bool ATUIAltViewPrepareFrame(ATUIAltViewFrame& frame) {
	IATDeviceVideoOutput *output = ResolveOutput();

	if (!output) {
		g_pAltViewLastOutput = nullptr;
		g_altViewShowing = false;
		return false;
	}

	output->UpdateFrame();

	const ATDeviceVideoInfo& vi = output->GetVideoInfo();

	if (vi.mbSignalPassThrough) {
		// The device passes the computer's picture through its own output.
		g_pAltViewLastOutput = nullptr;
		g_altViewShowing = false;
		return false;
	}

	g_altViewShowing = true;
	g_altViewSignalValid = vi.mbSignalValid;
	g_altViewHorizScanRate = vi.mHorizScanRate;
	g_altViewVertScanRate = vi.mVertScanRate;

	frame.mbSignalValid = vi.mbSignalValid;
	frame.mHorizScanRate = vi.mHorizScanRate;
	frame.mVertScanRate = vi.mVertScanRate;
	frame.mPixelAspectRatio = vi.mPixelAspectRatio;
	frame.mbForceExactPixels = vi.mbForceExactPixels;

	bool changed = g_altViewForceUpload
		|| output != g_pAltViewLastOutput
		|| vi.mFrameBufferLayoutChangeCount != g_altViewLastLayoutChangeCount
		|| vi.mFrameBufferChangeCount != g_altViewLastChangeCount;

	if (changed) {
		g_altViewForceUpload = false;
		g_pAltViewLastOutput = output;
		g_altViewLastLayoutChangeCount = vi.mFrameBufferLayoutChangeCount;
		g_altViewLastChangeCount = vi.mFrameBufferChangeCount;

		const VDPixmap& fb = output->GetFrameBuffer();

		// Only the display area of the frame buffer is shown, as on Windows.
		vdrect32 area = vi.mDisplayArea;

		if (area.left < 0) area.left = 0;
		if (area.top < 0) area.top = 0;
		if (area.right > fb.w) area.right = fb.w;
		if (area.bottom > fb.h) area.bottom = fb.h;

		const int w = std::max<int>(1, area.width());
		const int h = std::max<int>(1, area.height());

		if (g_altViewConvertBuffer.w != w || g_altViewConvertBuffer.h != h || g_altViewConvertBuffer.format != nsVDPixmap::kPixFormat_XRGB8888)
			g_altViewConvertBuffer.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);

		if (fb.data && area.width() > 0 && area.height() > 0)
			VDPixmapBlt(g_altViewConvertBuffer, 0, 0, fb, area.left, area.top, area.width(), area.height());
		else
			memset(g_altViewConvertBuffer.data, 0, (size_t)g_altViewConvertBuffer.pitch * h);

		// The backends read the alpha channel on some paths; keep it opaque.
		for(int y = 0; y < h; ++y) {
			uint32 *row = (uint32 *)((char *)g_altViewConvertBuffer.data + g_altViewConvertBuffer.pitch * y);

			for(int x = 0; x < w; ++x)
				row[x] |= 0xFF000000;
		}
	}

	frame.mpPixels = g_altViewConvertBuffer.data;
	frame.mWidth = g_altViewConvertBuffer.w;
	frame.mHeight = g_altViewConvertBuffer.h;
	frame.mPitch = (int)g_altViewConvertBuffer.pitch;
	frame.mbChanged = changed;

	return true;
}

bool ATUIAltViewGetBadSignalText(char *buf, size_t bufSize) {
	if (!g_altViewShowing || g_altViewSignalValid)
		return false;

	if (g_altViewHorizScanRate > 0 && g_altViewVertScanRate > 0)
		snprintf(buf, bufSize, "Unsupported video mode\n%.3fKHz, %.1fHz", g_altViewHorizScanRate / 1000.0f, g_altViewVertScanRate);
	else
		snprintf(buf, bufSize, "No signal");

	return true;
}
