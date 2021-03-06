// CPS - Run
#include "cps.h"

//HACK
#include "fbaconf.h"
//


// Inputs:
UINT8 CpsReset = 0;
UINT8 Cpi01A = 0, Cpi01C = 0, Cpi01E = 0;

static INT32 nInterrupt;
static INT32 nIrqLine, nIrqCycles;
static bool bEnableAutoIrq50, bEnableAutoIrq52;				// Trigger an interrupt every 32 scanlines

static const INT32 nFirstLine = 0x10;							// The first scanline of the display

static INT32 nCpsCyclesExtra;

INT32 CpsDrawSpritesInReverse = 0;

INT32 nIrqLine50, nIrqLine52;

INT32 nCpsNumScanlines = 259;
INT32 Cps1VBlankIRQLine = 2;

CpsRunInitCallback CpsRunInitCallbackFunction = NULL;
CpsRunInitCallback CpsRunExitCallbackFunction = NULL;
CpsRunResetCallback CpsRunResetCallbackFunction = NULL;
CpsRunFrameStartCallback CpsRunFrameStartCallbackFunction = NULL;
CpsRunFrameMiddleCallback CpsRunFrameMiddleCallbackFunction = NULL;
CpsRunFrameEndCallback CpsRunFrameEndCallbackFunction = NULL;

static void CpsQSoundCheatSearchCallback()
{
	// Q-Sound Shared RAM ranges - not useful for cheat searching, and runs the Z80
	// in the handler, exclude it from cheat searching
	if (Cps == 2) {
		CheatSearchExcludeAddressRange(0x618000, 0x619FFF);
	}
	
	if (Cps1Qs == 1) {	
		CheatSearchExcludeAddressRange(0xF18000, 0xF19FFF);
		CheatSearchExcludeAddressRange(0xF1E000, 0xF1FFFF);
	}
}

static INT32 DrvReset()
{
	// Reset machine
	if (Cps == 2 || PangEEP || Cps1Qs == 1 || CpsBootlegEEPROM) EEPROMReset();
    
    //HACK
    if (glob_ffingeron&&virtual_stick_on) {
        wait_control=60;
        glob_framecpt=0;
        glob_replay_last_dx16=glob_replay_last_dy16=0;
        glob_delta_dy16=0;
        glob_replay_last_fingerOn=0;
    }
    //

	SekOpen(0);
	SekReset();
	SekClose();

	if (((Cps & 1) && !Cps1DisablePSnd) || ((Cps == 2) && !Cps2DisableQSnd)) {
		ZetOpen(0);
		ZetReset();
		ZetClose();
	}

	if (Cps == 2) {
		// Disable beam-synchronized interrupts
		*((UINT16*)(CpsReg + 0x4E)) = BURN_ENDIAN_SWAP_INT16(0x0200);
		*((UINT16*)(CpsReg + 0x50)) = BURN_ENDIAN_SWAP_INT16(nCpsNumScanlines);
		*((UINT16*)(CpsReg + 0x52)) = BURN_ENDIAN_SWAP_INT16(nCpsNumScanlines);
	}

	SekOpen(0);
	CpsMapObjectBanks(0);
	SekClose();

	nCpsCyclesExtra = 0;

	if (((Cps == 2) && !Cps2DisableQSnd) || Cps1Qs == 1) {			// Sound init (QSound)
		QsndReset();
	}
	
	if (CpsRunResetCallbackFunction) {
		CpsRunResetCallbackFunction();
	}
	
	HiscoreReset();

	return 0;
}

static const eeprom_interface qsound_eeprom_interface =
{
	7,		/* address bits */
	8,		/* data bits */
	"0110",	/*  read command */
	"0101",	/* write command */
	"0111",	/* erase command */
	0,
	0,
	0,
	0
};

static const eeprom_interface cps2_eeprom_interface =
{
	6,		/* address bits */
	16,		/* data bits */
	"0110",	/*  read command */
	"0101",	/* write command */
	"0111",	/* erase command */
	0,
	0,
	0,
	0
};

INT32 CpsRunInit()
{
	SekInit(0, 0x68000);					// Allocate 68000
	
	if (CpsMemInit()) {						// Memory init
		return 1;
	}
	
	if (Cps == 2 || PangEEP) {
		EEPROMInit(&cps2_eeprom_interface);
	} else {
		if (Cps1Qs == 1 || CpsBootlegEEPROM) {
			EEPROMInit(&qsound_eeprom_interface);
		}
	}

	CpsRwInit();							// Registers setup

	if (CpsPalInit()) {						// Palette init
		return 1;
	}
	if (CpsObjInit()) {						// Sprite init
		return 1;
	}

	if ((Cps & 1) && Cps1Qs == 0 && Cps1DisablePSnd == 0) {			// Sound init (MSM6295 + YM2151)
		if (PsndInit()) {
			return 1;
		}
	}

	if (((Cps == 2) && !Cps2DisableQSnd) || Cps1Qs == 1) {			// Sound init (QSound)
		if (QsndInit()) {
			return 1;
		}
		QsndSetRoute(BURN_SND_QSND_OUTPUT_1, 1.00, BURN_SND_ROUTE_LEFT);
		QsndSetRoute(BURN_SND_QSND_OUTPUT_2, 1.00, BURN_SND_ROUTE_RIGHT);
	}

	if (Cps == 2 || PangEEP || Cps1Qs == 1 || CpsBootlegEEPROM) EEPROMReset();
	
	if (CpsRunInitCallbackFunction) {
		CpsRunInitCallbackFunction();
	}
	
	DrvReset();

	//Init Draw Function
	DrawFnInit();
	
	pBurnDrvPalette = CpsPal;
	
	if (Cps == 2 || Cps1Qs == 1) {
		CheatSearchInitCallbackFunction = CpsQSoundCheatSearchCallback;
	}

	return 0;
}

INT32 CpsRunExit()
{
	if (Cps == 2 || PangEEP || Cps1Qs == 1 || CpsBootlegEEPROM) EEPROMExit();

	// Sound exit
	if (((Cps == 2) && !Cps2DisableQSnd) || Cps1Qs == 1) QsndExit();
	if (Cps != 2 && Cps1Qs == 0 && !Cps1DisablePSnd) PsndExit();

	// Graphics exit
	CpsObjExit();
	CpsPalExit();

	// Sprite Masking exit
	ZBuf = NULL;

	// Memory exit
	CpsRwExit();
	CpsMemExit();

	SekExit();
	
	if (CpsRunExitCallbackFunction) {
		CpsRunExitCallbackFunction();
		CpsRunExitCallbackFunction = NULL;
	}
	CpsRunInitCallbackFunction = NULL;
	CpsRunResetCallbackFunction = NULL;
	CpsRunFrameStartCallbackFunction = NULL;
	CpsRunFrameMiddleCallbackFunction = NULL;
	CpsRunFrameEndCallbackFunction = NULL;
	
	Cps1VBlankIRQLine = 2;
	
	Cps2DisableQSnd = 0;
	CpsBootlegEEPROM = 0;

	return 0;
}

inline static void CopyCpsReg(INT32 i)
{
	memcpy(CpsSaveReg[i], CpsReg, 0x0100);
}

inline static void CopyCpsFrg(INT32 i)
{
	memcpy(CpsSaveFrg[i], CpsFrg, 0x0010);
}

// Schedule a beam-synchronized interrupt
static void ScheduleIRQ()
{
	INT32 nLine = nCpsNumScanlines;

	if (nIrqLine50 <= nLine) {
		nLine = nIrqLine50;
	}
	if (nIrqLine52 < nLine) {
		nLine = nIrqLine52;
	}

	if (nLine < nCpsNumScanlines) {
		nIrqLine = nLine;
		nIrqCycles = (nLine * nCpsCycles / nCpsNumScanlines) + 1;
	} else {
		nIrqCycles = nCpsCycles + 1;
	}

	return;
}

// Execute a beam-synchronised interrupt and schedule the next one
static void DoIRQ()
{
	// 0x4E - bit 9 = 1: Beam Synchronized interrupts disabled
	// 0x50 - Beam synchronized interrupt #1 occurs at raster line.
	// 0x52 - Beam synchronized interrupt #2 occurs at raster line.

	// Trigger IRQ and copy registers.
	if (nIrqLine >= nFirstLine) {

		nInterrupt++;
		nRasterline[nInterrupt] = nIrqLine - nFirstLine;
	}

	SekSetIRQLine(4, SEK_IRQSTATUS_AUTO);
	SekRun(nCpsCycles * 0x01 / nCpsNumScanlines);
	if (nRasterline[nInterrupt] < 224) {
		CopyCpsReg(nInterrupt);
		CopyCpsFrg(nInterrupt);
	} else {
		nRasterline[nInterrupt] = 0;
	}

	// Schedule next interrupt
	if (!bEnableAutoIrq50) {
		if (nIrqLine >= nIrqLine50) {
			nIrqLine50 = nCpsNumScanlines;
		}
	} else {
		if (bEnableAutoIrq50 && nIrqLine == nIrqLine50) {
			nIrqLine50 += 32;
		}
	}
	if (!bEnableAutoIrq52 && nIrqLine >= nIrqLine52) {
		nIrqLine52 = nCpsNumScanlines;
	} else {
		if (bEnableAutoIrq52 && nIrqLine == nIrqLine52) {
			nIrqLine52 += 32;
		}
	}
	ScheduleIRQ();
	if (nIrqCycles < SekTotalCycles()) {
		nIrqCycles = SekTotalCycles() + 1;
	}

	return;
}

INT32 Cps1Frame()
{
	INT32 nDisplayEnd, nNext, i;

	if (CpsReset) {
		DrvReset();
	}

	SekNewFrame();
	if (Cps1Qs == 1) {
		QsndNewFrame();
	} else {
		if (!Cps1DisablePSnd) {
			ZetOpen(0);
			PsndNewFrame();
		}
	}
	
	if (CpsRunFrameStartCallbackFunction) {
		CpsRunFrameStartCallbackFunction();
	}

	nCpsCycles = (INT32)((INT64)nCPS68KClockspeed * nBurnCPUSpeedAdjust >> 8);

	CpsRwGetInp();												// Update the input port values
    
    //HACK for 'follow finger' touchpad mode
    if (glob_ffingeron&&virtual_stick_on) {
        if ( wait_control==0 ) PatchMemory68KFFinger();
        else wait_control--;
    }
    //8 bits => 0/1: touch off/on switch
    //          1/2: posX
    //          2/4: posY
    //          3/8: input0
    //          4/16: input1
    //          5/32: ...
    //          6/64:
    //          7/128:
    
    if ((glob_replay_mode==REPLAY_RECORD_MODE)&&(glob_replay_data_index<MAX_REPLAY_DATA_BYTES-MAX_REPLAY_FRAME_SIZE)) {
        if (glob_replay_flag) {
            //STORE FRAME_INDEX
            glob_replay_data_stream[glob_replay_data_index++]=glob_framecpt&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>8)&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>16)&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>24)&0xFF; //frame index
            //STORE FLAG
            glob_replay_data_stream[glob_replay_data_index++]=glob_replay_flag;
            
            if (glob_replay_flag&REPLAY_FLAG_POSX) { //MEMX HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=glob_replay_last_dx16&0xFF;
                glob_replay_data_stream[glob_replay_data_index++]=(glob_replay_last_dx16>>8)&0xFF;
            }
            if (glob_replay_flag&REPLAY_FLAG_POSY) { //MEMY HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=glob_replay_last_dy16&0xFF;
                glob_replay_data_stream[glob_replay_data_index++]=(glob_replay_last_dy16>>8)&0xFF;
            }
            if (glob_replay_flag&REPLAY_FLAG_IN0) { //INPUT0 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[0];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN1) { //INPUT1 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[1];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN2) { //INPUT2 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[2];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN3) { //INPUT3 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[3];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN4) { //INPUT4 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[4];
            }
            
        }
    }
    //


	nDisplayEnd = (nCpsCycles * (nFirstLine + 224)) / nCpsNumScanlines;	// Account for VBlank

	SekOpen(0);
	SekIdle(nCpsCyclesExtra);

	SekRun(nCpsCycles * nFirstLine / nCpsNumScanlines);					// run 68K for the first few lines

	CpsObjGet();											// Get objects

	for (i = 0; i < 4; i++) {
		nNext = ((i + 1) * nCpsCycles) >> 2;					// find out next cycle count to run to
		
		if (i == 2 && CpsRunFrameMiddleCallbackFunction) {
			CpsRunFrameMiddleCallbackFunction();
		}

		if (SekTotalCycles() < nDisplayEnd && nNext > nDisplayEnd) {

			SekRun(nNext - nDisplayEnd);						// run 68K

			memcpy(CpsSaveReg[0], CpsReg, 0x100);				// Registers correct now

			SekSetIRQLine(Cps1VBlankIRQLine, SEK_IRQSTATUS_AUTO);				// Trigger VBlank interrupt
		}

		SekRun(nNext - SekTotalCycles());						// run 68K
		
//		if (pBurnDraw) {
//			CpsDraw();										// Draw frame
//		}
	}
	
	if (pBurnDraw) {
		CpsDraw();										// Draw frame
	}

	if (Cps1Qs == 1) {
		QsndEndFrame();
	} else {
		if (!Cps1DisablePSnd) {
			PsndSyncZ80(nCpsZ80Cycles);
			PsmUpdate(nBurnSoundLen);
			ZetClose();
		}
	}
	
	if (CpsRunFrameEndCallbackFunction) {
		CpsRunFrameEndCallbackFunction();
	}

	nCpsCyclesExtra = SekTotalCycles() - nCpsCycles;

	SekClose();
    
    glob_framecpt++;
    if ((glob_replay_mode==REPLAY_PLAYBACK_MODE)&&(glob_replay_data_index>=glob_replay_data_index_max)) {
        //should end replay here
        nShouldExit=1;
    }

	return 0;
}

INT32 Cps2Frame()
{
	INT32 nDisplayEnd, nNext;									// variables to keep track of executed 68K cyles
	INT32 i;

	if (CpsReset) {
		DrvReset();
	}

//	extern INT32 prevline;
//	prevline = -1;

	SekNewFrame();
	if (!Cps2DisableQSnd) QsndNewFrame();

	nCpsCycles = (INT32)(((INT64)nCPS68KClockspeed * nBurnCPUSpeedAdjust) / 0x0100);
	SekOpen(0);
	SekSetCyclesScanline(nCpsCycles / nCpsNumScanlines);

	CpsRwGetInp();											// Update the input port values
    
    //HACK for 'follow finger' touchpad mode
    if (glob_ffingeron&&virtual_stick_on) {
        if ( wait_control==0 ) PatchMemory68KFFinger();
        else wait_control--;
    }
    
    //8 bits => 0/1: touch off/on switch
    //          1/2: posX
    //          2/4: posY
    //          3/8: input0
    //          4/16: input1
    //          5/32: ...
    //          6/64:
    //          7/128:
    
    if ((glob_replay_mode==REPLAY_RECORD_MODE)&&(glob_replay_data_index<MAX_REPLAY_DATA_BYTES-MAX_REPLAY_FRAME_SIZE)) {
        if (glob_replay_flag) {
            //STORE FRAME_INDEX
            glob_replay_data_stream[glob_replay_data_index++]=glob_framecpt&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>8)&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>16)&0xFF; //frame index
            glob_replay_data_stream[glob_replay_data_index++]=(glob_framecpt>>24)&0xFF; //frame index
            //STORE FLAG
            glob_replay_data_stream[glob_replay_data_index++]=glob_replay_flag;
            
            if (glob_replay_flag&REPLAY_FLAG_POSX) { //MEMX HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=glob_replay_last_dx16&0xFF;
                glob_replay_data_stream[glob_replay_data_index++]=(glob_replay_last_dx16>>8)&0xFF;
            }
            if (glob_replay_flag&REPLAY_FLAG_POSY) { //MEMY HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=glob_replay_last_dy16&0xFF;
                glob_replay_data_stream[glob_replay_data_index++]=(glob_replay_last_dy16>>8)&0xFF;
            }
            if (glob_replay_flag&REPLAY_FLAG_IN0) { //INPUT0 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[0];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN1) { //INPUT1 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[1];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN2) { //INPUT2 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[2];
            }
            if (glob_replay_flag&REPLAY_FLAG_IN3) { //INPUT3 HAS CHANGED
                glob_replay_data_stream[glob_replay_data_index++]=last_DrvInput[3];
            }
            
        }
    }
    //
	
	// Check the volumes every 5 frames or so
	if (GetCurrentFrame() % 5 == 0) {
		if (Cps2VolUp) Cps2Volume++;
		if (Cps2VolDwn) Cps2Volume--;
		
		if (Cps2Volume > 39) Cps2Volume = 39;
		if (Cps2Volume < 0) Cps2Volume = 0;
		
		QscSetRoute(BURN_SND_QSND_OUTPUT_1, Cps2Volume / 39.0, BURN_SND_ROUTE_LEFT);
		QscSetRoute(BURN_SND_QSND_OUTPUT_2, Cps2Volume / 39.0, BURN_SND_ROUTE_RIGHT);
	}
	
	nDisplayEnd = nCpsCycles * (nFirstLine + 224) / nCpsNumScanlines;	// Account for VBlank

	nInterrupt = 0;
	for (i = 0; i < MAX_RASTER + 2; i++) {
		nRasterline[i] = 0;
	}

	// Determine which (if any) of the line counters generates the first IRQ
	bEnableAutoIrq50 = bEnableAutoIrq52 = false;
	nIrqLine50 = nIrqLine52 = nCpsNumScanlines;
	if (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x50))) & 0x8000) {
		bEnableAutoIrq50 = true;
	}
	if (bEnableAutoIrq50 || (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x4E))) & 0x0200) == 0) {
		nIrqLine50 = (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x50))) & 0x01FF);
	}
	if (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x52))) & 0x8000) {
		bEnableAutoIrq52 = true;
	}
	if (bEnableAutoIrq52 || (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x4E))) & 0x0200) == 0) {
		nIrqLine52 = (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x52))) & 0x01FF);
	}
	ScheduleIRQ();

	SekIdle(nCpsCyclesExtra);

	if (nIrqCycles < nCpsCycles * nFirstLine / nCpsNumScanlines) {
		SekRun(nIrqCycles);
		DoIRQ();
	}
	nNext = nCpsCycles * nFirstLine / nCpsNumScanlines;
	if (SekTotalCycles() < nNext) {
		SekRun(nNext - SekTotalCycles());
	}

	CopyCpsReg(0);										// Get inititial copy of registers
	CopyCpsFrg(0);										//

	if (nIrqLine >= nCpsNumScanlines && (BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x4E))) & 0x0200) == 0) {
		nIrqLine50 = BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x50))) & 0x01FF;
		nIrqLine52 = BURN_ENDIAN_SWAP_INT16(*((UINT16*)(CpsReg + 0x52))) & 0x01FF;
		ScheduleIRQ();
	}

	for (i = 0; i < 3; i++) {
		nNext = ((i + 1) * nDisplayEnd) / 3;			// find out next cycle count to run to

		while (nNext > nIrqCycles && nInterrupt < MAX_RASTER) {
			SekRun(nIrqCycles - SekTotalCycles());
			DoIRQ();
		}
		SekRun(nNext - SekTotalCycles());				// run cpu
	}
	
	CpsObjGet();										// Get objects

//	nCpsCyclesSegment[0] = (nCpsCycles * nVBlank) / nCpsNumScanlines;
//	nDone += SekRun(nCpsCyclesSegment[0] - nDone);

	SekSetIRQLine(2, SEK_IRQSTATUS_AUTO);				// VBlank
	if (pBurnDraw) {
		CpsDraw();
	}
	SekRun(nCpsCycles - SekTotalCycles());	

	nCpsCyclesExtra = SekTotalCycles() - nCpsCycles;

	if (!Cps2DisableQSnd) QsndEndFrame();

	SekClose();
    
    glob_framecpt++;
    if ((glob_replay_mode==REPLAY_PLAYBACK_MODE)&&(glob_replay_data_index>=glob_replay_data_index_max)) {
        //should end replay here
        nShouldExit=1;
    }

//	bprintf(PRINT_NORMAL, _T("    -\n"));

#if 0 && defined FBA_DEBUG
	if (nInterrupt) {
		bprintf(PRINT_IMPORTANT, _T("Beam synchronized interrupt at line %2X.\r"), nRasterline[nInterrupt]);
	} else {
		bprintf(PRINT_NORMAL, _T("Beam synchronized interrupt disabled.   \r"));
	}

	extern INT32 counter;
	if (counter) {
		bprintf(PRINT_NORMAL, _T("\n\nSlices start at: "));
		for (i = 0; i < MAX_RASTER + 2; i++) {
			bprintf(PRINT_NORMAL, _T("%2X "), nRasterline[i]);
		}
		bprintf(PRINT_NORMAL, _T("\n"));
		for (i = 0; i < 0x80; i++) {
			if (*((UINT16*)(CpsSaveReg[0] + i * 2)) != *((UINT16*)(CpsSaveReg[nInterrupt] + i * 2))) {
				bprintf(PRINT_NORMAL, _T("Register %2X: %4X -> %4X\n"), i * 2, *((UINT16*)(CpsSaveReg[0] + i * 2)), *((UINT16*)(CpsSaveReg[nInterrupt] + i * 2)));
			}
		}
		bprintf(PRINT_NORMAL, _T("\n"));
		for (i = 0; i < 0x010; i++) {
			if (CpsSaveFrg[0][i] != CpsSaveFrg[nInterrupt][i]) {
				bprintf(PRINT_NORMAL, _T("FRG %X: %02X -> %02X\n"), i, CpsSaveFrg[0][i], CpsSaveFrg[nInterrupt][i]);
			}
		}
		bprintf(PRINT_NORMAL, _T("\n"));
		if (((CpsSaveFrg[0][4] << 8) | CpsSaveFrg[0][5]) != ((CpsSaveFrg[nInterrupt][4] << 8) | CpsSaveFrg[nInterrupt][5])) {
			bprintf(PRINT_NORMAL, _T("Layer-sprite priority: %04X -> %04X\n"), ((CpsSaveFrg[0][4] << 8) | CpsSaveFrg[0][5]), ((CpsSaveFrg[nInterrupt][4] << 8) | CpsSaveFrg[nInterrupt][5]));
		}

		bprintf(PRINT_NORMAL, _T("\n"));
		for (INT32 j = 0; j <= nInterrupt; j++) {
			if (j) {
				bprintf(PRINT_NORMAL, _T("IRQ : %i (triggered at line %3i)\n\n"), j, nRasterline[j]);
			} else {
				bprintf(PRINT_NORMAL, _T("Initial register status\n\n"));
			}

			for (i = 0; i < 0x080; i+= 8) {
				bprintf(PRINT_NORMAL, _T("%2X: %4X %4X %4X %4X %4X %4X %4X %4X\n"), i * 2, *((UINT16*)(CpsSaveReg[j] + 0 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 2 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 4 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 6 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 8 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 10 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 12 + i * 2)), *((UINT16*)(CpsSaveReg[j] + 14 + i * 2)));
			}

			bprintf(PRINT_NORMAL, _T("\nFRG: "));
			for (i = 0; i < 0x010; i++) {
				bprintf(PRINT_NORMAL, _T("%02X "), CpsSaveFrg[j][i]);
			}
			bprintf(PRINT_NORMAL, _T("\n\n"));

		}

		extern INT32 bRunPause;
		bRunPause = 1;
		counter = 0;
	}
#endif

	return 0;
}

