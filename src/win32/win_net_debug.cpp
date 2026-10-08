#include <universal/q_shared.h>
#include "win_net.h"
#include "win_net_debug.h"
#include <script/scr_vm.h>
#include "win_local.h"

static int g_debugClient;

static int g_debugPacketPos[1];
static int sys_debugMessageType[1];

unsigned __int8 g_debugPacket[1][8192];

static int g_debugReadBytesRemote;
static int g_debugReadBytesSent;
static int g_debugWriteBytes;

uint32_t ip_debugSocket[2];
uint32_t ip_debugServerSocket[2];

char *g_debugReadBytes;

int NET_InitDebugStreams()
{
	int result; // eax
	int i; // [esp+0h] [ebp-4h]
	int ia; // [esp+0h] [ebp-4h]

	for (i = 0; i < 2; ++i)
	{
		ip_debugServerSocket[i] = 0;
		ip_debugSocket[i] = 0;
		result = i + 1;
	}
	for (ia = 0; ia < 1; ++ia)
	{
		sys_debugMessageType[ia] = 0;
		g_debugPacketPos[ia] = 0;
		result = ia + 1;
	}
	g_debugReadBytes = 0;
	g_debugReadBytesSent = 0;
	g_debugWriteBytes = 0;
	g_debugReadBytesRemote = 0;
	return result;
}

void Sys_SendDebugReadBytesInternal()
{
	const char *v0; // eax
	const char *v1; // eax

	if (*g_debugReadBytes != g_debugReadBytesSent)
	{
		g_debugReadBytesSent = *g_debugReadBytes;
		if (!ip_debugSocket[1])
			MyAssertHandler(".\\win32\\win_net.cpp", 1728, 0, "%s", "ip_debugSocket[DEBUG_SOCKET_MSG_REPLY_CHANNEL]");
		if (send(ip_debugSocket[1], g_debugReadBytes, 4, 0) == -1)
		{
			v0 = NET_ErrorString();
			v1 = va("Sys_SendDebugReadBytes: %s", v0);
			Sys_DebugSocketError(v1);
		}
	}
}

void __cdecl Sys_SendDebugReadBytes(int read)
{
	*g_debugReadBytes += read;
	if (*g_debugReadBytes - g_debugReadBytesSent >= 0x2000)
		Sys_SendDebugReadBytesInternal();
}

void __cdecl Sys_DebugSend(int channel, const char *buf, int len, const char *name);

int __cdecl Sys_ReadDebugSocketMessageType(unsigned __int8 *type, int blocking)
{
	return Sys_ReadDebugSocketData((char*)type, 1, blocking);
}

BOOL __cdecl Sys_DebugCanSend()
{
	return g_debugWriteBytes - g_debugReadBytesRemote <= 40960;
}

void __cdecl Sys_DebugSend(int channel, const char *buf, int len, const char *name)
{
	const char *v4; // eax
	const char *v5; // eax
	const char *v6; // eax
	const char *v7; // eax
	int err; // [esp+0h] [ebp-Ch]
	int debugReadBytesRemote; // [esp+4h] [ebp-8h] BYREF
	int read; // [esp+8h] [ebp-4h]

	if (ip_debugSocket[channel])
	{
		g_debugWriteBytes += len;
		while (!Sys_DebugCanSend())
		{
			while (1)
			{
				if (!ip_debugSocket[1])
					MyAssertHandler(".\\win32\\win_net.cpp", 1887, 0, "%s", "ip_debugSocket[DEBUG_SOCKET_MSG_REPLY_CHANNEL]");
				read = recvfrom(ip_debugSocket[1], (char*)&debugReadBytesRemote, 4, 0, 0, 0);
				if (read == -1)
					break;
				g_debugReadBytesRemote = debugReadBytesRemote;
			}
			err = WSAGetLastError();
			if (err != 10035)
			{
				if (err == 10054)
				{
					Sys_DebugSocketError("Sys_DebugSend: Socket closed");
				}
				else
				{
					v4 = NET_ErrorString();
					v5 = va("Sys_DebugSend: %s", v4);
					Sys_DebugSocketError(v5);
				}
				return;
			}
			if (!Sys_DebugCanSend())
				NET_Sleep(1);
		}
		if (!ip_debugSocket[channel])
			MyAssertHandler(".\\win32\\win_net.cpp", 1911, 0, "%s", "ip_debugSocket[channel]");
		while (send(ip_debugSocket[channel], buf, len, 0) == -1)
		{
			if (WSAGetLastError() != 10035)
			{
				v6 = NET_ErrorString();
				v7 = va("%s: %s", name, v6);
				Sys_DebugSocketError(v7);
				return;
			}
			NET_Sleep(1);
		}
	}
	else
	{
		Sys_DebugSocketError("Sys_DebugSend: Socket closed");
	}
}

