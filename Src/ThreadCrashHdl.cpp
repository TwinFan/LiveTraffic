/// @file       ThreadCrashHdl.cpp
/// @brief      Thead handling and Crash Report
/// @see        For thread-local locales see
///             https://stackoverflow.com/a/17173977
/// @see        For Crash Reporter see
///             https://developer.x-plane.com/code-sample/crash-handling/
/// @details    Sets standard settings for worker threads like locale and
///             crashh reporting.
///             Installs our own crash reporter (since X-Plane seems to filter
///             out crashes in plugins and doesn't write a dump any longer
///             in such cases).
/// @author     Birger Hoppe
/// @copyright  (c) 2024 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the following conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.

#include "LiveTraffic.h"

#if APL || LIN
#include <signal.h>
#include <execinfo.h>           // backtrace et al
#include <fcntl.h>
#include <unistd.h>
#endif

//
// MARK: Crash Handler
//       mostly copied verbatim from https://developer.x-plane.com/code-sample/crash-handling/
//

/*
 * This [code] demonstrates how to intercept and handle crashes in a way that plays nicely with the X-Plane crash reporter.
 * The core idea is to intercept all crashes by installing appropriate crash handlers and then filtering out crashes that weren't caused
 * by our plugin. To do so, we track both the threads created by us at runtime, as well as checking whether our plugin is active when we crash on the main thread.
 * If the crash wasn't caused by us, the crash is forwarded to the next crash handler (potentially X-Plane, or another plugin) which then gets a chance
 * to process the crash.
 * If the crash is caused by us, we eat the crash to not falsely trigger X-Planes crash reporter and pollute the X-Plane crash database. This example
 * also writes a minidump file on Windows and a simple backtrace on macOS and Linux. Production code might want to integrate more sophisticated crash handling
 */

static std::thread::id s_main_thread;               ///< X-Plane's main thread's id
static std::atomic_flag s_thread_lock;
static std::set<std::pair<std::thread::id, std::string> > s_known_threads;   ///< list of threads registered by LiveTraffic (through class ThreadSettings)
static XPLMPluginID s_my_plugin_id;                 ///< LiveTraffic's plugin id

static char crash_thread_name [100] = {0};          ///< name of the crashing thread

/// Function called when we detect a crash that was caused by us
#if    APL || LIN
void handle_crash(int sig);
#else
void handle_crash(EXCEPTION_POINTERS *ei);
#endif

#if APL || LIN
static struct sigaction s_prev_sigsegv = {};
static struct sigaction s_prev_sigabrt = {};
static struct sigaction s_prev_sigfpe = {};
static struct sigaction s_prev_sigint = {};
static struct sigaction s_prev_sigill = {};
static struct sigaction s_prev_sigterm = {};

static void handle_posix_sig(int sig, siginfo_t *siginfo, void *context);
#endif

#if IBM
static LPTOP_LEVEL_EXCEPTION_FILTER s_previous_windows_exception_handler;
LONG WINAPI handle_windows_exception(EXCEPTION_POINTERS *ei);
#endif

// Registers the calling thread with the crash handler. We use this to figure out if a crashed thread belongs to us when we later try to figure out if we caused a crash
void CrashHandlerRegisterThread (const char* sThrName)
{
	while(s_thread_lock.test_and_set(std::memory_order_acquire))
	{}
    s_known_threads.emplace(std::this_thread::get_id(), sThrName);
	s_thread_lock.clear(std::memory_order_release);
}

// Unregisters the calling thread from the crash handler. MUST be called at the end of thread that was registered via CrashHandlerRegister()
void CrashHandlerUnregisterThread()
{
	while(s_thread_lock.test_and_set(std::memory_order_acquire))
	{}

    // find and erase (all) thread(s) with current thread's id
    const std::thread::id thread_id = std::this_thread::get_id();
    for (auto iter = s_known_threads.begin();
         iter != s_known_threads.end();)
    {
        if (iter->first == thread_id)
            iter = s_known_threads.erase(iter);
        else
            ++iter;
    }

	s_thread_lock.clear(std::memory_order_release);
}

// Registers the global crash handler. Should be called from XPluginStart
void CrashHandlerRegister()
{
	s_main_thread = std::this_thread::get_id();
	s_my_plugin_id = XPLMGetMyID();

#if APL || LIN
    struct sigaction sig_action = { 0 };
    sig_action.sa_sigaction = handle_posix_sig;

	sigemptyset(&sig_action.sa_mask);

#if	LIN
    static uint8_t alternate_stack[SIGSTKSZ] = { 0 };
    stack_t ss = { 0 };
    ss.ss_sp = (void*)alternate_stack;
	ss.ss_size = SIGSTKSZ:
	ss.ss_flags = 0:

	sigaltstack(&ss, NULL);
	sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
#else
	sig_action.sa_flags = SA_SIGINFO;
#endif

	sigaction(SIGSEGV, &sig_action, &s_prev_sigsegv);
	sigaction(SIGABRT, &sig_action, &s_prev_sigabrt);
	sigaction(SIGFPE, &sig_action, &s_prev_sigfpe);
	sigaction(SIGINT, &sig_action, &s_prev_sigint);
	sigaction(SIGILL, &sig_action, &s_prev_sigill);
	sigaction(SIGTERM, &sig_action, &s_prev_sigterm);
    
    // We call backtrace once in a safe place, so that all potential dlopen
    // already takes place here, making backtrace a little more AS safe
    void *frames[64] = {0};
    backtrace(frames, 64);
#endif
	
#if IBM
	// Load the debug helper library into the process already, this way we don't have to hit the dynamic loader
	// in an exception context where it's potentially unsafe to do so.
	HMODULE module = ::GetModuleHandleA("dbghelp.dll");
	if(!module)
		module = ::LoadLibraryA("dbghelp.dll");

	(void)module;
	s_previous_windows_exception_handler = SetUnhandledExceptionFilter(handle_windows_exception);
#endif
}

// Unregisters the global crash handler. You need to call this in XPluginStop so we can clean up after ourselves
void CrashHandlerUnregister()
{
#if APL || LIN
	sigaction(SIGSEGV, &s_prev_sigsegv, NULL);
	sigaction(SIGABRT, &s_prev_sigabrt, NULL);
	sigaction(SIGFPE, &s_prev_sigfpe, NULL);
	sigaction(SIGINT, &s_prev_sigint, NULL);
	sigaction(SIGILL, &s_prev_sigill, NULL);
	sigaction(SIGTERM, &s_prev_sigterm, NULL);
#endif
	
#if IBM
	SetUnhandledExceptionFilter(s_previous_windows_exception_handler);
#endif
}


// Predicates that returns true if a thread is caused by us
// The main idea is to check the plugin ID if we are on the main thread,
// if not, we check if the current thread is known to be from us.
// Returns false if the crash was caused by code that didn't come from our plugin
bool is_us_executing()
{
	const std::thread::id thread_id = std::this_thread::get_id();

	if(thread_id == s_main_thread)
	{
		// Check if the plugin executing is our plugin.
		// XPLMGetMyID() will return the ID of the currently executing plugin. If this is us, then it will return the plugin ID that we have previously stashed away
		return (s_my_plugin_id == XPLMGetMyID());
	}

	if(s_thread_lock.test_and_set(std::memory_order_acquire))
	{
		// We couldn't acquire our lock. In this case it's better if we just say it's not us so we don't eat the exception
		return false;
	}

    // find the current thread in our list by its id
    const auto thr = std::find_if(s_known_threads.begin(),
                                  s_known_threads.end(),
                                  [thread_id](const std::pair<std::thread::id, std::string>& p)
                                  { return p.first == thread_id; });
	const bool is_our_thread = (thr != s_known_threads.end());
    if (is_our_thread) {            // copy the thread's name to a place that's a little more safe
        std::strncpy(crash_thread_name, thr->second.c_str(), sizeof(crash_thread_name));
        crash_thread_name[sizeof(crash_thread_name)-1]=0;       // ensure zero-termination
    }
    else
        crash_thread_name[0] = 0;

    s_thread_lock.clear(std::memory_order_release);

	return is_our_thread;
}

#if	APL || LIN

static void handle_posix_sig(int sig, siginfo_t *siginfo, void *context)
{
	if(is_us_executing())
	{
		static bool has_called_out = false;
		
		if(!has_called_out)
		{
			has_called_out = true;
			handle_crash(sig);
		}
		
		abort();
	}

	// Forward the signal to the other handlers
#define	FORWARD_SIGNAL(sigact) \
	do { \
		if((sigact)->sa_sigaction && ((sigact)->sa_flags & SA_SIGINFO)) \
			(sigact)->sa_sigaction(sig, siginfo, context); \
		else if((sigact)->sa_handler) \
			(sigact)->sa_handler(sig); \
	} while (0)
	
	switch(sig)
	{
		case SIGSEGV:
			FORWARD_SIGNAL(&s_prev_sigsegv);
			break;
		case SIGABRT:
			FORWARD_SIGNAL(&s_prev_sigabrt);
			break;
		case SIGFPE:
			FORWARD_SIGNAL(&s_prev_sigfpe);
			break;
		case SIGILL:
			FORWARD_SIGNAL(&s_prev_sigill);
			break;
		case SIGTERM:
			FORWARD_SIGNAL(&s_prev_sigterm);
			break;
	}
	
#undef FORWARD_SIGNAL
	
	abort();
}

#endif

#if IBM
LONG WINAPI handle_windows_exception(EXCEPTION_POINTERS *ei)
{
	if(is_us_executing())
	{
		handle_crash(ei);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if(s_previous_windows_exception_handler)
		return s_previous_windows_exception_handler(ei);

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Runtime
#if IBM
void write_mini_dump(PEXCEPTION_POINTERS exception_pointers, const char* szFileName);

static const char* ExceptionCode2Txt(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
    default: return "UNKNOWN EXCEPTION";
    }
}

#endif

#if    APL || LIN
void handle_crash(int sig)
#else
void handle_crash(EXCEPTION_POINTERS *ei)
#endif
{
    char sz[1024];

    // Determine a name for the dump file
    char szFileName[255];
    const time_t now = time(nullptr);
    struct tm tm;
    gmtime_s(&tm, &now);
    snprintf(szFileName, sizeof(szFileName), "Output/crash_reports/LiveTraffic_%4d-%02d-%02d_%02d-%02d-%02d.dmp",
             tm.tm_year + 1900, tm.tm_mon+1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    
#if APL || LIN
	// NOTE: Laminar's post doesn't consider this safe enough for production
	// as backtrace is NOT signal handler safe (backtrace_symbols_fd supposingly is),
    // but in my tests it seems 'good enough' for the purpose and did write
    // proper output at least for the most typical case of SIGSEGV.
    // Most crashes happen in Windows anyway, so APL/LIN are less of a concern.
    void *frames[64] = {0};
	int frame_count = backtrace(frames, 64);
	
	const int fd = open(szFileName, O_CREAT | O_RDWR | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(fd >= 0)
	{
        snprintf(sz, sizeof(sz), "File: %s\nPlatform: %s\nThread: %s\nSignal: %d\n\n",
                 szFileName,
#if APL
                 "Apple",
#else
                 "Linux",
#endif
                 crash_thread_name,
                 sig);
        write (fd, sz, strlen(sz));
        backtrace_symbols_fd(frames, frame_count, fd);
		close(fd);
	}
	
    // Last thing: we _try_ to leave a trace in X-Plane's Log.txt
    snprintf(sz, sizeof(sz), "LiveTraffic crashed%s%s by signal %d, please upload the following dump file to the LiveTraffic Support Forum:\n",
        *crash_thread_name ? " in thread " : "",
        *crash_thread_name ? crash_thread_name : "",
        sig);
#endif
#if IBM
	// Create a mini-dump file that can be later opened up in Visual Studio or WinDbg to do post mortem debugging
	write_mini_dump(ei, szFileName);

    // Last thing: we _try_ to leave a trace in X-Plane's Log.txt
    snprintf(sz, sizeof(sz), "LiveTraffic crashed%s%s by %s at address %p, please upload the following dump file to the LiveTraffic Support Forum:\n",
        *crash_thread_name ? " in thread " : "",
        *crash_thread_name ? crash_thread_name : "",
        ExceptionCode2Txt(ei->ExceptionRecord->ExceptionCode),
        ei->ExceptionRecord->ExceptionAddress);
#endif

    XPLMDebugString(sz);
    XPLMDebugString(szFileName);
    XPLMDebugString("\n");
}

#if IBM
#include <DbgHelp.h>

typedef BOOL(WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

void write_mini_dump(PEXCEPTION_POINTERS exception_pointers, const char* szFileName)
{
	HMODULE module = ::GetModuleHandleA("dbghelp.dll");
	if(!module)
		module = ::LoadLibraryA("dbghelp.dll");

	if(module)
	{
		const MINIDUMPWRITEDUMP pDump = MINIDUMPWRITEDUMP(::GetProcAddress(module, "MiniDumpWriteDump"));

		if(pDump)
		{
			// Create dump file
			const HANDLE handle = ::CreateFileA(szFileName, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

			if(handle != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION exception_information = {};
				exception_information.ThreadId = ::GetCurrentThreadId();
				exception_information.ExceptionPointers = exception_pointers;
				exception_information.ClientPointers = false;

				pDump(GetCurrentProcess(), GetCurrentProcessId(), handle, MiniDumpNormal, &exception_information, nullptr, nullptr);
				::CloseHandle(handle);
			}
		}
	}
}
#endif

//
// MARK: Thread Handling
//

// Defines thread's name and sets the thread's locale
ThreadSettings::ThreadSettings ([[maybe_unused]] const char* sThreadName,
                                int localeMask,
                                const char* sLocaleName)
{
    // --- Register thread with crash handler ---
    CrashHandlerRegisterThread(sThreadName);
    
    // --- Set thread's name ---
#if IBM
    // This might not work on older Windows version, which is why we don't publish it in release builds
#ifdef DEBUG
    wchar_t swThreadName[100];
    std::mbstowcs(swThreadName, sThreadName, sizeof(swThreadName));
    SetThreadDescription(GetCurrentThread(), swThreadName);
#endif
    
#elif APL
    pthread_setname_np(sThreadName);
#elif LIN
    pthread_setname_np(pthread_self(),sThreadName);
#endif
    
    // --- Set thread's locale ---
    if (sLocaleName)
    {
#if IBM
        _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
        setlocale(localeMask, sLocaleName);
#else
        threadLocale = newlocale(localeMask, sLocaleName, NULL);
        prevLocale = uselocale(threadLocale);
#endif
    }
}

// Restores and cleans up locale
ThreadSettings::~ThreadSettings()
{
#if IBM
    _configthreadlocale(_DISABLE_PER_THREAD_LOCALE);
#else
    if (prevLocale)
        uselocale(prevLocale);
    if (threadLocale) {
        freelocale(threadLocale);
        threadLocale = locale_t(0);
    }
#endif
    CrashHandlerUnregisterThread();
}
