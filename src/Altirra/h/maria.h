//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

//=========================================================================
// MARIA — FPGA cartridge on the Parallel Bus Interface (device ID $08)
//
// This device emulates the MariaCEL sprite blitter module of the MARIA
// cartridge together with the parts of the host device that the blitter
// needs: the RAMMAP memory windows at $4000 and $8000, the 32 MB SDRAM
// behind them, and the PBIRAMBANK page at $DF00-$DFFF that carries the
// CEL palette. The blitter itself is the vendored MariaCEL reference
// model (source/mariacel/), driven from the emulated 6502 bus and from
// the ANTIC frame timing: the blitter's video output is synchronous with
// the computer's video output, so its ten 24-line strips are rendered
// against the same scanline counter as GTIA and its frame protocol
// (SNAP/prologue) turns over at the computer's vertical blank.
//
// The picture is presented as a separate video output (View > Video
// Outputs), 320x240 at 8 bits per pixel through the CEL palette.
//=========================================================================

#ifndef f_AT_MARIA_H
#define f_AT_MARIA_H

#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicepbi.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/scheduler.h>

class ATMemoryManager;
class ATMemoryLayer;
class ATSimulatorEventManager;
class ATSaveStateMaria;

namespace mcel {
	class Mcel;
}

class ATDeviceMaria final : public ATDevice
	, public IATPBIDevice
	, public IATDeviceMemMap
	, public IATDeviceScheduling
	, public IATDeviceVideoOutput
	, public IATDeviceSnapshot
	, public IATDeviceDiagnostics
	, public IATSchedulerCallback
{
	ATDeviceMaria(const ATDeviceMaria&) = delete;
	ATDeviceMaria& operator=(const ATDeviceMaria&) = delete;

public:
	ATDeviceMaria();
	~ATDeviceMaria();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	void WarmReset() override;

public:		// IATPBIDevice
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

public:		// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:		// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:		// IATDeviceVideoOutput
	const char *GetName() const override;
	const wchar_t *GetDisplayName() const override;
	void Tick(uint32 hz300ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override;
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pixelPos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override;

public:		// IATDeviceSnapshot
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

public:		// IATDeviceDiagnostics
	void DumpStatus(ATConsoleOutput& output) override;

public:		// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

private:
	enum : uint32 {
		kEventId_Strip = 1
	};

	// PBI device select bit in PDVS/PDVI. Fixed by the hardware.
	static constexpr uint8 kPBIDeviceId = 0x08;

	static constexpr uint32 kScreenWidth = 320;
	static constexpr uint32 kScreenHeight = 240;
	static constexpr uint32 kStripHeight = 24;
	static constexpr uint32 kStripCount = 10;

	// ANTIC scanline length in machine cycles; the blitter runs against
	// the same scanline counter as GTIA.
	static constexpr uint32 kCyclesPerScanline = 114;

	// First scanline of the computer's visible picture that carries line 0
	// of the 240-line MARIA picture.
	static constexpr uint32 kFirstDisplayScanline = 8;

	// Scanline at the end of which the computer's vertical blank starts
	// (kATSimEvent_VBLANK fires at the end of this line).
	static constexpr uint32 kVBlankScanline = 248;

	// Blitter core clock (docs/02-architecture.md §7).
	static constexpr double kBlitterClockHz = 135000000.0;

	// RAMMAP windows wired to the blitter: $4000 and $8000, 16 KB each.
	static constexpr uint32 kWindowSlots = 2;
	static constexpr uint32 kWindowBase[kWindowSlots] = { 0x4000, 0x8000 };
	static constexpr uint32 kWindowSize = 0x4000;

	// Bit 7 of the RAMMAP high byte selects a blitter BRAM block instead
	// of an SDRAM block (D53/D54).
	static constexpr uint8 kRAMMapBRAM = 0x80;

	// Host device registers on the $D1xx page that this device implements
	// (tests/vars.s of the MariaCEL project).
	static constexpr uint32 kRegMIRQControl	= 0xD111;
	static constexpr uint32 kRegBGEN		= 0xD14A;
	static constexpr uint32 kRegMEXTBNL		= 0xD14C;
	static constexpr uint32 kRegMEXTBNH		= 0xD14D;
	static constexpr uint32 kRegPBIRAMBank	= 0xD14F;
	static constexpr uint32 kRegRAMMap4000L	= 0xD1A8;
	static constexpr uint32 kRegRAMMap4000H	= 0xD1A9;
	static constexpr uint32 kRegRAMMap8000L	= 0xD1AA;
	static constexpr uint32 kRegRAMMap8000H	= 0xD1AB;
	static constexpr uint32 kRegRAMMapC000L	= 0xD1AC;
	static constexpr uint32 kRegRAMMapC000H	= 0xD1AD;
	static constexpr uint32 kRegCELCTL		= 0xD1B2;

	sint32 OnRegisterDebugRead(uint32 addr);
	sint32 OnRegisterRead(uint32 addr);
	bool OnRegisterWrite(uint32 addr, uint8 value);

	sint32 OnPBIRAMDebugRead(uint32 addr);
	sint32 OnPBIRAMRead(uint32 addr);
	bool OnPBIRAMWrite(uint32 addr, uint8 value);
	bool IsCELPaletteBankSelected() const;

	sint32 OnWindowDebugRead(uint32 addr);
	sint32 OnWindowRead(uint32 addr);
	bool OnWindowWrite(uint32 addr, uint8 value);

	void ResetDevice();
	void UpdateLayers();
	void UpdateTimingConfig();
	void OnVBlank();
	void RunStrip();
	void ApplyCPULoad(sint64 windowCycles);
	void UnscheduleStrips();

	uint8 ComposeStatus() const;
	bool IsWindowMapped(uint32 slot) const;
	uint32 GetWindowSDRAMAddress(uint32 slot, uint32 addr) const;
	void CountSDRAMAccess(uint32 sdramAddr);
	void UpdatePaletteEntry(uint32 index);
	void RebuildPalette();
	void ConvertStrip(uint32 strip);
	void ConvertAllStrips();

	mcel::Mcel *mpCel = nullptr;

	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	IATDevicePBIManager *mpPBIManager = nullptr;
	IATDeviceVideoManager *mpVideoManager = nullptr;
	ATSimulatorEventManager *mpSimEventManager = nullptr;
	uint32 mVBlankEventBinding = 0;
	ATEvent *mpEventStrip = nullptr;

	ATMemoryLayer *mpLayerRegisters = nullptr;
	ATMemoryLayer *mpLayerPBIRAM = nullptr;
	ATMemoryLayer *mpLayerWindow[kWindowSlots] {};

	bool mbSelected = false;

	// Raw RAMMAP latches: [slot][0] = low byte, [slot][1] = high byte.
	// Slot 2 is the $C000 window, which is latched for visibility but not
	// wired to the blitter or SDRAM.
	uint8 mRAMMap[3][2] {};

	// Host device latches. Only the reset (zero) state of these is
	// specified for the blitter subset; they are kept for diagnostics.
	// PBIRAMBANK and the CEL palette tables live in the model (D86).
	uint8 mMIRQControl = 0;
	uint8 mMEXTBNL = 0;
	uint8 mMEXTBNH = 0;
	uint8 mBGEN = 0;

	// Packed XRGB render palette derived from the model's CEL palette
	// tables; mbPaletteDirty requests a re-conversion of the whole picture
	// on the next UpdateFrame().
	uint32 mPalette[256] {};
	bool mbPaletteDirty = false;

	// Frame timing.
	uint32 mScanlineCount = 312;
	bool mbFrameActive = false;
	uint32 mNextStrip = 0;

	// 6502 accesses to SDRAM through the windows in the current blitter
	// window, fed to the model's bus contention injection (docs/11 §5).
	uint32 mWindowSDRAMHits = 0;
	uint32 mWindowFirstSDRAMAddr = 0;
	uint64 mTotalSDRAMHits = 0;

	// Video output. The activity counter follows the computer's writes to
	// the blitter (CTL, SPRLIST/TEXTAB through the windows, palette), not
	// the autonomous rendering, so auto-switching tracks the program that
	// drives the device rather than a picture that keeps refreshing after
	// the program is gone.
	VDPixmapBuffer mFrameBuffer;
	ATDeviceVideoInfo mVideoInfo {};
	uint32 mActivityCounter = 0;
	uint8 mIndexFrame[kScreenWidth * kScreenHeight] {};
	uint8 mStripScratch[kScreenWidth * kStripHeight] {};
};

#endif
