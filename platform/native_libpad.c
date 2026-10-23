#include <macros.h>
#include <platform/native_input.h>
#include <psx/libpad.h>

// NOTE(aalhendi): Native libpad preserves behavior from PsyCross's
// MIT-licensed LIBPAD.C while moving host ownership into ctr-native.
// See THIRD_PARTY_NOTICES.md.

s32 g_padCommEnable = 0;

void PadInitMtap(unsigned char *slot1, unsigned char *slot2)
{
	Platform_InputPadInit(0, slot1);
	Platform_InputPadInit(1, slot2);
}

void PadStartCom(void)
{
	g_padCommEnable = 1;
}

void PadStopCom(void)
{
	g_padCommEnable = 0;
}

int PadGetState(int port)
{
	return Platform_InputPadGetState(port);
}

int PadInfoAct(int port, int acno, int term)
{
	(void)port;
	(void)acno;
	(void)term;
	return 0;
}

int PadSetActAlign(int port, unsigned char *table)
{
	(void)port;
	(void)table;
	return 1;
}

int PadSetMainMode(int socket, int offs, int lock)
{
	(void)socket;
	(void)offs;
	(void)lock;
	return 0;
}

void PadSetAct(int port, unsigned char *table, int len)
{
	Platform_InputPadVibrate(port, table, len);
}
