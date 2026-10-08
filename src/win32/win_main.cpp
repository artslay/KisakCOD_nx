//#include "../qcommon/exe_headers.h"

//#include "../client/client.h
#include <universal/q_shared.h>
#include "win_configure.h"
#include "win_local.h"
#include "win_localize.h"
#include "win_net.h"
#include "win_net_debug.h"
#include "win_steam.h"

//#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

#include <Windows.h>
#include <tlhelp32.h>

#include <client/client.h>

#include <qcommon/qcommon.h>
#include <qcommon/cmd.h>
#include <qcommon/threads.h>
#include <qcommon/mem_track.h>

#include <universal/com_memory.h>
#include <universal/q_parse.h>
#include <universal/timing.h>

#include <gfx_d3d/r_init.h>
#include <universal/profile.h>

//#include "../qcommon/stringed_ingame.h"

char sys_cmdline[1024];
char sys_exitCmdLine[1024];

HWND g_splashWnd;

sysEvent_t eventQue[0x100];

int eventHead;
int eventTail;

SysInfo sys_info;

int client_state;

cmd_function_s Sys_In_Restart_f_VAR;
#ifdef KISAK_MP
cmd_function_s Sys_Net_Restart_f_VAR;
cmd_function_s Sys_Listen_f_VAR;
#endif

WinVars_t	g_wv;

static char sys_processSemaphoreFile[0x20];

static void PrintWorkingDir()
{
	char cwd[260];

	_getcwd(cwd, 256);
	Com_Printf(CON_CHANNEL_SYSTEM, "Working directory: %s\n", cwd);
}

static void Win_RegisterClass()
{
	tagWNDCLASSEXA wce{};

	wce.cbSize = sizeof(wce);
	wce.lpfnWndProc = MainWndProc;
	wce.hInstance = g_wv.hInstance;
	wce.hIcon = LoadIconA(g_wv.hInstance, (LPCSTR)1);
	wce.hCursor = LoadCursorA(0, (LPCSTR)0x7F00); // KISAKTODO figure resources out
	wce.hbrBackground = CreateSolidBrush(0);
	wce.lpszClassName = "CoD4";

	if (!RegisterClassExA(&wce))
		Com_Error(ERR_FATAL, "EXE_ERR_COULDNT_REGISTER_WINDOW");
}

sysEvent_t* __cdecl Win_GetEvent(sysEvent_t* result)
{
	PROF_SCOPED("Win_GetEvent");

	size_t v2; // [esp+0h] [ebp-50h]
	char* b; // [esp+10h] [ebp-40h]
	tagMSG msg; // [esp+18h] [ebp-38h] BYREF
	char* s; // [esp+34h] [ebp-1Ch]
	sysEvent_t ev; // [esp+38h] [ebp-18h] BYREF

	Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
	if (eventHead <= eventTail)
	{
		{
			PROF_SCOPED("Message Pump");

			while (PeekMessageA(&msg, 0, 0, 0, 0))
			{
				if (!GetMessageA(&msg, 0, 0, 0))
					Com_Quit_f();
				g_wv.sysMsgTime = msg.time;
				TranslateMessage(&msg);
				DispatchMessageA(&msg);
			}
		}

		{
			PROF_SCOPED("Console Input");
			s = Sys_ConsoleInput();
			if (s)
			{
				v2 = strlen(s);
				b = (char *)Com_AllocEvent(v2 + 1);
				I_strncpyz(b, s, v2);
				Sys_QueEvent(0, SE_CONSOLE, 0, 0, v2 + 1, b);
			}
		}

		if (eventHead <= eventTail)
		{
			memset(&ev, 0, sizeof(ev));
			ev.evTime = Sys_Milliseconds();
		}
		else
		{
			ev = eventQue[(unsigned __int8)eventTail++];
		}
		Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
		*result = ev;
		return result;
	}
	else
	{
		ev = eventQue[(unsigned __int8)eventTail++];
		Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
		*result = ev;
		return result;
	}
}

static int Sys_GetSemaphoreFileName()
{
	char* i; // [esp+0h] [ebp-118h]
	const char* moduleName; // [esp+4h] [ebp-114h]
	char modulePath[268]; // [esp+8h] [ebp-110h] BYREF

	GetModuleFileNameA(0, modulePath, MAX_PATH);
	modulePath[MAX_PATH - 1] = 0;
	moduleName = modulePath;
	for (i = modulePath; *i; ++i)
	{
		if (*i == '\\' || *i == ':')
		{
			moduleName = i + 1;
		}
		else if (*i == '.')
		{
			*i = 0;
		}
	}

	return sprintf_s(sys_processSemaphoreFile, "__%s", moduleName);
}

void __cdecl Sys_QuitAndStartProcess(const char *exeName, const char *parameters)
{
	char pathOrig[268]; // [esp+0h] [ebp-110h] BYREF

	GetCurrentDirectoryA(0x104u, pathOrig);
	if (parameters)
		Com_sprintf(sys_exitCmdLine, 0x400u, "\"%s\\%s\" %s", pathOrig, exeName, parameters);
	else
		Com_sprintf(sys_exitCmdLine, 0x400u, "\"%s\\%s\"", pathOrig, exeName);
	Cbuf_AddText(0, "quit\n");
}

int __cdecl Sys_IsGameProcess(DWORD id)
{
	tagMODULEENTRY32 me; // [esp+0h] [ebp-350h] BYREF
	int isGame; // [esp+22Ch] [ebp-124h]
	char* i; // [esp+230h] [ebp-120h]
	char* moduleName; // [esp+234h] [ebp-11Ch]
	char modulePath[268]; // [esp+238h] [ebp-118h] BYREF
	void* snapshot; // [esp+348h] [ebp-8h]
	void* process; // [esp+34Ch] [ebp-4h]

	process = OpenProcess(0x1F0FFFu, 0, id);
	if (!process)
		return 0;
	CloseHandle(process);
	snapshot = CreateToolhelp32Snapshot(8u, id);
	if (snapshot == (void*)-1)
		return 0;
	isGame = 0;
	me.dwSize = 548;
	if (Module32First(snapshot, &me))
	{
		GetModuleFileNameA(0, modulePath, 0x104u);
		modulePath[259] = 0;
		moduleName = modulePath;
		for (i = modulePath; *i; ++i)
		{
			if (*i == 92 || *i == 58)
				moduleName = i + 1;
		}
		while (I_stricmp(me.szModule, moduleName))
		{
			if (!Module32Next(snapshot, &me))
				goto LABEL_15;
		}
		isGame = 1;
	}
LABEL_15:
	CloseHandle(snapshot);
	return isGame;
}

int __cdecl Sys_CheckCrashOrRerun()
{
#ifdef KISAK_PURE
	HWND ActiveWindow; // eax
	char* v2; // [esp-Ch] [ebp-20h]
	char* v3; // [esp-8h] [ebp-1Ch]
	uint32_t procID; // [esp+0h] [ebp-14h] BYREF
	int answer; // [esp+4h] [ebp-10h]
	DWORD byteCount; // [esp+8h] [ebp-Ch] BYREF
	void* file; // [esp+Ch] [ebp-8h]
	uint32_t id; // [esp+10h] [ebp-4h] BYREF

	if (!sys_processSemaphoreFile[0])
		return 1;
	procID = GetCurrentProcessId();
	file = CreateFileA(sys_processSemaphoreFile, 0x80000000, 0, 0, 3u, 2u, 0);
	if (file != (void*)-1)
	{
		if (ReadFile(file, &id, 4u, &byteCount, 0) && byteCount == 4)
		{
			CloseHandle(file);
			if (procID != id && Sys_IsGameProcess(id))
				return 0;
			v3 = Win_LocalizeRef("WIN_IMPROPER_QUIT_TITLE");
			v2 = Win_LocalizeRef("WIN_IMPROPER_QUIT_BODY");
			ActiveWindow = GetActiveWindow();
			answer = MessageBoxA(ActiveWindow, v2, v3, 0x33u);
			if (answer == 6)
			{
				Com_ForceSafeMode();
			}
			else if (answer == 2)
			{
				return 0;
			}
		}
		else
		{
			CloseHandle(file);
		}
	}
	file = CreateFileA(sys_processSemaphoreFile, 0x40000000u, 0, 0, 2u, 2u, 0);
	if (file == (void*)-1)
		Sys_NoFreeFilesError();
	if (!WriteFile(file, &procID, 4u, &byteCount, 0) || byteCount != 4)
	{
		CloseHandle(file);
		Sys_NoFreeFilesError();
	}
	CloseHandle(file);
	return 1;
#else
	return 1; // LWSS: Disable the "Do you wanna startup in Safe Mode?!" Prompt. 
#endif
}

void Sys_SpawnQuitProcess()
{
	const char* v0; // eax
	_STARTUPINFOA dst; // [esp+0h] [ebp-60h] BYREF
	void* msgBuf; // [esp+48h] [ebp-18h] BYREF
	_PROCESS_INFORMATION pi; // [esp+4Ch] [ebp-14h] BYREF
	uint32_t error; // [esp+5Ch] [ebp-4h]

	if (sys_exitCmdLine[0])
	{
		memset((unsigned __int8*)&dst, 0, sizeof(dst));
		dst.cb = 68;
		if (!CreateProcessA(0, sys_exitCmdLine, 0, 0, 0, 0, 0, 0, &dst, &pi))
		{
			error = GetLastError();
			FormatMessageA(0x1300u, 0, error, 0x400u, (LPSTR)&msgBuf, 0, 0);
			v0 = va("EXE_ERR_COULDNT_START_PROCESS", sys_exitCmdLine, msgBuf, error);
			Com_Error(ERR_DROP, v0);
		}
	}
}

void __cdecl Sys_QueEvent(uint32_t time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr)
{
	sysEvent_t *ev; // [esp+0h] [ebp-4h]

	Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
	ev = &eventQue[(unsigned __int8)eventHead];
	if (eventHead - eventTail >= 256)
	{
		Com_Printf(CON_CHANNEL_SYSTEM, "Sys_QueEvent: overflow\n");
		if (ev->evPtr)
			Z_Free((char *)ev->evPtr, 10);
		++eventTail;
	}
	++eventHead;
	if (!time)
		time = Sys_Milliseconds();
	ev->evTime = time;
	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
	Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_ShutdownEvents()
{
	sysEvent_t *ev; // [esp+0h] [ebp-4h]

	Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
	while (eventHead > eventTail)
	{
		ev = &eventQue[(unsigned __int8)eventTail++];
		if (ev->evPtr)
			Z_Free((char *)ev->evPtr, 10);
	}
	Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_In_Restart_f()
{
	IN_Shutdown();
	IN_Init();
}

#ifdef KISAK_MP
void Sys_Net_Restart_f()
{
	NET_Restart();
}
#endif

void __cdecl Sys_CreateSplashWindow()
{
	HWND__* hwnd; // [esp+0h] [ebp-54h]
	int bmpSize; // [esp+4h] [ebp-50h]
	int bmpSize_4; // [esp+8h] [ebp-4Ch]
	tagWNDCLASSA wc; // [esp+Ch] [ebp-48h] BYREF
	void* hbmp; // [esp+34h] [ebp-20h]
	int style; // [esp+38h] [ebp-1Ch]
	tagSIZE screenSize; // [esp+3Ch] [ebp-18h]
	tagRECT rc; // [esp+44h] [ebp-10h] BYREF

	wc.style = 0;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.lpszMenuName = 0;
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = g_wv.hInstance;
	wc.hIcon = LoadIconA(g_wv.hInstance, (LPCSTR)1);
	wc.hCursor = LoadCursorA(0, (LPCSTR)0x7F00);
	wc.hbrBackground = (HBRUSH__*)6;
	wc.lpszClassName = "CoD Splash Screen";
	if (RegisterClassA(&wc))
	{
		screenSize.cx = GetSystemMetrics(16);
		screenSize.cy = GetSystemMetrics(17);
		hbmp = LoadImageA(0, "cod.bmp", 0, 0, 0, 0x10u);
		if (hbmp)
		{
			g_splashWnd = CreateWindowExA(
				0x40000u,
				"CoD Splash Screen",
#ifdef KISAK_MP
				"Call of Duty 4 Multiplayer",
#elif KISAK_SP
				"Call of Duty 4",
#endif
				0x80880000,
				(screenSize.cx - 320) / 2,
				(screenSize.cy - 100) / 2,
				320,
				100,
				0,
				0,
				g_wv.hInstance,
				0);
			if (g_splashWnd)
			{
				style = 0x5000000E;
				hwnd = CreateWindowExA(0, "Static", 0, 0x5000000Eu, 0, 0, 320, 100, g_splashWnd, 0, g_wv.hInstance, 0);
				if (hwnd)
				{
					SendMessageA(hwnd, 0x172u, 0, (LPARAM)hbmp);
					GetWindowRect(hwnd, &rc);
					bmpSize = rc.right - rc.left + 2;
					bmpSize_4 = rc.bottom - rc.top + 2;
					rc.left = (screenSize.cx - bmpSize) / 2;
					rc.right = bmpSize + rc.left;
					rc.top = (screenSize.cy - bmpSize_4) / 2;
					rc.bottom = bmpSize_4 + rc.top;
					AdjustWindowRect(&rc, style, 0);
					SetWindowPos(g_splashWnd, 0, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 4u);
				}
			}
		}
	}
}
void __cdecl Sys_ShowSplashWindow()
{
	if (g_splashWnd)
	{
		ShowWindow(g_splashWnd, 5);
		UpdateWindow(g_splashWnd);
	}
}

int __cdecl Sys_SystemMemoryMB()
{
	HWND ActiveWindow; // eax
	HWND v2; // eax
	char* v3; // [esp-Ch] [ebp-C8h]
	char* v4; // [esp-Ch] [ebp-C8h]
	char* v5; // [esp-8h] [ebp-C4h]
	char* v6; // [esp-8h] [ebp-C4h]
	float v7; // [esp+30h] [ebp-8Ch]
	float v8; // [esp+40h] [ebp-7Ch]
	int sysMB; // [esp+50h] [ebp-6Ch]
	int sysMBa; // [esp+50h] [ebp-6Ch]
	HINSTANCE__* hm; // [esp+54h] [ebp-68h]
	_MEMORYSTATUS status; // [esp+58h] [ebp-64h] BYREF
	int(__stdcall * MemStatEx)(_MEMORYSTATUSEX*); // [esp+78h] [ebp-44h]
	_MEMORYSTATUSEX statusEx; // [esp+7Ch] [ebp-40h] BYREF

	hm = GetModuleHandleA("kernel32.dll");
	if (hm && (MemStatEx = (int(__stdcall*)(_MEMORYSTATUSEX*))GetProcAddress(hm, "GlobalMemoryStatusEx")) != 0)
	{
		statusEx.dwLength = 64;
		MemStatEx(&statusEx);
		if (statusEx.ullAvailVirtual < 0x8000000)
		{
			v5 = Win_LocalizeRef("WIN_LOW_MEMORY_TITLE");
			v3 = Win_LocalizeRef("WIN_LOW_MEMORY_BODY");
			ActiveWindow = GetActiveWindow();
			if (MessageBoxA(ActiveWindow, v3, v5, 0x34u) != 6)
			{
				Sys_NormalExit();
				exit(0);
			}
		}
		v8 = (double)statusEx.ullTotalPhys * 0.00000095367431640625;
		sysMB = (int)(v8 + 0.4999999990686774);
		if ((double)statusEx.ullTotalPhys > (double)sysMB * 1048576.0 || sysMB > 1024)
			return 1024;
		return sysMB;
	}
	else
	{
		status.dwLength = 32;
		GlobalMemoryStatus(&status);
		if (status.dwAvailVirtual < 0x8000000)
		{
			v6 = Win_LocalizeRef("WIN_LOW_MEMORY_TITLE");
			v4 = Win_LocalizeRef("WIN_LOW_MEMORY_BODY");
			v2 = GetActiveWindow();
			if (MessageBoxA(v2, v4, v6, 0x34u) != 6)
			{
				Sys_NormalExit();
				exit(0);
			}
		}
		v7 = (double)status.dwTotalPhys * 0.00000095367431640625;
		sysMBa = (int)(v7 + 0.4999999990686774);
		if ((double)status.dwTotalPhys > (double)sysMBa * 1048576.0 || sysMBa > 1024)
			return 1024;
		return sysMBa;
	}
}

void Sys_FindInfo()
{
	sys_info.logicalCpuCount = Sys_GetCpuCount();
	sys_info.cpuGHz = 1.0 / (((double)1LL - (double)0LL) * msecPerRawTimerTick * 1000000.0);
	sys_info.sysMB = Sys_SystemMemoryMB();
	Sys_DetectVideoCard(512, sys_info.gpuDescription);
	sys_info.SSE = 1; // KISAKTODO if someone from 1990 time travels and complains Sys_SupportsSSE();
	Sys_DetectCpuVendorAndName(sys_info.cpuVendor, sys_info.cpuName);
	Sys_SetAutoConfigureGHz(&sys_info);
}

/*
==================
WinMain

# BURN, BABY, BURN -- MASTER IGNITION ROUTINE
==================
*/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	// KISAK: make a pretty console in debug mode, redirect in/out/err stream
#if 1 || defined(KISAK_DEBUG)
	AllocConsole();

	SetConsoleTitleA("KisakCOD");
	DeleteMenu(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_BYCOMMAND);

	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
		ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
		ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN |
		ENABLE_LVB_GRID_WORLDWIDE);

	SetConsoleCtrlHandler(nullptr, true);

	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
#endif

	Sys_InitializeCriticalSections();
	Sys_InitMainThread();
	track_init();
	Win_InitLocalization();

	const bool allowDupe = I_strnicmp(lpCmdLine, "allowdupe", 9) && lpCmdLine[9] <= ' ';

	// initialize semaphore
	Sys_GetSemaphoreFileName();

	if (!allowDupe || Sys_CheckCrashOrRerun())
	{
		// should never get a previous instance in Win32
		if (!hPrevInstance)
		{
			Com_InitParse();
			Dvar_Init();
			InitTiming(); 
			Sys_FindInfo();
			g_wv.hInstance = hInstance;
			I_strncpyz(sys_cmdline, lpCmdLine, 1024);
			Sys_CreateSplashWindow();
			Sys_ShowSplashWindow();
			Win_RegisterClass();
			SetErrorMode(1);
			Sys_Milliseconds();
			Profile_Init();
			Profile_InitContext(0);
			KISAK_NULLSUB();
			// LWSS ADD: Steam Init
			Steam_Init();
			// LWSS END
			Com_Init(sys_cmdline);

#ifdef KISAK_MP
			if (!com_dedicated->current.integer)
#endif
			{
				Cbuf_AddText(0, "readStats\n");
			}

			PrintWorkingDir();

			// LWSS: Punkbuster stuff
			//if ((!com_dedicated || !com_dedicated->current.integer) && !PbClientInitialize(hInstance))
			//	Com_SetErrorMessage("MPUI_NOPUNKBUSTER");
			//if (!PbServerInitialize())
			//{
			//	Com_PrintError(CON_CHANNEL_SYSTEM, "Unable to initialize punkbuster.  Punkbuster is disabled\n");
			//	Com_SetErrorMessage("MPUI_NOPUNKBUSTER");
			//}

			SetFocus(g_wv.hWnd);

			// main game loop
			while (1) {
				// if not running as a game client, sleep a bit
#ifdef KISAK_MP
				if (g_wv.isMinimized || (com_dedicated && com_dedicated->current.integer)) 
#elif KISAK_SP
				if (g_wv.isMinimized)
#endif
				{
					Sleep(5);
				}

				// run the game
				Com_Frame();

				// LWSS: Punkbuster stuff
				//if (!com_dedicated || !com_dedicated->integer) {
				//	PbClientProcessEvents();
				//}
				//PbServerProcessEvents();
			}
		}
	}


	Win_ShutdownLocalization();
	track_shutdown(0);
	return 0;
}

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;