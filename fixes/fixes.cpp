/*  
 *  Version: MPL 1.1
 *  
 *  The contents of this file are subject to the Mozilla Public License Version 
 *  1.1 (the "License"); you may not use this file except in compliance with 
 *  the License. You may obtain a copy of the License at 
 *  http://www.mozilla.org/MPL/
 *  
 *  Software distributed under the License is distributed on an "AS IS" basis,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 *  for the specific language governing rights and limitations under the
 *  License.
 *  
 *  The Original Code is the fixes 1.0 SA:MP plugin.
 *  
 *  The Initial Developer of the Original Code is Alex "Y_Less" Cole.
 *  Portions created by the Initial Developer are Copyright (C) 2010
 *  the Initial Developer. All Rights Reserved.
 *  
 *  Contributor(s):
 *
 *  SA:MP team - plugin framework.
 *  
 *  Special Thanks to:
 *  
 *  SA:MP Team past, present and future
 */

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <queue>
#include <deque>
#include <map>
#include <functional>

#include <iostream>

#include "fixes.h"

#include "SDK/amx/amx.h"
#include "SDK/plugincommon.h"

#ifdef LINUX
	#include <sys/mman.h>
	#include <time.h>
	
	long long unsigned int
		MicrosecondTime()
	{
		struct timespec
			ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		//return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	}
#else
	#define VC_EXTRALEAN
	#define WIN32_LEAN_AND_MEAN
	
	#include <windows.h>
	
	long long unsigned int
		gFreq;

	long long unsigned int
		MicrosecondTime()
	{
		LARGE_INTEGER
			ts;
		QueryPerformanceCounter(&ts);
		return ts.QuadPart * 1000000 / gFreq;
	}
#endif

//----------------------------------------------------------

#define STR_PARAM(amx,param,result)                                                          \
	do {                                                                                     \
		cell * amx_cstr_; int amx_length_;                                                   \
		amx_GetAddr((amx), (param), &amx_cstr_);                                             \
		amx_StrLen(amx_cstr_, &amx_length_);                                                 \
		if (amx_length_ > 0) {                                                               \
			if (((result) = (char *)alloca((amx_length_ + 1) * sizeof (*(result)))) != NULL) \
				amx_GetString((result), amx_cstr_, sizeof (*(result)) > 1, amx_length_ + 1); \
			else {                                                                           \
				logprintf("fixes.plugin: Unable to allocate memory.");                       \
				return 0; } }                                               \
		else (result) = ""; }                                                                \
	while (false)

enum
	PARAM_TYPE
{
	PARAM_TYPE_CELL,
	PARAM_TYPE_ARRAY,
	PARAM_TYPE_STRING,
};

struct
	params_s
{
	enum PARAM_TYPE
		type;
	struct params_s *
		next;
	cell
		free;
	cell
		numData; // Or length.
	cell
		arrayData[0];
};

struct
	timer_s
{
	AMX *
		amx;
	int
		id,
		func,
		repeat;
	long long unsigned int
		interval,
		trigger;
	struct params_s *
		params; // 0 means no parameters (SetTimer).
};

// Define an ordering for timers, so that they get inserted in the order they
// are to be next triggered.
struct TimerCompare : public std::binary_function<struct timer_s *, struct timer_s *, bool>
{
	bool
		operator()(struct timer_s * a, struct timer_s * b)
	{
		// Make the one with the smallest trigger value the next one to run.
		return a->trigger > b->trigger;
	};
};

logprintf_t
	logprintf;

// Order the timers by trigger time.
std::priority_queue<
		// Store times.
		struct timer_s *,
		// Use a deque as the underlying storage.
		std::deque<struct timer_s *>,
		// Order based only on next trigger time.
		TimerCompare>
	gTimers;

std::map<int, struct timer_s *>
	gHandles;

extern void *
	pAMXFunctions;

int
	gAMXPtr[17];

AMX *
	gAMXFiles[17];

char
	gLogprintfAssembly[5];

bool
	bInPrint;

unsigned int
	gCurrentTimer = 0;

//AMX_NATIVE
//	SetTimer;

void
	AssemblySwap(char * addr, char * dat, int len)
{
	char
		swp;
	while (len--)
	{
		swp = addr[len];
		addr[len] = dat[len];
		dat[len] = swp;
	}
}

void
	AssemblyRedirect(void * from, void * to, char * ret)
{
	#ifdef LINUX
		size_t
			iPageSize = getpagesize(),
			iAddr = ((reinterpret_cast <uint32_t>(from) / iPageSize) * iPageSize),
			iCount = (5 / iPageSize) * iPageSize + iPageSize * 2;
		mprotect(reinterpret_cast <void*>(iAddr), iCount, PROT_READ | PROT_WRITE | PROT_EXEC);
		//mprotect(from, 5, PROT_READ | PROT_WRITE | PROT_EXEC);
	#else
		DWORD
			old;
		VirtualProtect(from, 5, PAGE_EXECUTE_READWRITE, &old);
	#endif
	*((unsigned char *)ret) = 0xE9;
	*((char **)(ret + 1)) = (char *)((char *)to - (char *)from) - 5;
	//std::cout << "fixes.plugin: load: " << (sizeof (char *)) << *((int *)(ret + 1)) << std::endl;
	AssemblySwap((char *)from, ret, 5);
}

//__declspec(naked)
//__cdecl
int
	FIXES_logprintf(char * str, ...)
{
	va_list ap;
	char
		dst[1024];
	va_start(ap, str);
	vsnprintf(dst, 1024, str, ap);
	va_end(ap);
	printf("%s\n", dst);
	if (!bInPrint)
	{
		// So we can use "printf" without getting stuck in endless loops.
		bInPrint = true;
		//std::cout << "fixes.plugin: " << dst << std::endl;
		for (int i = 0; i != 17; ++i)
		{
			if (gAMXPtr[i] != -1)
			{
				cell
					ret,
					addr;
				amx_PushString(gAMXFiles[i], &addr, 0, dst, 0, 0);
				amx_Exec(gAMXFiles[i], &ret, gAMXPtr[i]);
				amx_Release(gAMXFiles[i], addr);
				if (ret == 1)
				{
					bInPrint = false;
					return 1;
				}
			}
		}
		bInPrint = false;
	}
	//asm
	//{
	//	
	//}
	return 1;
}

void
	DestroyTimer(struct timer_s * t)
{
	struct params_s
		* p = t->params,
		* o;
	while (p)
	{
		o = p->next;
		free((void *)p);
		p = o;
	}
	gHandles.erase(t->id);
	delete t;
}

PLUGIN_EXPORT int PLUGIN_CALL
	ProcessTick()
{
	long long unsigned int
		time = MicrosecondTime();
	//logprintf("Process %d", time);
	while (!gTimers.empty())
	{
		struct timer_s *
			next = gTimers.top();
		if (next->trigger > time)
		{
			return 1;
		}
		else
		{
			gTimers.pop();
			//logprintf("Triggered: %d %d", next->func, next->interval);
			if (next->repeat)
			{
				struct params_s *
					p0 = next->params;
				while (p0)
				{
					switch (p0->type)
					{
						case PARAM_TYPE_CELL:
						{
							amx_Push(next->amx, p0->numData);
							break;
						}
						case PARAM_TYPE_ARRAY:
						case PARAM_TYPE_STRING:
						{
							// These are actually done the same way because we
							// just store the AMX string representation, not the
							// C char* representation.  Just remember the NULL!
							amx_PushArray(next->amx, &p0->free, 0, p0->arrayData, p0->numData);
							break;
						}
					}
					p0 = p0->next;
				}
				cell
					ret;
				amx_Exec(next->amx, &ret, next->func);
				// Free things.
				p0 = next->params;
				while (p0)
				{
					switch (p0->type)
					{
						case PARAM_TYPE_ARRAY:
						case PARAM_TYPE_STRING:
						{
							amx_Release(next->amx, p0->free);
							p0->free = 0;
							break;
						}
					}
					p0 = p0->next;
				}
				switch (next->repeat)
				{
					case 1:
						DestroyTimer(next);
						break;
					default:
						--next->repeat;
					case -1:
						// Don't rely on the current time or we'll get errors
						// compounded.
						next->trigger += next->interval;
						gTimers.push(next);
						break;
				}
			}
			else
			{
				// Used by "KillTimer".
				DestroyTimer(next);
			}
		}
	}
	return 1;
}

static cell
	SetTimer_(AMX * amx, cell func, cell delay, cell interval, cell count, cell format, cell * params)
{
	// Advanced version of SetTimer.  Takes four main parameters so that we can
	// have offsets on timers (so they may start after 10ms, then run once every
	// 5ms say), and a COUNT for how many times to run the function!
	// First, find the given function.
	//logprintf("Adding");
	if (delay >= -1 && interval >= 0 && count >= -1)
	{
		char *
			fname;
		STR_PARAM(amx, func, fname);
		int
			idx;
		if (amx_FindPublic(amx, fname, &idx))
		{
			logprintf("fixes.plugin: Could not find function %s.", fname);
		}
		else
		{
			struct timer_s *
				timer;
			try
			{
				timer = new struct timer_s;
			}
			catch (...)
			{
				logprintf("fixes.plugin: Unable to allocate memory.");
				return 0;
			}
			timer->id = ++gCurrentTimer;
			timer->amx = amx;
			timer->func = idx;
			timer->interval = interval * 1000;
			// Need to somehow get the current time.  There is a handy trick here
			// with negative numbers (i.e -1 being "almost straight away").
			timer->trigger = MicrosecondTime() + delay * 1000;
			timer->params = 0;
			timer->repeat = count;
			gTimers.push(timer);
			// Add this timer to the map of timers.
			gHandles[gCurrentTimer] = timer;
			//logprintf("Added %d", timer->trigger);
			if (format)
			{
				char *
					fmat;
				STR_PARAM(amx, format, fmat);
				idx = 0;
				for ( ; ; )
				{
					switch (*fmat++)
					{
						case '\0':
						{
							if (gCurrentTimer == 0xFFFFFFFF)
							{
								logprintf("fixes.plugin: 4294967295 timers created.");
							}
							return (cell)gCurrentTimer;
						}
						case 'i': case 'f': case 'x': case 'h': case 'b': case 'c': case 'l':
						case 'I': case 'F': case 'X': case 'H': case 'B': case 'C': case 'L':
						{
							struct params_s *
								p0 = (struct params_s *)malloc(sizeof (struct params_s));
							if (p0)
							{
								cell *
									cstr;
								amx_GetAddr(amx, params[idx++], &cstr);
								p0->free = 0;
								p0->type = PARAM_TYPE_CELL;
								p0->numData = *cstr; //params[idx++];
								// Construct the list backwards.  Means we don't
								// need to worry about finding the latest one OR
								// the push order, so serves two purposes.
								p0->next = timer->params;
								timer->params = p0;
							}
							else
							{
								DestroyTimer(timer);
								logprintf("fixes.plugin: Unable to allocate memory.");
								return 0;
							}
							break;
						}
						case 's': case 'S':
						{
							cell *
								cstr;
							int
								len;
							amx_GetAddr(amx, params[idx++], &cstr);
							amx_StrLen(cstr, &len);
							struct params_s *
								p0 = (struct params_s *)malloc(sizeof (struct params_s) + len * sizeof (cell) + sizeof (cell));
							if (p0)
							{
								p0->free = 0;
								p0->type = PARAM_TYPE_STRING;
								p0->numData = len + 1;
								memcpy(p0->arrayData, cstr, len * sizeof (cell) + sizeof (cell));
								p0->next = timer->params;
								timer->params = p0;
							}
							else
							{
								DestroyTimer(timer);
								logprintf("fixes.plugin: Unable to allocate memory.");
								return 0;
							}
							break;
						}
						case 'a': case 'A':
						{
							switch (*fmat)
							{
								case 'i': case 'x': case 'h': case 'b':
								case 'I': case 'X': case 'H': case 'B':
								{
									cell *
										cstr;
									amx_GetAddr(amx, params[idx++], &cstr);
									int
										len = params[idx];
									struct params_s *
										p0 = (struct params_s *)malloc(sizeof (struct params_s) + len * sizeof (cell));
									if (p0)
									{
										p0->free = 0;
										p0->type = PARAM_TYPE_ARRAY;
										p0->numData = len;
										memcpy(p0->arrayData, cstr, len * sizeof (cell));
										p0->next = timer->params;
										timer->params = p0;
									}
									else
									{
										DestroyTimer(timer);
										logprintf("fixes.plugin: Unable to allocate memory.");
										return 0;
									}
									break;
								}
								default:
								{
									logprintf("fixes.plugin: Array with no length.");
								}
							}
							break;
						}
					}
				}
			}
			else
			{
				if (gCurrentTimer == 0xFFFFFFFF)
				{
					logprintf("fixes.plugin: 4294967295 timers created.");
				}
				return (cell)gCurrentTimer;
			}
		}
	}
	else
	{
		logprintf("fixes.plugin: Invalid timer parameter.");
	}
	return 0;
}

// native SetTimer(const function[], const delay, const repeat);
static cell AMX_NATIVE_CALL
	n_SetTimer(AMX * amx, cell * params)
{
	CHECK_PARAMS_EQ(params, 3);
	return SetTimer_(amx, params[1], params[2], params[2], params[3] ? -1 : 1, 0, 0);
}

// native SetTimerEx(const function[], const delay, const repeat, const format[], {Float, _}:...);
static cell AMX_NATIVE_CALL
	n_SetTimerEx(AMX * amx, cell * params)
{
	CHECK_PARAMS_GTE(params, 4);
	return SetTimer_(amx, params[1], params[2], params[2], params[3] ? -1 : 1, params[4], params + 5);
}

// native SetTimer_(const function[], const delay, const interval, const count);
static cell AMX_NATIVE_CALL
	n_SetTimer_(AMX * amx, cell * params)
{
	CHECK_PARAMS_EQ(params, 4);
	return SetTimer_(amx, params[1], params[2], params[3], params[4], 0, 0);
}

// native SetTimerEx_(const function[], const delay, const interval, const count, const format[], {Float, _}:...);
static cell AMX_NATIVE_CALL
	n_SetTimerEx_(AMX * amx, cell * params)
{
	CHECK_PARAMS_GTE(params, 5);
	return SetTimer_(amx, params[1], params[2], params[3], params[4], params[5], params + 6);
}

// native KillTimer(timer);
static cell AMX_NATIVE_CALL
	n_KillTimer(AMX * amx, cell * params)
{
	CHECK_PARAMS_EQ(params, 1);
	// Look up to see if this timer is valid and running.
	std::map<int, struct timer_s *>::iterator
		it = gHandles.find(params[1]);
	if (it != gHandles.end() && (*it).second->amx == amx)
	{
		// Can't remove it yet because it's stuck in a queue.
		(*it).second->repeat = 0;
		gHandles.erase(params[1]);
	}
	return 0;
}

#if SSCANF_QUIET
	void
		qlog(char * str, ...)
	{
		// Do nothing
	}
#endif

//----------------------------------------------------------
// The Support() function indicates what possibilities this
// plugin has. The SUPPORTS_VERSION flag is required to check
// for compatibility with the server. 

PLUGIN_EXPORT unsigned int PLUGIN_CALL
	Supports() 
{
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

//----------------------------------------------------------
// The Load() function gets passed on exported functions from
// the SA-MP Server, like the AMX Functions and logprintf().
// Should return true if loading the plugin has succeeded.

PLUGIN_EXPORT bool PLUGIN_CALL
	Load(void ** ppData)
{
	setlocale(LC_CTYPE, "");
	pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
	logprintf = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];
	//GetServer = (GetServer_t)ppData[0xE1];
	for (int i = 0; i != 17; ++i)
	{
		gAMXFiles[i] = 0;
		gAMXPtr[i] = -1;
	}
	//logprintf("0x%08X\n", (int)logprintf);
	logprintf("\n");
	logprintf(" ===============================\n");
	logprintf("       fixes plugin loaded.     \n");
	logprintf("   (c) 2012 Alex \"Y_Less\" Cole\n");
	logprintf(" ===============================\n");
	
	AssemblyRedirect(logprintf, FIXES_logprintf, gLogprintfAssembly);
	bInPrint = false;
	
	#ifndef LINUX
	LARGE_INTEGER
		ts;
	QueryPerformanceFrequency(&ts);
	gFreq = ts.QuadPart;
	#endif
	
	#if SSCANF_QUIET
		logprintf = qlog;
	#endif
	return true;
}

//----------------------------------------------------------
// The Unload() function is called when the server shuts down,
// meaning this plugin gets shut down with it.

PLUGIN_EXPORT void PLUGIN_CALL
	Unload()
{
	//logprintf("\n");
	//logprintf(" ===============================\n");
	//logprintf("     sscanf plugin unloaded.    \n");
	//logprintf(" ===============================\n");
}

//----------------------------------------------------------
// The AmxLoad() function gets called when a new gamemode or
// filterscript gets loaded with the server. In here we register
// the native functions we like to add to the scripts.

AMX_NATIVE_INFO
	sscanfNatives[] =
		{
			{"SetTimer_", n_SetTimer_},
			{"SetTimerEx_", n_SetTimerEx_},
			//{"KillTimer_", n_KillTimer_},
			{0,        0}
		};

// From "amx.c", part of the PAWN language runtime:
// http://code.google.com/p/pawnscript/source/browse/trunk/amx/amx.c

#define USENAMETABLE(hdr) \
	((hdr)->defsize==sizeof(AMX_FUNCSTUBNT))

#define NUMENTRIES(hdr,field,nextfield) \
	(unsigned)(((hdr)->nextfield - (hdr)->field) / (hdr)->defsize)

#define GETENTRY(hdr,table,index) \
	(AMX_FUNCSTUB *)((unsigned char*)(hdr) + (unsigned)(hdr)->table + (unsigned)index*(hdr)->defsize)

#define GETENTRYNAME(hdr,entry) \
	(USENAMETABLE(hdr) ? \
		(char *)((unsigned char*)(hdr) + (unsigned)((AMX_FUNCSTUBNT*)(entry))->nameofs) : \
		((AMX_FUNCSTUB*)(entry))->name)

void
	Redirect(AMX * amx, char const * const from, ucell to, AMX_NATIVE * store)
{
	int
		num,
		idx;
	// Operate on the raw AMX file, don't use the amx_ functions to avoid issues
	// with the fact that we've not actually finished initialisation yet.  Based
	// VERY heavilly on code from "amx.c" in the PAWN runtime library.
	AMX_HEADER *
		hdr = (AMX_HEADER *)amx->base;
	AMX_FUNCSTUB *
		func;
	num = NUMENTRIES(hdr, natives, libraries);
	//logprintf("Redirect 1");
	for (idx = 0; idx != num; ++idx)
	{
		func = GETENTRY(hdr, natives, idx);
		//logprintf("Redirect 2 \"%s\" \"%s\"", from, GETENTRYNAME(hdr, func));
		if (!strcmp(from, GETENTRYNAME(hdr, func)))
		{
			//logprintf("Redirect 3");
			// Intercept the call!
			if (store)
			{
				*store = (AMX_NATIVE)func->address;
			}
			func->address = to;
			break;
		}
	}
}

PLUGIN_EXPORT int PLUGIN_CALL
	AmxLoad(AMX * amx) 
{
	//Redirect(amx, "SetPlayerName", (ucell)n_SSCANF_SetPlayerName, &SetPlayerName);
	//Redirect(amx, "SetPlayerName", (ucell)n_SSCANF_SetPlayerName, 0);
	Redirect(amx, "SetTimer", (ucell)n_SetTimer, 0);
	Redirect(amx, "KillTimer", (ucell)n_KillTimer, 0);
	Redirect(amx, "SetTimerEx", (ucell)n_SetTimerEx, 0);
	//Redirect(amx, "print", (ucell)n_print, 0);
	//Redirect(amx, "printf", (ucell)n_printf, 0);
	for (int i = 0; i != 17; ++i)
	{
		if (gAMXFiles[i] == 0)
		{
			gAMXFiles[i] = amx;
			int
				idx;
			if (amx_FindPublic(amx, "OnServerMessage", &idx))
			{
				//printf("NO CALLBACK\n");
				gAMXPtr[i] = -1;
			}
			else
			{
				//printf("GOT CALLBACK %d\n", idx);
				gAMXPtr[i] = idx;
			}
			break;
		}
	}
	return amx_Register(amx, sscanfNatives, -1);
}

//----------------------------------------------------------
// When a gamemode is over or a filterscript gets unloaded, this
// function gets called. No special actions needed in here.

PLUGIN_EXPORT int PLUGIN_CALL
	AmxUnload(AMX * amx) 
{
	std::priority_queue<struct timer_s *, std::deque<struct timer_s *>, TimerCompare>
		cleaned;
	// Destroy all the timers for this mode.
	while (!gTimers.empty())
	{
		struct timer_s *
			next = gTimers.top();
		gTimers.pop();
		if (next->amx == amx)
		{
			// Ending, remove from the map also.
			gHandles.erase(next->id);
		}
		else
		{
			// Not ending, leave it be.
			cleaned.push(next);
		}
	}
	gTimers = cleaned;
	for (int i = 0; i != 17; ++i)
	{
		if (gAMXFiles[i] == amx)
		{
			gAMXFiles[i] = 0;
			break;
		}
	}
	return AMX_ERR_NONE;
}
