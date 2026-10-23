#include <common.h>

//- creates 8 events (4 per card?)
//- calls InitCARD and StartCARD
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003d7d8-0x8003d95c.
void MEMCARD_InitCard(void)
{
	// This section was copy/pasted by Naughty Dog,
	// psx\sample\memcard\CARD\CARD.C lines 84 to 101

	// -------------------------------------------
	EnterCriticalSection();
	sdata->SwCARD_EvSpIOE = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, NULL);
	sdata->SwCARD_EvSpERROR = OpenEvent(SwCARD, EvSpERROR, EvMdNOINTR, NULL);
	sdata->SwCARD_EvSpTIMOUT = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, NULL);
	sdata->SwCARD_EvSpNEW = OpenEvent(SwCARD, EvSpNEW, EvMdNOINTR, NULL);
	sdata->HwCARD_EvSpIOE = OpenEvent(HwCARD, EvSpIOE, EvMdNOINTR, NULL);
	sdata->HwCARD_EvSpERROR = OpenEvent(HwCARD, EvSpERROR, EvMdNOINTR, NULL);
	sdata->HwCARD_EvSpTIMOUT = OpenEvent(HwCARD, EvSpTIMOUT, EvMdNOINTR, NULL);
	sdata->HwCARD_EvSpNEW = OpenEvent(HwCARD, EvSpNEW, EvMdNOINTR, NULL);
	EnableEvent(sdata->SwCARD_EvSpIOE);
	EnableEvent(sdata->SwCARD_EvSpERROR);
	EnableEvent(sdata->SwCARD_EvSpTIMOUT);
	EnableEvent(sdata->SwCARD_EvSpNEW);
	EnableEvent(sdata->HwCARD_EvSpIOE);
	EnableEvent(sdata->HwCARD_EvSpERROR);
	EnableEvent(sdata->HwCARD_EvSpTIMOUT);
	EnableEvent(sdata->HwCARD_EvSpNEW);
	ExitCriticalSection();
	// -------------------------------------------

	InitCARD(0);
	StartCARD();
	_bu_init();

	// This tells us a memory card exists,
	// If it is set to 0 with Cheat Engine,
	// the game will think the card is unformatted
	sdata->memcardStatusFlags = 1;
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003d95c-0x8003d9ec.
void MEMCARD_CloseCard(void)
{
	// This function was copy/pasted by Naughty Dog,
	// psx\sample\memcard\CARD\CARD.C lines 355 to 365

	StopCARD();
	EnterCriticalSection();
	CloseEvent(sdata->SwCARD_EvSpIOE);
	CloseEvent(sdata->SwCARD_EvSpERROR);
	CloseEvent(sdata->SwCARD_EvSpTIMOUT);
	CloseEvent(sdata->SwCARD_EvSpNEW);
	CloseEvent(sdata->HwCARD_EvSpIOE);
	CloseEvent(sdata->HwCARD_EvSpERROR);
	CloseEvent(sdata->HwCARD_EvSpTIMOUT);
	CloseEvent(sdata->HwCARD_EvSpNEW);
	ExitCriticalSection();

	return;
}
