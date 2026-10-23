// OnlineCTR client -- transport and lobby.
//
// See include/ctrds_online.h for why the packet layouts are what they are. This
// file owns the socket; nothing here is called from the frame loop except
// Ctrds_OnlineGetStatus, which copies a snapshot under a lock.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <enet/enet.h>

#include "common.h"
#include "ctrds.h"
#include "ctrds_online.h"

// ---------------------------------------------------------------- protocol --
// Transcribed from CTR-ModSDK global.h. Bitfield order matters: these go on the
// wire as-is.

enum ServerGiveMessageType
{
	SG_ROOMS = 0,
	SG_NEWCLIENT,
	SG_NAME,
	SG_TRACK,
	SG_CHARACTER,
	SG_STARTLOADING,
	SG_STARTRACE,
	SG_RACEDATA,
	SG_WEAPON,
	SG_ENDRACE,
	SG_SERVERCLOSED
};

enum ClientGiveMessageType
{
	CG_JOINROOM = 0,
	CG_NAME,
	CG_TRACK,
	CG_CHARACTER,
	CG_STARTRACE,
	CG_RACEDATA,
	CG_WEAPON,
	CG_ENDRACE
};

struct SG_MessageRooms
{
	unsigned char type : 4;
	unsigned char padding : 4;
	unsigned char numRooms;
	unsigned short version;
	unsigned char counts[8];
};

struct SG_MessageClientStatus
{
	unsigned char type : 4;
	unsigned char clientID : 4;
	unsigned char numClientsTotal : 4;
	unsigned char special : 4;
};

struct SG_MessageName
{
	unsigned char type : 4;
	unsigned char padding : 4;
	unsigned char clientID : 4;
	unsigned char numClientsTotal : 4;
	char name[CTRDS_OCTR_NAME_LEN + 1];
};

struct CG_MessageRoom
{
	unsigned char type : 4;
	unsigned char padding : 4;
	unsigned char room;
};

struct CG_MessageName
{
	unsigned char type : 4;
	unsigned char padding : 4;
	char name[CTRDS_OCTR_NAME_LEN + 1];
};

// ------------------------------------------------------------------- state --

// NOTE(ctrds): everything static in this file carries an s_octr prefix. This
// file is part of main.c's unity build, so a plain name like s_enabled merges
// with another member's tentative definition of the same name instead of
// erroring -- native_perf.c has exactly that, and sharing it turned the perf
// writer on with no file open, crashing in fprintf.
global_variable int s_octrenabled = 0;
global_variable char s_octrhost[128] = "127.0.0.1";
global_variable int s_octrport = 64001;
global_variable char s_octrname[CTRDS_OCTR_NAME_LEN + 1] = "CTRDS";

global_variable SDL_Thread *s_octrthread = NULL;
global_variable SDL_Mutex *s_octrlock = NULL;
global_variable int s_octrquit = 0;

global_variable struct CtrdsOnlineStatus s_octrstatus;

// Single pending command; the lobby never needs a deep queue.
global_variable int s_octrpendingJoinRoom = -1;
global_variable unsigned s_octrmessageSeq = 0;
global_variable unsigned s_octrmessageSeqLogged = 0;
global_variable int s_octrautoRoom = -1;
global_variable int s_octrautoJoined = 0;

// NOTE(ctrds): never log from this thread. Platform_Log and the perf counters
// share one unlocked FILE*, so logging here races the frame loop's own writes
// and corrupts it -- observed as a SIGSEGV inside fprintf on the main thread.
// Messages are stored and drained by Ctrds_OnlinePump on the game thread.
internal void Online_SetMessage(const char *msg)
{
	SDL_LockMutex(s_octrlock);
	SDL_strlcpy(s_octrstatus.message, msg, sizeof(s_octrstatus.message));
	s_octrmessageSeq++;
	SDL_UnlockMutex(s_octrlock);
}

internal void Online_SetState(int state)
{
	SDL_LockMutex(s_octrlock);
	s_octrstatus.state = state;
	SDL_UnlockMutex(s_octrlock);
}

internal void Online_HandleRooms(const unsigned char *data, size_t len)
{
	struct SG_MessageRooms r;
	int i;

	if (len < sizeof(r))
	{
		return;
	}

	memcpy(&r, data, sizeof(r));

	SDL_LockMutex(s_octrlock);
	s_octrstatus.serverVersion = r.version;
	s_octrstatus.numRooms = (r.numRooms > CTRDS_OCTR_MAX_ROOMS) ? CTRDS_OCTR_MAX_ROOMS : r.numRooms;

	// Occupancy is packed two rooms per byte, low nibble first.
	for (i = 0; i < s_octrstatus.numRooms; i++)
	{
		s_octrstatus.roomClients[i] = (i & 1) ? (r.counts[i / 2] >> 4) : (r.counts[i / 2] & 0x0F);
	}
	SDL_UnlockMutex(s_octrlock);

	if (r.version != CTRDS_OCTR_VERSION)
	{
		char msg[96];

		// Their client accepts a couple of older servers; mirror that so we are
		// not stricter than the official one.
		if (!((r.version == 1019 || r.version == 1020) && CTRDS_OCTR_VERSION == 1021))
		{
			snprintf(msg, sizeof(msg), "version mismatch: server %u, client %d", r.version, CTRDS_OCTR_VERSION);
			Online_SetMessage(msg);
			Online_SetState(CTRDS_ONLINE_ERROR);
			return;
		}
	}

	Online_SetState(CTRDS_ONLINE_LOBBY);
	Online_SetMessage("room list received");

	// The room list is also the cue to take a configured room, once.
	if ((s_octrautoRoom >= 0) && !s_octrautoJoined)
	{
		s_octrautoJoined = 1;

		SDL_LockMutex(s_octrlock);
		s_octrpendingJoinRoom = s_octrautoRoom;
		SDL_UnlockMutex(s_octrlock);
	}
}

internal void Online_HandlePacket(ENetPeer *peer, const unsigned char *data, size_t len)
{
	int type;

	if (len < 1)
	{
		return;
	}

	type = data[0] & 0x0F;

	switch (type)
	{
	case SG_ROOMS:
		Online_HandleRooms(data, len);
		break;

	case SG_NEWCLIENT:
	{
		struct SG_MessageClientStatus st;

		if (len < sizeof(st))
		{
			break;
		}
		memcpy(&st, data, sizeof(st));

		SDL_LockMutex(s_octrlock);
		s_octrstatus.clientID = st.clientID;
		s_octrstatus.numClientsTotal = st.numClientsTotal;
		s_octrstatus.state = CTRDS_ONLINE_IN_ROOM;
		SDL_UnlockMutex(s_octrlock);

		{
			char msg[96];
			snprintf(msg, sizeof(msg), "joined room as client %u of %u", st.clientID, st.numClientsTotal);
			Online_SetMessage(msg);
		}

		// Announce ourselves, as their client does on entering a room.
		{
			struct CG_MessageName n;
			ENetPacket *packet;

			memset(&n, 0, sizeof(n));
			n.type = CG_NAME;
			SDL_strlcpy(n.name, s_octrname, sizeof(n.name));

			packet = enet_packet_create(&n, sizeof(n), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, packet);
		}
		break;
	}

	case SG_NAME:
	{
		struct SG_MessageName n;

		if (len < sizeof(n))
		{
			break;
		}
		memcpy(&n, data, sizeof(n));

		if (n.clientID < CTRDS_OCTR_MAX_PLAYERS)
		{
			SDL_LockMutex(s_octrlock);
			memcpy(s_octrstatus.names[n.clientID], n.name, CTRDS_OCTR_NAME_LEN + 1);
			s_octrstatus.names[n.clientID][CTRDS_OCTR_NAME_LEN] = '\0';
			s_octrstatus.numClientsTotal = n.numClientsTotal;
			SDL_UnlockMutex(s_octrlock);

		}
		break;
	}

	case SG_SERVERCLOSED:
		Online_SetMessage("server closed the room");
		Online_SetState(CTRDS_ONLINE_LOBBY);
		break;

	default:
		// Race traffic is not wired up yet; ignore rather than misinterpret.
		break;
	}
}

internal int SDLCALL Online_Thread(void *ud)
{
	ENetAddress addr;
	ENetHost *client = NULL;
	ENetPeer *peer = NULL;
	ENetEvent event;

	(void)ud;

	if (enet_initialize() != 0)
	{
		Online_SetMessage("enet_initialize failed");
		Online_SetState(CTRDS_ONLINE_ERROR);
		return 0;
	}

	Online_SetState(CTRDS_ONLINE_RESOLVING);

	if (enet_address_set_host(&addr, s_octrhost) != 0)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "cannot resolve %s", s_octrhost);
		Online_SetMessage(msg);
		Online_SetState(CTRDS_ONLINE_ERROR);
		enet_deinitialize();
		return 0;
	}
	addr.port = (enet_uint16)s_octrport;

	// Same shape as the official client: one connection, two channels.
	client = enet_host_create(NULL, 1, 2, 0, 0);
	if (client == NULL)
	{
		Online_SetMessage("enet_host_create failed");
		Online_SetState(CTRDS_ONLINE_ERROR);
		enet_deinitialize();
		return 0;
	}

	peer = enet_host_connect(client, &addr, 2, 0);
	if (peer == NULL)
	{
		Online_SetMessage("no peer available");
		Online_SetState(CTRDS_ONLINE_ERROR);
		enet_host_destroy(client);
		enet_deinitialize();
		return 0;
	}

	Online_SetState(CTRDS_ONLINE_CONNECTING);
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "connecting to %s:%d", s_octrhost, s_octrport);
		Online_SetMessage(msg);
	}

	if (!(enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT))
	{
		Online_SetMessage("connection failed");
		Online_SetState(CTRDS_ONLINE_ERROR);
		enet_peer_reset(peer);
		enet_host_destroy(client);
		enet_deinitialize();
		return 0;
	}

	Online_SetMessage("connected");
	enet_peer_timeout(peer, 1000000, 1000000, 5000);

	while (!s_octrquit)
	{
		int room;

		// Commands from the game thread.
		SDL_LockMutex(s_octrlock);
		room = s_octrpendingJoinRoom;
		s_octrpendingJoinRoom = -1;
		SDL_UnlockMutex(s_octrlock);

		if (room >= 0)
		{
			struct CG_MessageRoom m;
			ENetPacket *packet;
			char msg[96];

			memset(&m, 0, sizeof(m));
			m.type = CG_JOINROOM;
			m.room = (unsigned char)room;

			packet = enet_packet_create(&m, sizeof(m), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, packet);

			Online_SetState(CTRDS_ONLINE_JOINING);
			snprintf(msg, sizeof(msg), "joining room %d", room);
			Online_SetMessage(msg);
		}

		while (enet_host_service(client, &event, 50) > 0)
		{
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				Online_HandlePacket(peer, event.packet->data, event.packet->dataLength);
				enet_packet_destroy(event.packet);
			}
			else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				Online_SetMessage("disconnected by server");
				Online_SetState(CTRDS_ONLINE_ERROR);
				goto done;
			}
		}
	}

done:
	if (peer != NULL)
	{
		enet_peer_disconnect(peer, 0);

		while (enet_host_service(client, &event, 1000) > 0)
		{
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				enet_packet_destroy(event.packet);
			}
			else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				break;
			}
		}
	}

	enet_host_destroy(client);
	enet_deinitialize();
	return 0;
}

// -------------------------------------------------------------- public API --

int Ctrds_OnlineEnabled(void)
{
	return s_octrenabled;
}

void Ctrds_OnlineGetStatus(struct CtrdsOnlineStatus *out)
{
	if (out == NULL)
	{
		return;
	}

	if (s_octrlock == NULL)
	{
		memset(out, 0, sizeof(*out));
		return;
	}

	SDL_LockMutex(s_octrlock);
	*out = s_octrstatus;
	SDL_UnlockMutex(s_octrlock);
}

void Ctrds_OnlineJoinRoom(int room)
{
	if (s_octrlock == NULL)
	{
		return;
	}

	SDL_LockMutex(s_octrlock);
	s_octrpendingJoinRoom = room;
	SDL_UnlockMutex(s_octrlock);
}

// Starting and stopping are separate from Init so the panel can switch online on
// and off while the game runs -- no restart.
void Ctrds_OnlineStart(void)
{
	const struct CtrdsOnlineConfig *cfg = Ctrds_OnlineConfig();

	if (s_octrenabled || (cfg == NULL))
	{
		return;
	}

	s_octrenabled = 1;
	SDL_strlcpy(s_octrhost, cfg->host, sizeof(s_octrhost));
	s_octrport = cfg->port;
	s_octrautoRoom = cfg->room;
	SDL_strlcpy(s_octrname, cfg->name, sizeof(s_octrname));

	memset(&s_octrstatus, 0, sizeof(s_octrstatus));
	s_octrstatus.clientID = -1;

	s_octrlock = SDL_CreateMutex();
	if (s_octrlock == NULL)
	{
		s_octrenabled = 0;
		return;
	}

	s_octrquit = 0;
	s_octrthread = SDL_CreateThread(Online_Thread, "ctrds-online", NULL);

	if (s_octrthread == NULL)
	{
		s_octrenabled = 0;
	}
}

void Ctrds_OnlineInit(void)
{
	const struct CtrdsOnlineConfig *cfg = Ctrds_OnlineConfig();

	if ((cfg != NULL) && (cfg->enabled != 0))
	{
		Ctrds_OnlineStart();
	}
}

void Ctrds_OnlineToggle(void)
{
	if (s_octrenabled)
	{
		Ctrds_OnlineShutdown();
	}
	else
	{
		Ctrds_OnlineStart();
	}
}

void Ctrds_OnlinePump(void)
{
	char msg[96];
	unsigned seq;

	if (!s_octrenabled || (s_octrlock == NULL))
	{
		return;
	}

	SDL_LockMutex(s_octrlock);
	seq = s_octrmessageSeq;
	SDL_strlcpy(msg, s_octrstatus.message, sizeof(msg));
	SDL_UnlockMutex(s_octrlock);

	if (seq != s_octrmessageSeqLogged)
	{
		s_octrmessageSeqLogged = seq;
		Platform_Log("[CTR-DS/online] %s\n", msg);
	}
}

void Ctrds_OnlineShutdown(void)
{
	if (!s_octrenabled)
	{
		return;
	}

	s_octrquit = 1;

	if (s_octrthread != NULL)
	{
		SDL_WaitThread(s_octrthread, NULL);
		s_octrthread = NULL;
	}

	if (s_octrlock != NULL)
	{
		SDL_DestroyMutex(s_octrlock);
		s_octrlock = NULL;
	}

	s_octrenabled = 0;
	s_octrautoJoined = 0;
	s_octrmessageSeqLogged = 0;
	memset(&s_octrstatus, 0, sizeof(s_octrstatus));
	s_octrstatus.clientID = -1;
}
