//	AltirraSDL - clean emulator frame capture helpers.

#include <stdafx.h>

#include <algorithm>
#include <cmath>
#include <string.h>

#include <vd2/system/math.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/resample.h>
#include <at/atcore/configvar.h>
#include <at/atcore/devicevideo.h>

#include "ui_frame_capture.h"
#include "simulator.h"
#include "gtia.h"
#include "devicemanager.h"
#include "ui_altview.h"

ATConfigVarFloat g_ATCVUISaveImageScale("ui.save_image.scale", 1.0f);

namespace {
	bool GetComputerFrame(ATSimulator& sim, ATUIFrameCaptureMode mode,
		VDPixmapBuffer& frameStorage, VDPixmap& frameView, double& par)
	{
		ATGTIAEmulator& gtia = sim.GetGTIA();

		if (mode == ATUIFrameCaptureMode::Raw) {
			float rawPAR = 1.0f;
			if (!gtia.GetLastFrameBufferRaw(frameStorage, frameView, rawPAR))
				return false;
			par = rawPAR;
		} else {
			if (!gtia.GetLastFrameBuffer(frameStorage, frameView))
				return false;
			par = gtia.GetPixelAspectRatio();
		}

		return true;
	}

	// Same source selection as ATUIVideoDisplayWindow::CopyFrameImage(): the
	// whole frame buffer of the device output with its pixel aspect ratio.
	void GetDeviceOutputFrame(IATDeviceVideoOutput& output, VDPixmap& frameView,
		double& par)
	{
		// The display path calls UpdateFrame() before presenting; do the
		// same so that a capture of an output that is not currently shown
		// still has its pending changes (e.g. palette writes) applied.
		output.UpdateFrame();

		frameView = output.GetFrameBuffer();
		par = output.GetVideoInfo().mPixelAspectRatio;
	}

	bool CaptureFrame(ATSimulator& sim, IATDeviceVideoOutput *output,
		ATUIFrameCaptureMode mode, VDPixmapBuffer& dst)
	{
		VDPixmapBuffer frameStorage;
		VDPixmap frameView {};
		double par = 1.0;

		// An output passing the computer's signal through shows the GTIA frame.
		if (output && output->GetVideoInfo().mbSignalPassThrough)
			output = nullptr;

		if (output)
			GetDeviceOutputFrame(*output, frameView, par);
		else if (!GetComputerFrame(sim, mode, frameStorage, frameView, par))
			return false;

		if (!frameView.data || frameView.w <= 0 || frameView.h <= 0)
			return false;

		dst.init(frameView.w, frameView.h, nsVDPixmap::kPixFormat_XRGB8888);
		VDPixmapBlt(dst, frameView);

		if (mode == ATUIFrameCaptureMode::Raw)
			return true;

		int sw = frameView.w;
		int sh = frameView.h;
		double dw = sw;
		double dh = sh;

		if (mode == ATUIFrameCaptureMode::TrueAspect) {
			if (par < 1.0) {
				dh *= 2;
				par *= 2;
			}

			dw *= par;
		} else {
			if (par < 0.75)
				dh *= 2;
			else if (par > 1.5)
				dw *= 2;
		}

		const double scale = std::clamp<double>(g_ATCVUISaveImageScale, 0.25, 4.0);
		dw *= scale;
		dh *= scale;

		const int iw = VDRoundToInt(dw);
		const int ih = VDRoundToInt(dh);

		if (iw != dst.w || ih != dst.h) {
			VDPixmapBuffer scaled(iw, ih, nsVDPixmap::kPixFormat_XRGB8888);

			if (mode == ATUIFrameCaptureMode::TrueAspect) {
				vdautoptr<IVDPixmapResampler> r(VDCreatePixmapResampler());

				r->SetFilters(IVDPixmapResampler::kFilterSharpLinear,
					IVDPixmapResampler::kFilterSharpLinear, false);
				r->SetSharpnessFactors(sqrt((float)dw / (float)sw),
					sqrtf((float)dh / (float)sh));

				const vdrect32f dstRect {
					(float)((iw - dw) * 0.5),
					(float)((ih - dh) * 0.5),
					(float)((iw + dw) * 0.5),
					(float)((ih + dh) * 0.5)
				};

				const vdrect32f srcRect {
					0.0f,
					0.0f,
					(float)frameView.w,
					(float)frameView.h
				};

				r->Init(dstRect, scaled.w, scaled.h, scaled.format,
					srcRect, dst.w, dst.h, dst.format);
				r->Process(scaled, dst);
			} else {
				VDPixmapStretchBltNearest(scaled, dst);
			}

			dst.swap(scaled);
		}

		return true;
	}
}

bool ATUICaptureEmulatorFrame(ATSimulator& sim, ATUIFrameCaptureMode mode,
	VDPixmapBuffer& dst)
{
	return CaptureFrame(sim, ATUIAltViewGetActiveOutput(), mode, dst);
}

bool ATUICaptureVideoOutputFrame(ATSimulator& sim, const char *outputName,
	ATUIFrameCaptureMode mode, VDPixmapBuffer& dst, VDStringA *error)
{
	if (!outputName || !*outputName || !strcmp(outputName, "display")) {
		if (ATUICaptureEmulatorFrame(sim, mode, dst))
			return true;

		if (error)
			*error = "no emulator frame is available";
		return false;
	}

	IATDeviceVideoOutput *output = nullptr;

	if (strcmp(outputName, "computer")) {
		ATDeviceManager *dm = sim.GetDeviceManager();
		IATDeviceVideoManager *vm = dm ? dm->GetService<IATDeviceVideoManager>() : nullptr;

		output = vm ? vm->GetOutputByName(outputName) : nullptr;

		if (!output) {
			if (error) {
				error->sprintf("unknown video output: %s (available: computer", outputName);

				const uint32 n = vm ? vm->GetOutputCount() : 0;
				for(uint32 i = 0; i < n; ++i) {
					if (IATDeviceVideoOutput *o = vm->GetOutput(i)) {
						*error += ", ";
						*error += o->GetName();
					}
				}

				*error += ')';
			}

			return false;
		}
	}

	if (CaptureFrame(sim, output, mode, dst))
		return true;

	if (error) {
		if (output)
			error->sprintf("video output %s has no frame", outputName);
		else
			*error = "no emulator frame is available";
	}

	return false;
}
