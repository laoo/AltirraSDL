//	AltirraSDL - clean emulator frame capture helpers.

#pragma once

#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmaputils.h>

class ATSimulator;

enum class ATUIFrameCaptureMode {
	Display,
	TrueAspect,
	Raw,
};

// Captures the picture currently shown in the display area, as Windows
// Altirra's ATUIVideoDisplayWindow::CopyFrameImage() does: the selected
// device video output (View > Video Outputs, e.g. XEP80 or MARIA) when one
// is shown, otherwise the computer's (GTIA) frame. Used by Save Frame,
// Copy Frame, the debugger's .autotest image commands and test mode.
bool ATUICaptureEmulatorFrame(ATSimulator& sim, ATUIFrameCaptureMode mode,
	VDPixmapBuffer& dst);

// Captures a specific video output regardless of what is displayed:
//   "computer"  - the computer's (GTIA) frame
//   "display"   - same as ATUICaptureEmulatorFrame()
//   other       - the device video output with that internal name
//                 (IATDeviceVideoOutput::GetName(), e.g. "xep80", "maria")
// On failure returns false and, if error is non-null, sets a message.
bool ATUICaptureVideoOutputFrame(ATSimulator& sim, const char *outputName,
	ATUIFrameCaptureMode mode, VDPixmapBuffer& dst, VDStringA *error = nullptr);

