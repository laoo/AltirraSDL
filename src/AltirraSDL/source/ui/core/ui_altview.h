//	AltirraSDL - alternate video output view ("Video Outputs")
//
//	Counterpart of the alt-output handling in Windows Altirra's
//	ATUIVideoDisplayWindow (uivideodisplaywindow.cpp): device video outputs
//	(XEP80, 1090, Bit 3, MARIA, ...) registered with IATDeviceVideoManager
//	can replace the computer's picture in the display area. The selection
//	is exposed through the ATUI*AltView*/ATUI*AltOutput* accessors declared
//	in uiaccessors.h; this header adds the entry points the SDL3 main loop
//	needs to present the selected output.

#pragma once

#include <vd2/system/vdtypes.h>

class IATDeviceVideoOutput;
class IATDeviceVideoManager;

struct ATUIAltViewFrame {
	// XRGB8888 pixels of the output's display area, valid until the next
	// ATUIAltViewPrepareFrame() call.
	const void *mpPixels = nullptr;
	int mWidth = 0;
	int mHeight = 0;
	int mPitch = 0;

	// True if the pixels changed since the previous prepared frame or the
	// output changed, i.e. the texture must be re-uploaded.
	bool mbChanged = false;

	// Signal state as reported by the device; when the signal is not
	// valid the display shows a black area with a message instead of
	// the frame.
	bool mbSignalValid = true;
	float mHorizScanRate = 0;
	float mVertScanRate = 0;

	// Layout hints from the device.
	double mPixelAspectRatio = 1.0;
	bool mbForceExactPixels = false;
};

// Hook simulator events (reset) and the video manager. Call once after
// g_sim.Init(); ATUIAltViewShutdown() before the simulator is torn down.
void ATUIAltViewInit();
void ATUIAltViewShutdown();

// Per presented frame: auto-switch to an output that became active.
void ATUIAltViewOnFrameTick();

IATDeviceVideoManager *ATUIAltViewGetVideoManager();

// Returns the device output currently selected for display, or null when
// the computer output is shown (also when the selected output's device
// has been removed).
IATDeviceVideoOutput *ATUIAltViewGetActiveOutput();

// Prepare the selected output's frame for the display backend. Returns
// false when the computer output should be shown instead (no selection,
// device missing, or the output passes the computer's signal through).
bool ATUIAltViewPrepareFrame(ATUIAltViewFrame& frame);

// Text to draw over a black display area when the selected output has no
// valid signal; returns false when the picture should be drawn normally.
bool ATUIAltViewGetBadSignalText(char *buf, size_t bufSize);
