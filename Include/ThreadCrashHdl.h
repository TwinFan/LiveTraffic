/// @file       ThreadCrashHdl.h
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

#ifndef ThreadCrashHdl_h
#define ThreadCrashHdl_h

//
// MARK: Crash Handler
//

/// @brief Registers the global crash handler
/// @details Should be called from XPluginStart()
void CrashHandlerRegister();
/// @brief Unregisters the global crash handler
/// @details You need to call this in XPluginStop() so we can clean up after ourselves
void CrashHandlerUnregister();

/// @brief Registers the calling thread with the crash handler
/// @details We use this to figure out if a crashed thread belongs to us
///          when we later try to figure out if we caused a crash
void CrashHandlerRegisterThread (const char* sThrName);
/// @brief Unregisters the calling thread from the crash handler
/// @details MUST be called at the end of thread that was registered
///          via CrashHandlerRegister()
void CrashHandlerUnregisterThread();



//
// MARK: Thread Settings
//

/// Begin a thread and set a thread-local locale
/// @details In the communication with servers we must use internal standards,
///          ie. C locale, so that for example the decimal point is `.`
///          Hence we set a thread-local locale in all threads as they deal with communication.
///          See https://stackoverflow.com/a/17173977
class ThreadSettings {
protected:
#if IBM
#define LC_ALL_MASK LC_ALL
#else
    locale_t threadLocale = locale_t(0);
    locale_t prevLocale = locale_t(0);
#endif
public:
    /// @brief Defines thread's name and sets the thread's locale
    /// @param sThreadName Thread's name, max 16 chars
    /// @param localeMask One of the LC_*_MASK constants. If `0` then locale is not changed.
    /// @param sLocaleName New locale to set
    ThreadSettings (const char* sThreadName,
                    int localeMask = 0,
                    const char* sLocaleName = "C");
    /// Restores and cleans up locale
    ~ThreadSettings();
};

#endif /* ThreadCrashHdl_h */
