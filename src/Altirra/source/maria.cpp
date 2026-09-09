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

#include <stdafx.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/serialization.h>
#include <at/atcore/snapshotimpl.h>
#include "antic.h"
#include "maria.h"
#include "memorymanager.h"
#include "pbi.h"
#include "simeventmanager.h"
#include "simulator.h"
#include "mariacel/mcel.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceMaria(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceMaria> p(new ATDeviceMaria);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMaria = {
	"maria",
	nullptr,
	L"MARIA",
	ATCreateDeviceMaria,
	kATDeviceDefFlag_RebootOnPlug
};

///////////////////////////////////////////////////////////////////////////

class ATSaveStateMaria final : public ATSnapExchangeObject<ATSaveStateMaria, "ATSaveStateMaria"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	// blitter BRAM blocks and SDRAM
	vdrefptr<ATSaveStateMemoryBuffer> mpSDRAM;
	vdrefptr<ATSaveStateMemoryBuffer> mpSprList;
	vdrefptr<ATSaveStateMemoryBuffer> mpTexTab;
	vdrefptr<ATSaveStateMemoryBuffer> mpDerived;
	vdrefptr<ATSaveStateMemoryBuffer> mpStripMap;
	vdrefptr<ATSaveStateMemoryBuffer> mpStepFrame;

	// strip buffers (word-addressed BRAM) and the picture assembled from
	// the strips scanned out so far
	vdfastvector<uint32> mStripWords;
	vdrefptr<ATSaveStateMemoryBuffer> mpFrame;

	// blitter architectural and internal state
	uint8 mCtl = 0;
	bool mbSnapReq = true;
	bool mbOverrun = false;
	bool mbFrameOdd = false;
	sint16 mVPX = 0;
	sint16 mVPY = 0;
	uint8 mColA = 0;
	uint8 mColB = 0;
	uint8 mColBMax = 0;
	uint8 mColStatus = 0;
	sint32 mStepNext = 0;
	sint32 mStepLast = -1;
	bool mbStepUnread = false;
	uint32 mCPUProbe = 0;

	// SDRAM controller bank state (timing model)
	sint32 mSDRAMOpenRow[4] { -1, -1, -1, -1 };
	bool mbSDRAMLastWrite = false;
	bool mbSDRAMHaveDirection = false;

	// host device latches
	uint8 mRAMMap[6] {};
	uint8 mMIRQControl = 0;
	uint8 mMEXTBNL = 0;
	uint8 mMEXTBNH = 0;
	uint8 mBGEN = 0;
	uint8 mPBIRAMBank = 0;
	uint8 mPaletteR[256] {};
	uint8 mPaletteG[256] {};
	uint8 mPaletteB[256] {};

	// PBI selection (PDVS bit of this device). The PBI manager's select
	// register is not part of the simulator save state, so the device
	// restores its own bit.
	bool mbSelected = false;

	// frame sequencing
	bool mbFrameActive = false;
	uint32 mNextStrip = 0;
	uint32 mStripEventDelay = 0;
	uint32 mWindowSDRAMHits = 0;
	uint32 mWindowFirstSDRAMAddr = 0;
	uint32 mActivityCounter = 0;
};

template<ATExchanger T>
void ATSaveStateMaria::Exchange(T& ex) {
	ex.Transfer("sdram", &mpSDRAM);
	ex.Transfer("bram_sprlist", &mpSprList);
	ex.Transfer("bram_textab", &mpTexTab);
	ex.Transfer("bram_derived", &mpDerived);
	ex.Transfer("bram_stripmap", &mpStripMap);
	ex.Transfer("step_frame", &mpStepFrame);
	ex.Transfer("strip_words", &mStripWords);
	ex.Transfer("frame", &mpFrame);

	ex.Transfer("arch_ctl", &mCtl);
	ex.Transfer("int_snap_req", &mbSnapReq);
	ex.Transfer("arch_overrun", &mbOverrun);
	ex.Transfer("int_frame_odd", &mbFrameOdd);
	ex.Transfer("arch_vpx", &mVPX);
	ex.Transfer("arch_vpy", &mVPY);
	ex.Transfer("arch_col_a", &mColA);
	ex.Transfer("arch_col_b", &mColB);
	ex.Transfer("arch_col_bmax", &mColBMax);
	ex.Transfer("arch_col_status", &mColStatus);
	ex.Transfer("int_step_next", &mStepNext);
	ex.Transfer("int_step_last", &mStepLast);
	ex.Transfer("int_step_unread", &mbStepUnread);
	ex.Transfer("int_cpu_probe", &mCPUProbe);

	ex.TransferArray("int_sdram_open_row", mSDRAMOpenRow);
	ex.Transfer("int_sdram_last_write", &mbSDRAMLastWrite);
	ex.Transfer("int_sdram_have_direction", &mbSDRAMHaveDirection);

	ex.TransferArray("arch_rammap", mRAMMap);
	ex.Transfer("arch_mirq_control", &mMIRQControl);
	ex.Transfer("arch_mextbnl", &mMEXTBNL);
	ex.Transfer("arch_mextbnh", &mMEXTBNH);
	ex.Transfer("arch_bgen", &mBGEN);
	ex.Transfer("arch_pbirambank", &mPBIRAMBank);
	ex.TransferArray("arch_cel_palette_r", mPaletteR);
	ex.TransferArray("arch_cel_palette_g", mPaletteG);
	ex.TransferArray("arch_cel_palette_b", mPaletteB);

	ex.Transfer("arch_pbi_selected", &mbSelected);
	ex.Transfer("int_frame_active", &mbFrameActive);
	ex.Transfer("int_next_strip", &mNextStrip);
	ex.Transfer("int_strip_event_delay", &mStripEventDelay);
	ex.Transfer("int_window_sdram_hits", &mWindowSDRAMHits);
	ex.Transfer("int_window_first_sdram_addr", &mWindowFirstSDRAMAddr);
	ex.Transfer("int_activity_counter", &mActivityCounter);

	if constexpr (T::IsReader) {
		if (mNextStrip > (uint32)mcel::STRIPS)
			throw ATInvalidSaveStateException();

		if (mStepNext < 0 || mStepNext >= (sint32)mcel::STRIPS)
			throw ATInvalidSaveStateException();

		if (mStepLast < -1 || mStepLast >= (sint32)mcel::STRIPS)
			throw ATInvalidSaveStateException();
	}
}

template void ATSaveStateMaria::Exchange(ATSerializer&);
template void ATSaveStateMaria::Exchange(ATDeserializer&);

///////////////////////////////////////////////////////////////////////////

ATDeviceMaria::ATDeviceMaria() {
}

ATDeviceMaria::~ATDeviceMaria() {
	Shutdown();
}

void *ATDeviceMaria::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceMemMap::kTypeID:			return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceScheduling::kTypeID:		return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceVideoOutput::kTypeID:		return static_cast<IATDeviceVideoOutput *>(this);
		case IATDeviceSnapshot::kTypeID:		return static_cast<IATDeviceSnapshot *>(this);
		case IATDeviceDiagnostics::kTypeID:		return static_cast<IATDeviceDiagnostics *>(this);
	}

	return ATDevice::AsInterface(iid);
}

void ATDeviceMaria::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMaria;
}

void ATDeviceMaria::Init() {
	if (!mpMemMan)
		mpMemMan = GetService<ATMemoryManager>();

	mpPBIManager = GetService<IATDevicePBIManager>();
	mpVideoManager = GetService<IATDeviceVideoManager>();

	// Host register page. The device only answers on the bus while it is
	// selected through PDVS, so all layers start disabled and are gated by
	// SelectPBIDevice().
	ATMemoryHandlerTable regHandlers {};
	regHandlers.mbPassReads = true;
	regHandlers.mbPassAnticReads = true;
	regHandlers.mbPassWrites = true;
	regHandlers.mpThis = this;
	regHandlers.BindDebugReadHandler<&ATDeviceMaria::OnRegisterDebugRead>();
	regHandlers.BindReadHandler<&ATDeviceMaria::OnRegisterRead>();
	regHandlers.BindWriteHandler<&ATDeviceMaria::OnRegisterWrite>();

	mpLayerRegisters = mpMemMan->CreateLayer(kATMemoryPri_PBI, regHandlers, 0xD1, 0x01);
	mpMemMan->SetLayerName(mpLayerRegisters, "MARIA registers");

	// PBIRAMBANK page at $DF00-$DFFF, inside the PBI ROM overlay region.
	ATMemoryHandlerTable pbiRAMHandlers {};
	pbiRAMHandlers.mbPassReads = true;
	pbiRAMHandlers.mbPassAnticReads = true;
	pbiRAMHandlers.mbPassWrites = true;
	pbiRAMHandlers.mpThis = this;
	pbiRAMHandlers.BindDebugReadHandler<&ATDeviceMaria::OnPBIRAMDebugRead>();
	pbiRAMHandlers.BindReadHandler<&ATDeviceMaria::OnPBIRAMRead>();
	pbiRAMHandlers.BindWriteHandler<&ATDeviceMaria::OnPBIRAMWrite>();

	mpLayerPBIRAM = mpMemMan->CreateLayer(kATMemoryPri_PBI, pbiRAMHandlers, 0xDF, 0x01);
	mpMemMan->SetLayerName(mpLayerPBIRAM, "MARIA PBIRAMBANK page");

	// RAMMAP windows. A mapped window always answers, for the CPU and for
	// ANTIC alike, so these layers never pass accesses through.
	ATMemoryHandlerTable windowHandlers {};
	windowHandlers.mbPassReads = false;
	windowHandlers.mbPassAnticReads = false;
	windowHandlers.mbPassWrites = false;
	windowHandlers.mpThis = this;
	windowHandlers.BindDebugReadHandler<&ATDeviceMaria::OnWindowDebugRead>();
	windowHandlers.BindReadHandler<&ATDeviceMaria::OnWindowRead>();
	windowHandlers.BindWriteHandler<&ATDeviceMaria::OnWindowWrite>();

	static constexpr const char *kWindowNames[kWindowSlots] = {
		"MARIA RAMMAP $4000 window",
		"MARIA RAMMAP $8000 window",
	};

	for(uint32 slot = 0; slot < kWindowSlots; ++slot) {
		mpLayerWindow[slot] = mpMemMan->CreateLayer(kATMemoryPri_PBI, windowHandlers, kWindowBase[slot] >> 8, kWindowSize >> 8);
		mpMemMan->SetLayerName(mpLayerWindow[slot], kWindowNames[slot]);
	}

	mFrameBuffer.init(kScreenWidth, kScreenHeight, nsVDPixmap::kPixFormat_XRGB8888);

	mVideoInfo = {};
	mVideoInfo.mbSignalValid = true;
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mFrameBufferLayoutChangeCount = 1;
	mVideoInfo.mFrameBufferChangeCount = 0;
	mVideoInfo.mTextRows = 0;
	mVideoInfo.mTextColumns = 0;
	mVideoInfo.mPixelAspectRatio = 1.0;
	mVideoInfo.mDisplayArea = vdrect32(0, 0, kScreenWidth, kScreenHeight);
	mVideoInfo.mBorderColor = 0;
	mVideoInfo.mbForceExactPixels = false;

	ResetDevice();

	mpPBIManager->AddDevice(this);
	mpVideoManager->AddVideoOutput(this);

	mpSimEventManager = g_sim.GetEventManager();
	mVBlankEventBinding = mpSimEventManager->AddEventCallback(kATSimEvent_VBLANK,
		[this] { OnVBlank(); });
}

void ATDeviceMaria::Shutdown() {
	if (mpSimEventManager) {
		if (mVBlankEventBinding) {
			mpSimEventManager->RemoveEventCallback(mVBlankEventBinding);
			mVBlankEventBinding = 0;
		}

		mpSimEventManager = nullptr;
	}

	UnscheduleStrips();

	if (mpVideoManager) {
		mpVideoManager->RemoveVideoOutput(this);
		mpVideoManager = nullptr;
	}

	if (mpPBIManager) {
		mpPBIManager->RemoveDevice(this);
		mpPBIManager = nullptr;
	}

	if (mpMemMan) {
		mpMemMan->DeleteLayerPtr(&mpLayerRegisters);
		mpMemMan->DeleteLayerPtr(&mpLayerPBIRAM);

		for(ATMemoryLayer *& layer : mpLayerWindow)
			mpMemMan->DeleteLayerPtr(&layer);

		mpMemMan = nullptr;
	}

	delete mpCel;
	mpCel = nullptr;

	mpScheduler = nullptr;
}

void ATDeviceMaria::ColdReset() {
	// The cartridge is powered from the computer, so a cold reset is a
	// power cycle: BRAM and SDRAM come up zeroed and every latch is cleared.
	// PDVS is cleared by the PBI manager, which deselects the device.
	ResetDevice();
}

void ATDeviceMaria::WarmReset() {
	// The blitter has no reset input besides the CTL.RESET strobe, and the
	// host latches survive a system reset. The PBI manager clears PDVS on
	// warm reset, which deselects the device and hides the windows until the
	// program selects it again.
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = kPBIDeviceId;
	devInfo.mbHasIrq = false;
}

void ATDeviceMaria::SelectPBIDevice(bool enable) {
	if (mbSelected == enable)
		return;

	mbSelected = enable;
	UpdateLayers();
}

bool ATDeviceMaria::IsPBIOverlayActive() const {
	// Only the $DF00-$DFFF page of the math pack region is replaced while
	// the device is selected; the OS math pack stays visible below it.
	return false;
}

uint8 ATDeviceMaria::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return busData;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDeviceMaria::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	switch(index) {
		case 0:
			lo = kRegMIRQControl;
			hi = kRegMIRQControl + 1;
			return true;

		case 1:
			lo = kRegBGEN;
			hi = kRegPBIRAMBank + 1;
			return true;

		case 2:
			lo = kRegRAMMap4000L;
			hi = kRegRAMMapC000H + 1;
			return true;

		case 3:
			lo = kRegCELCTL;
			hi = kRegCELCTL + 1;
			return true;

		case 4:
			lo = 0xDF00;
			hi = 0xE000;
			return true;

		default:
			return false;
	}
}

void ATDeviceMaria::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

///////////////////////////////////////////////////////////////////////////

const char *ATDeviceMaria::GetName() const {
	return "maria";
}

const wchar_t *ATDeviceMaria::GetDisplayName() const {
	return L"MARIA";
}

void ATDeviceMaria::Tick(uint32 hz300ticks) {
	// The blitter is clocked from the scanline timing of the computer, not
	// from the 300Hz tick.
}

void ATDeviceMaria::UpdateFrame() {
	if (mbPaletteDirty) {
		mbPaletteDirty = false;

		ConvertAllStrips();
		++mVideoInfo.mFrameBufferChangeCount;
	}
}

const VDPixmap& ATDeviceMaria::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATDeviceMaria::GetVideoInfo() {
	return mVideoInfo;
}

vdpoint32 ATDeviceMaria::PixelToCaretPos(const vdpoint32& pixelPos) {
	return vdpoint32(0, 0);
}

vdrect32 ATDeviceMaria::CharToPixelRect(const vdrect32& r) {
	return r;
}

int ATDeviceMaria::ReadRawText(uint8 *dst, int x, int y, int n) {
	return 0;
}

uint32 ATDeviceMaria::GetActivityCounter() {
	return mActivityCounter;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		ResetDevice();
		return;
	}

	const ATSaveStateMaria& mstate = atser_cast<const ATSaveStateMaria&>(*state);

	// Start from power-on state and overlay the saved state on it. The PBI
	// selection is owned by the PBI manager and is not touched here.
	ResetDevice();

	mcel::Mcel& cel = *mpCel;

	const auto copyBuffer = [](void *dst, size_t dstSize, const ATSaveStateMemoryBuffer *buf) {
		if (!buf)
			return;

		const auto& src = buf->GetReadBuffer();
		memcpy(dst, src.data(), std::min<size_t>(dstSize, src.size()));
	};

	copyBuffer(cel.sdram_.mem_.data(), cel.sdram_.mem_.size(), mstate.mpSDRAM);
	copyBuffer(cel.sprlist_, sizeof cel.sprlist_, mstate.mpSprList);
	copyBuffer(cel.textab_, sizeof cel.textab_, mstate.mpTexTab);
	copyBuffer(cel.derived_ram_, sizeof cel.derived_ram_, mstate.mpDerived);
	copyBuffer(cel.stripmap_, sizeof cel.stripmap_, mstate.mpStripMap);
	copyBuffer(cel.step_frame_, sizeof cel.step_frame_, mstate.mpStepFrame);
	copyBuffer(mIndexFrame, sizeof mIndexFrame, mstate.mpFrame);

	{
		uint32 *stripWords = &cel.sb_.w_[0][0][0];
		const size_t stripWordCount = sizeof cel.sb_.w_ / sizeof stripWords[0];
		const size_t n = std::min<size_t>(stripWordCount, mstate.mStripWords.size());

		if (n)
			memcpy(stripWords, mstate.mStripWords.data(), n * sizeof stripWords[0]);
	}

	cel.ctl_ = mstate.mCtl & (mcel::CTL_ENABLE | mcel::CTL_NODETECT);
	cel.snap_req_ = mstate.mbSnapReq;
	cel.overrun_ = mstate.mbOverrun;
	cel.frame_odd_ = mstate.mbFrameOdd;
	cel.vpx_ = mstate.mVPX;
	cel.vpy_ = mstate.mVPY;
	cel.col_a_ = mstate.mColA;
	cel.col_b_ = mstate.mColB;
	cel.col_bmax_ = mstate.mColBMax;
	cel.col_status_ = mstate.mColStatus;
	cel.step_next_ = mstate.mStepNext;
	cel.step_last_ = mstate.mStepLast;
	cel.step_unread_ = mstate.mbStepUnread;
	cel.cpu_probe_ = mstate.mCPUProbe;

	for(int i = 0; i < 4; ++i)
		cel.sdram_.open_row_[i] = mstate.mSDRAMOpenRow[i];

	cel.sdram_.last_was_write_ = mstate.mbSDRAMLastWrite;
	cel.sdram_.have_direction_ = mstate.mbSDRAMHaveDirection;

	for(uint32 slot = 0; slot < 3; ++slot) {
		mRAMMap[slot][0] = mstate.mRAMMap[slot * 2 + 0];
		mRAMMap[slot][1] = mstate.mRAMMap[slot * 2 + 1];
	}

	// The blitter decodes the windows from its own copy of RAMMAP; only
	// the wired slots are forwarded, as in normal operation.
	for(uint32 slot = 0; slot < kWindowSlots; ++slot) {
		cel.rammap_[slot][0] = mRAMMap[slot][0];
		cel.rammap_[slot][1] = mRAMMap[slot][1];
	}

	mMIRQControl = mstate.mMIRQControl;
	mMEXTBNL = mstate.mMEXTBNL;
	mMEXTBNH = mstate.mMEXTBNH;
	mBGEN = mstate.mBGEN;

	cel.pbirambank_ = mstate.mPBIRAMBank;
	memcpy(cel.celpal_[0], mstate.mPaletteR, sizeof cel.celpal_[0]);
	memcpy(cel.celpal_[1], mstate.mPaletteG, sizeof cel.celpal_[1]);
	memcpy(cel.celpal_[2], mstate.mPaletteB, sizeof cel.celpal_[2]);
	RebuildPalette();

	mbFrameActive = mstate.mbFrameActive;
	mNextStrip = mstate.mNextStrip;
	mWindowSDRAMHits = mstate.mWindowSDRAMHits;
	mWindowFirstSDRAMAddr = mstate.mWindowFirstSDRAMAddr;
	mActivityCounter = mstate.mActivityCounter;

	UpdateTimingConfig();

	if (mbFrameActive && mNextStrip < kStripCount && mstate.mStripEventDelay)
		mpScheduler->SetEvent(mstate.mStripEventDelay, this, kEventId_Strip, mpEventStrip);

	ConvertAllStrips();
	++mVideoInfo.mFrameBufferChangeCount;

	UpdateLayers();

	// Re-establish the PDVS selection. The simulator cold resets before
	// loading device states, which clears the PBI select register; the
	// device's bit is restored here so a program that had selected MARIA
	// keeps its windows and registers after the load. Only the concrete
	// PBI manager exposes the select register (same shortcut as
	// pbidisk.cpp takes with the SIO manager).
	if (ATPBIManager *pbi = static_cast<ATPBIManager *>(mpPBIManager)) {
		const uint8 sel = pbi->GetSelectRegister();

		if (mstate.mbSelected)
			pbi->Select(sel | kPBIDeviceId);
		else
			pbi->Select(sel & ~kPBIDeviceId);
	}
}

vdrefptr<IATObjectState> ATDeviceMaria::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATSaveStateMaria };

	const mcel::Mcel& cel = *mpCel;

	const auto makeBuffer = [](const wchar_t *name, const void *src, size_t n) {
		vdrefptr buf { new ATSaveStateMemoryBuffer };

		buf->mpDirectName = name;
		buf->GetWriteBuffer().assign((const uint8 *)src, (const uint8 *)src + n);

		return buf;
	};

	state->mpSDRAM = makeBuffer(L"maria-sdram.bin", cel.sdram_.mem_.data(), cel.sdram_.mem_.size());
	state->mpSprList = makeBuffer(L"maria-sprlist.bin", cel.sprlist_, sizeof cel.sprlist_);
	state->mpTexTab = makeBuffer(L"maria-textab.bin", cel.textab_, sizeof cel.textab_);
	state->mpDerived = makeBuffer(L"maria-derived.bin", cel.derived_ram_, sizeof cel.derived_ram_);
	state->mpStripMap = makeBuffer(L"maria-stripmap.bin", cel.stripmap_, sizeof cel.stripmap_);
	state->mpStepFrame = makeBuffer(L"maria-stepframe.bin", cel.step_frame_, sizeof cel.step_frame_);
	state->mpFrame = makeBuffer(L"maria-frame.bin", mIndexFrame, sizeof mIndexFrame);

	{
		const uint32 *stripWords = &cel.sb_.w_[0][0][0];
		state->mStripWords.assign(stripWords, stripWords + sizeof cel.sb_.w_ / sizeof stripWords[0]);
	}

	state->mCtl = cel.ctl_;
	state->mbSnapReq = cel.snap_req_;
	state->mbOverrun = cel.overrun_;
	state->mbFrameOdd = cel.frame_odd_;
	state->mVPX = cel.vpx_;
	state->mVPY = cel.vpy_;
	state->mColA = cel.col_a_;
	state->mColB = cel.col_b_;
	state->mColBMax = cel.col_bmax_;
	state->mColStatus = cel.col_status_;
	state->mStepNext = cel.step_next_;
	state->mStepLast = cel.step_last_;
	state->mbStepUnread = cel.step_unread_;
	state->mCPUProbe = cel.cpu_probe_;

	for(int i = 0; i < 4; ++i)
		state->mSDRAMOpenRow[i] = cel.sdram_.open_row_[i];

	state->mbSDRAMLastWrite = cel.sdram_.last_was_write_;
	state->mbSDRAMHaveDirection = cel.sdram_.have_direction_;

	for(uint32 slot = 0; slot < 3; ++slot) {
		state->mRAMMap[slot * 2 + 0] = mRAMMap[slot][0];
		state->mRAMMap[slot * 2 + 1] = mRAMMap[slot][1];
	}

	state->mMIRQControl = mMIRQControl;
	state->mMEXTBNL = mMEXTBNL;
	state->mMEXTBNH = mMEXTBNH;
	state->mBGEN = mBGEN;
	state->mPBIRAMBank = cel.pbirambank();

	memcpy(state->mPaletteR, cel.celpal(0), sizeof state->mPaletteR);
	memcpy(state->mPaletteG, cel.celpal(1), sizeof state->mPaletteG);
	memcpy(state->mPaletteB, cel.celpal(2), sizeof state->mPaletteB);

	state->mbSelected = mbSelected;
	state->mbFrameActive = mbFrameActive;
	state->mNextStrip = mNextStrip;
	state->mStripEventDelay = mpEventStrip ? (uint32)mpScheduler->GetTicksToEvent(mpEventStrip) : 0;
	state->mWindowSDRAMHits = mWindowSDRAMHits;
	state->mWindowFirstSDRAMAddr = mWindowFirstSDRAMAddr;
	state->mActivityCounter = mActivityCounter;

	return state;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::DumpStatus(ATConsoleOutput& output) {
	const mcel::Mcel& cel = *mpCel;

	output("MARIA (MariaCEL blitter, PBI ID $%02X): %s", kPBIDeviceId, mbSelected ? "selected" : "not selected");
	output("CTL/STATUS ($%04X): ENABLE=%d NODETECT=%d SNAP=%d OVERRUN=%d",
		kRegCELCTL,
		cel.enabled() ? 1 : 0,
		cel.nodetect() ? 1 : 0,
		cel.snap() ? 1 : 0,
		cel.overrun() ? 1 : 0);

	static constexpr const char *kBRAMBlockNames[] = {
		"SPRLIST", "TEXTAB", "DERIVED", "STRIPMAP", "STRIP"
	};

	static constexpr const char *kSlotNames[] = { "$4000", "$8000", "$C000" };

	for(uint32 slot = 0; slot < 3; ++slot) {
		const uint8 lo = mRAMMap[slot][0];
		const uint8 hi = mRAMMap[slot][1];

		if (slot >= kWindowSlots)
			output("RAMMAP%s: $%02X%02X (window not emulated)", kSlotNames[slot] + 1, hi, lo);
		else if (hi & kRAMMapBRAM) {
			if (lo < mcel::WIN_BLOCKS)
				output("RAMMAP%s: $%02X%02X -> BRAM block %u (%s)%s", kSlotNames[slot] + 1, hi, lo, lo, kBRAMBlockNames[lo], mbSelected ? "" : " (hidden: not selected)");
			else
				output("RAMMAP%s: $%02X%02X -> BRAM block %u (out of map)", kSlotNames[slot] + 1, hi, lo, lo);
		} else {
			const uint32 block = lo | ((uint32)(hi & 7) << 8);

			if (block)
				output("RAMMAP%s: $%02X%02X -> SDRAM block %u ($%06X-$%06X)%s", kSlotNames[slot] + 1, hi, lo, block, block * kWindowSize, block * kWindowSize + kWindowSize - 1, mbSelected ? "" : " (hidden: not selected)");
			else
				output("RAMMAP%s: $%02X%02X -> stock RAM", kSlotNames[slot] + 1, hi, lo);
		}
	}

	output("PBIRAMBANK: $%02X%s  MIRQ_CONTROL: $%02X  MEXTBN: $%02X%02X  BGEN: $%02X",
		cel.pbirambank(),
		IsCELPaletteBankSelected() ? " (CEL palette)" : "",
		mMIRQControl, mMEXTBNH, mMEXTBNL, mBGEN);
	output("Viewport: VPX=%d VPY=%d (12.4)  Collision: A=%u B=%u BMAX=%u status=$%02X",
		cel.vpx(), cel.vpy(), cel.col_a_, cel.col_b(), cel.col_bmax_, cel.col_status());

	const auto& cfg = cel.config();
	output("Frame: %s, next strip %u of %u, %u scanlines, strip window %lld cycles, vblank window %lld cycles",
		mbFrameActive ? "active" : "idle",
		mNextStrip,
		kStripCount,
		mScanlineCount,
		(long long)cfg.strip_window_cycles,
		(long long)cfg.vblank_window_cycles);

	const auto& stats = cel.stats();
	output("Last prologue: %lld cycles, %d sprites (modes: LINE %d SCALE %d AFFINE %d QUAD %d, invisible %d, degenerate %d, unsupported %d)",
		(long long)stats.prologue_cycles,
		stats.prologue_sprites,
		stats.mode_count[mcel::MODE_LINE],
		stats.mode_count[mcel::MODE_SCALE],
		stats.mode_count[mcel::MODE_AFFINE],
		stats.mode_count[mcel::MODE_QUAD],
		stats.invisible,
		stats.degenerate,
		stats.unsupported_opcode);

	for(uint32 s = 0; s < kStripCount; ++s) {
		const auto& st = stats.strip[s];

		if (!st.cycles_window)
			continue;

		output("Strip %u: %lld / %lld cycles (%.1f%%), %d scanned, %d hit, %d drawn%s",
			s,
			(long long)st.cycles,
			(long long)st.cycles_window,
			100.0 * (double)st.cycles / (double)st.cycles_window,
			st.sprites_scanned,
			st.sprites_hit,
			st.sprites_drawn,
			st.overrun ? " OVERRUN" : "");
	}

	output("6502 SDRAM accesses through windows: %llu total, %u in current window", (unsigned long long)mTotalSDRAMHits, mWindowSDRAMHits);

	const auto& warnings = cel.warnings();
	if (!warnings.empty()) {
		output("Model warnings:");

		for(const auto& w : warnings)
			output("  %s", w.c_str());
	}
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::OnScheduledEvent(uint32 id) {
	if (id != kEventId_Strip)
		return;

	mpEventStrip = nullptr;

	if (!mbFrameActive || mNextStrip >= kStripCount)
		return;

	RunStrip();

	if (mNextStrip < kStripCount)
		mpScheduler->SetEvent(kStripHeight * kCyclesPerScanline, this, kEventId_Strip, mpEventStrip);
}

///////////////////////////////////////////////////////////////////////////

sint32 ATDeviceMaria::OnRegisterDebugRead(uint32 addr) {
	if (addr == kRegCELCTL)
		return ComposeStatus();

	return -1;
}

sint32 ATDeviceMaria::OnRegisterRead(uint32 addr) {
	if (addr == kRegCELCTL)
		return mpCel->rd((uint16)addr);

	return -1;
}

bool ATDeviceMaria::OnRegisterWrite(uint32 addr, uint8 value) {
	switch(addr) {
		case kRegRAMMap4000L:
		case kRegRAMMap4000H:
		case kRegRAMMap8000L:
		case kRegRAMMap8000H: {
			const uint32 slot = (addr - kRegRAMMap4000L) >> 1;

			mRAMMap[slot][addr & 1] = value;

			// The blitter latches RAMMAP itself to decode its BRAM windows
			// (D54).
			mpCel->wr((uint16)addr, value);
			UpdateLayers();
			return true;
		}

		case kRegRAMMapC000L:
		case kRegRAMMapC000H:
			// The $C000 window is latched for diagnostics only; it is not
			// wired to the blitter or to SDRAM in this device.
			mRAMMap[2][addr & 1] = value;
			return true;

		case kRegCELCTL:
			mpCel->wr((uint16)addr, value);
			++mActivityCounter;
			return true;

		case kRegMIRQControl:
			mMIRQControl = value;
			return true;

		case kRegBGEN:
			mBGEN = value;
			return true;

		case kRegMEXTBNL:
			mMEXTBNL = value;
			return true;

		case kRegMEXTBNH:
			mMEXTBNH = value;
			return true;

		case kRegPBIRAMBank:
			// The blitter model latches PBIRAMBANK itself to decode the
			// CEL palette page (D86).
			mpCel->wr((uint16)addr, value);
			return true;

		default:
			return false;
	}
}

sint32 ATDeviceMaria::OnPBIRAMDebugRead(uint32 addr) {
	return OnPBIRAMRead(addr);
}

sint32 ATDeviceMaria::OnPBIRAMRead(uint32 addr) {
	// Only the CEL palette banks are emulated; the other PBIRAMBANK pages
	// belong to the rest of the cartridge and float.
	if (!IsCELPaletteBankSelected())
		return -1;

	return mpCel->rd((uint16)addr);
}

bool ATDeviceMaria::OnPBIRAMWrite(uint32 addr, uint8 value) {
	if (!IsCELPaletteBankSelected())
		return false;

	mpCel->wr((uint16)addr, value);
	UpdatePaletteEntry(addr & 0xFF);
	++mActivityCounter;

	return true;
}

bool ATDeviceMaria::IsCELPaletteBankSelected() const {
	const uint8 bank = mpCel->pbirambank();

	return bank >= mcel::PBIRAM_CELPAL_R && bank <= mcel::PBIRAM_CELPAL_B;
}

sint32 ATDeviceMaria::OnWindowDebugRead(uint32 addr) {
	const uint32 slot = (addr >> 14) - 1;

	if (slot >= kWindowSlots)
		return -1;

	if (mRAMMap[slot][1] & kRAMMapBRAM)
		return mpCel->rd((uint16)addr);

	return mpCel->sdram().peek8(GetWindowSDRAMAddress(slot, addr));
}

sint32 ATDeviceMaria::OnWindowRead(uint32 addr) {
	const uint32 slot = (addr >> 14) - 1;

	if (slot >= kWindowSlots)
		return -1;

	if (mRAMMap[slot][1] & kRAMMapBRAM)
		return mpCel->rd((uint16)addr);

	const uint32 sdramAddr = GetWindowSDRAMAddress(slot, addr);
	CountSDRAMAccess(sdramAddr);

	return mpCel->sdram().peek8(sdramAddr);
}

bool ATDeviceMaria::OnWindowWrite(uint32 addr, uint8 value) {
	const uint32 slot = (addr >> 14) - 1;

	if (slot >= kWindowSlots)
		return false;

	if (mRAMMap[slot][1] & kRAMMapBRAM) {
		mpCel->wr((uint16)addr, value);
		++mActivityCounter;
		return true;
	}

	const uint32 sdramAddr = GetWindowSDRAMAddress(slot, addr);
	CountSDRAMAccess(sdramAddr);
	mpCel->sdram().poke8(sdramAddr, value);

	return true;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceMaria::ResetDevice() {
	UnscheduleStrips();

	mbFrameActive = false;
	mNextStrip = 0;
	mWindowSDRAMHits = 0;
	mWindowFirstSDRAMAddr = 0;
	mTotalSDRAMHits = 0;

	// Power-on state of the blitter: every BRAM block zeroed (D78), SDRAM
	// zeroed, no window mapped, SNAP = 0 pending the first prologue.
	delete mpCel;
	mpCel = nullptr;

	mcel::McelConfig cfg;
	cfg.trace_enabled = false;

	mpCel = new mcel::Mcel(cfg);

	memset(mRAMMap, 0, sizeof mRAMMap);
	mMIRQControl = 0;
	mMEXTBNL = 0;
	mMEXTBNH = 0;
	mBGEN = 0;

	// The model's PBIRAMBANK latch and palette tables come up zeroed (all
	// black).
	RebuildPalette();

	memset(mIndexFrame, 0, sizeof mIndexFrame);
	ConvertAllStrips();
	++mVideoInfo.mFrameBufferChangeCount;

	UpdateTimingConfig();
	UpdateLayers();
}

void ATDeviceMaria::UpdateLayers() {
	if (!mpMemMan)
		return;

	mpMemMan->EnableLayer(mpLayerRegisters, mbSelected);
	mpMemMan->EnableLayer(mpLayerPBIRAM, mbSelected);

	for(uint32 slot = 0; slot < kWindowSlots; ++slot)
		mpMemMan->EnableLayer(mpLayerWindow[slot], mbSelected && IsWindowMapped(slot));
}

void ATDeviceMaria::UpdateTimingConfig() {
	if (!mpScheduler || !mpCel)
		return;

	mScanlineCount = g_sim.GetAntic().GetScanlineCount();

	// The blitter core runs at 135 MHz against the computer's scanline
	// timing: a strip window is 24 scanlines and the vertical blank window
	// is everything outside the 240 visible lines (docs/02-architecture.md
	// §8, docs/11-machine-model.md §5).
	const double cpuHz = mpScheduler->GetRate().asDouble();
	const double blitterCyclesPerScanline = (double)kCyclesPerScanline * (kBlitterClockHz / cpuHz);

	auto& cfg = mpCel->config();
	cfg.strip_window_cycles = (int64_t)((double)kStripHeight * blitterCyclesPerScanline + 0.5);
	cfg.vblank_window_cycles = (int64_t)((double)(mScanlineCount - kScreenHeight) * blitterCyclesPerScanline + 0.5);

	const double horizRate = cpuHz / (double)kCyclesPerScanline;
	mVideoInfo.mHorizScanRate = (float)horizRate;
	mVideoInfo.mVertScanRate = (float)(horizRate / (double)mScanlineCount);
}

void ATDeviceMaria::OnVBlank() {
	if (!mpCel || !mpScheduler)
		return;

	if (mbFrameActive) {
		UnscheduleStrips();

		// Any strip still outstanding (only possible if the scanline count
		// changed mid-frame) is rendered before the frame is closed.
		while(mNextStrip < kStripCount)
			RunStrip();

		mpCel->frame_done();
	}

	UpdateTimingConfig();

	// 6502 accesses after strip 9 fall outside every blitter window, so
	// the vblank window starts with a clean count.
	mWindowSDRAMHits = 0;
	mWindowFirstSDRAMAddr = 0;

	mbFrameActive = true;
	mNextStrip = 0;

	mpCel->vblank();

	// Strip 0 must be ready when the first visible line starts; strip s is
	// due at the end of the scanline before its first line.
	const uint32 linesToStrip0 = mScanlineCount - (kVBlankScanline + 1) + kFirstDisplayScanline;
	mpScheduler->SetEvent(linesToStrip0 * kCyclesPerScanline, this, kEventId_Strip, mpEventStrip);
}

void ATDeviceMaria::RunStrip() {
	const uint32 strip = mNextStrip++;
	const auto& cfg = mpCel->config();

	ApplyCPULoad(strip == 0 ? cfg.vblank_window_cycles : cfg.strip_window_cycles);

	mpCel->strip(strip);
	mpCel->scanout(strip, mStripScratch);

	memcpy(&mIndexFrame[strip * kStripHeight * kScreenWidth], mStripScratch, sizeof mStripScratch);
	ConvertStrip(strip);
	++mVideoInfo.mFrameBufferChangeCount;
}

void ATDeviceMaria::ApplyCPULoad(sint64 windowCycles) {
	// Measured volume, modelled distribution (docs/11-machine-model.md §5):
	// the 6502 accesses to SDRAM counted in the window just ended are fed
	// to the model as a periodic contention stream for the strip that is
	// rendered in it.
	auto& cfg = mpCel->config();

	if (!mWindowSDRAMHits) {
		cfg.cpu6502_enabled = false;
	} else {
		sint64 period = windowCycles / (sint64)mWindowSDRAMHits;

		if (period < 1)
			period = 1;
		else if (period > 0x7FFFFFFF)
			period = 0x7FFFFFFF;

		cfg.cpu6502_enabled = true;
		cfg.cpu6502_period_cycles = (int)period;
		cfg.cpu6502_addr = mWindowFirstSDRAMAddr & ~(uint32)0x1FFF;
	}

	mTotalSDRAMHits += mWindowSDRAMHits;
	mWindowSDRAMHits = 0;
	mWindowFirstSDRAMAddr = 0;
}

void ATDeviceMaria::UnscheduleStrips() {
	if (mpScheduler)
		mpScheduler->UnsetEvent(mpEventStrip);
}

uint8 ATDeviceMaria::ComposeStatus() const {
	const mcel::Mcel& cel = *mpCel;

	// BUSY is never observed as 1 in the model: strips render atomically.
	return (cel.snap() ? mcel::STATUS_SNAP : 0)
		| (cel.overrun() ? mcel::STATUS_OVERRUN : 0)
		| (cel.enabled() ? mcel::STATUS_ENABLE : 0)
		| (cel.nodetect() ? mcel::STATUS_NODETECT : 0);
}

bool ATDeviceMaria::IsWindowMapped(uint32 slot) const {
	const uint8 lo = mRAMMap[slot][0];
	const uint8 hi = mRAMMap[slot][1];

	if (hi & kRAMMapBRAM)
		return true;

	// Block 0 means stock RAM, not SDRAM block 0.
	return (lo | ((uint32)(hi & 7) << 8)) != 0;
}

uint32 ATDeviceMaria::GetWindowSDRAMAddress(uint32 slot, uint32 addr) const {
	const uint32 block = mRAMMap[slot][0] | ((uint32)(mRAMMap[slot][1] & 7) << 8);

	return block * kWindowSize + (addr & (kWindowSize - 1));
}

void ATDeviceMaria::CountSDRAMAccess(uint32 sdramAddr) {
	if (!mWindowSDRAMHits)
		mWindowFirstSDRAMAddr = sdramAddr;

	++mWindowSDRAMHits;
}

void ATDeviceMaria::UpdatePaletteEntry(uint32 index) {
	const mcel::Mcel& cel = *mpCel;
	const uint32 packed = 0xFF000000
		| ((uint32)cel.celpal(0)[index] << 16)
		| ((uint32)cel.celpal(1)[index] << 8)
		| (uint32)cel.celpal(2)[index];

	if (mPalette[index] != packed) {
		mPalette[index] = packed;
		mbPaletteDirty = true;
	}
}

void ATDeviceMaria::RebuildPalette() {
	const mcel::Mcel& cel = *mpCel;

	for(uint32 i = 0; i < 256; ++i) {
		mPalette[i] = 0xFF000000
			| ((uint32)cel.celpal(0)[i] << 16)
			| ((uint32)cel.celpal(1)[i] << 8)
			| (uint32)cel.celpal(2)[i];
	}

	mbPaletteDirty = false;
}

void ATDeviceMaria::ConvertStrip(uint32 strip) {
	const uint32 y0 = strip * kStripHeight;

	for(uint32 y = y0; y < y0 + kStripHeight; ++y) {
		const uint8 *VDRESTRICT src = &mIndexFrame[y * kScreenWidth];
		uint32 *VDRESTRICT dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * y);

		for(uint32 x = 0; x < kScreenWidth; ++x)
			dst[x] = mPalette[src[x]];
	}
}

void ATDeviceMaria::ConvertAllStrips() {
	for(uint32 strip = 0; strip < kStripCount; ++strip)
		ConvertStrip(strip);
}
