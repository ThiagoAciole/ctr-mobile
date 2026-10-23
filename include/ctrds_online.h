#ifndef CTRDS_ONLINE_H
#define CTRDS_ONLINE_H

// OnlineCTR client.
//
// Speaks the OnlineCTR wire protocol (build 1021) so this port can join the same
// servers the official client does. The packet layouts below are transcribed
// from CTR-ModSDK's decompile/General/AltMods/OnlineCTR/global.h and must stay
// byte-identical to it -- they are bitfields sent raw over the wire, so any
// reordering silently breaks compatibility. Verified against the project's own
// server implementation: SG_ROOMS decodes to numRooms=16, version=1021.
//
// Everything here runs on its own thread. The game thread only ever reads a
// snapshot and posts commands, because a blocking network call in the frame loop
// would cost the 118fps the port currently holds.

#define CTRDS_OCTR_VERSION  1021
#define CTRDS_OCTR_NAME_LEN 9
#define CTRDS_OCTR_MAX_PLAYERS 8
#define CTRDS_OCTR_MAX_ROOMS 16

enum CtrdsOnlineState
{
	CTRDS_ONLINE_OFF = 0,
	CTRDS_ONLINE_RESOLVING,
	CTRDS_ONLINE_CONNECTING,
	CTRDS_ONLINE_LOBBY,      // connected, room list received
	CTRDS_ONLINE_JOINING,
	CTRDS_ONLINE_IN_ROOM,
	CTRDS_ONLINE_ERROR
};

// What the game thread is allowed to look at. Copied under a lock.
struct CtrdsOnlineStatus
{
	int state;
	int serverVersion;
	int numRooms;
	int roomClients[CTRDS_OCTR_MAX_ROOMS];

	int clientID;        // our slot in the room, -1 until assigned
	int numClientsTotal;
	char names[CTRDS_OCTR_MAX_PLAYERS][CTRDS_OCTR_NAME_LEN + 1];

	char message[96];    // last human-readable status or error
};

// Reads ctrds.cfg for online settings and starts the client thread when
// enabled. Safe to call when disabled: it does nothing.
void Ctrds_OnlineInit(void);
void Ctrds_OnlineShutdown(void);

// Start, stop or flip the client while the game runs. Toggling does not need a
// restart: the thread and socket are created and torn down on demand.
void Ctrds_OnlineStart(void);
void Ctrds_OnlineToggle(void);

// Snapshot of the current state. Cheap; safe from the game thread.
void Ctrds_OnlineGetStatus(struct CtrdsOnlineStatus *out);

// Commands. Non-blocking; they are queued for the network thread.
void Ctrds_OnlineJoinRoom(int room);

// Called once per frame from the game thread: drains status messages to the
// log. The network thread must not touch the log itself.
void Ctrds_OnlinePump(void);

int Ctrds_OnlineEnabled(void);

#endif // CTRDS_ONLINE_H
