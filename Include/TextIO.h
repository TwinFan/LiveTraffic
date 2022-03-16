/// @file       TextIO.h
/// @brief      Error handling, macros for output to Log.txt and to the message area
/// @details    Defines central logging macros `LOG_MSG`, `SHOW_MSG` et al\n
///             Defines exception handling class LTError\n
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2020 Birger Hoppe
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

#ifndef TextIO_h
#define TextIO_h

#include <stdexcept>

/// @brief To apply printf-style warnings to our functions.
/// @see Taken from imgui.h's definition of IM_FMTARGS
#if defined(__clang__) || defined(__GNUC__)
#define LT_FMTARGS(FMT)  __attribute__((format(printf, FMT, FMT+1)))
#else
#define LT_FMTARGS(FMT)
#endif


class LTFlightData;     // see LTFlightData.h

// MARK: Log Level:
// 4 - Fatal Errors only
// 3 - Errors
// 2 - Warnings
// 1 - Infos
// 0 - Debug Output
enum logLevelTy {
    logDEBUG = 0,
    logINFO,
    logWARN,
    logERR,
    logFATAL,
    logMSG              // will always be output
};

// MARK: custom X-Plane message Window

// Creates a window and displays the given szMsg for fTimeToDisplay seconds
// fTimeToDisplay == 0 -> no limit
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, logLevelTy lvl, const char* szMsg, ...) LT_FMTARGS(3);

/// Show the special text "Seeing aircraft...showing..."
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, int numSee, int numShow, int bufTime);

/// Check if message wait to be shown, then show
bool CheckThenShowMsgWindow();

// Destroys the windows (if still active)
void DestroyWindow();

//
// MARK: Log message storage
//

/// A single log message
struct LogMsgTy {
    unsigned long                           counter = 0;    ///< monotonic counter to find new/removed messages
    std::chrono::system_clock::time_point   wallTime;       ///< system time of message
    float                                   netwTime = NAN; ///< X-Plane's network time of message
    std::string                             fileName;       ///< source file name where message was produced
    int                                     ln = 0;         ///< line number if `fileName`
    std::string                             func;           ///< function in which message was produced
    logLevelTy                              lvl = logMSG;   ///< message severity
    std::string                             msg;            ///< message text
    bool                                    bFlushed = false;   ///< written to `Log.txt` by a call to XPLMDebugString?
    
    /// Constructor fills all fields
    LogMsgTy (const char* _fn, int _ln, const char* _func,
              logLevelTy _lvl, const char* _msg);
    /// Standard constructor does nothing much, only needed for std::list::resize
    LogMsgTy () {}
    
    /// does the entry match the given string (expected in upper case)?
    bool matches (const char* _s) const;
};

/// A list of log messages
typedef std::list<LogMsgTy> LogMsgListTy;

/// @brief A list of log message iterators
/// @details iterators into a `std::list` are guaranteed to stay valid
///          throughout all operations, except deletions of the element.
typedef std::list<LogMsgListTy::const_iterator> LogMsgIterListTy;

/// The global list of log messages
extern LogMsgListTy gLog;

/// Add a message to the list, flush immediately if in main thread
void LogMsg ( const char* szFile, int ln, const char* szFunc, logLevelTy lvl, const char* szMsg, ... ) LT_FMTARGS(5);

/// Force writing of all not yet flushed messages
void FlushMsg ();

/// @brief Remove old message
void PurgeMsgList ();

/// Return text for log level
const char* LogLvlText (logLevelTy _lvl);

/// Return color for log level (as float[3])
float* LogLvlColor (logLevelTy _lvl);

// Log a message if this is a beta version, or
//               if lvl is greater or equal currently defined log level
// Note: First parameter after lvl must be the message text,
//       which can be a format string with its parameters following like in sprintf
#define LOG_MSG(lvl,...)  {                                         \
    if (LIVETRAFFIC_VERSION_BETA || ((lvl) >= dataRefs.GetLogLevel()))          \
    {LogMsg(__FILE__, __LINE__, __func__, lvl, __VA_ARGS__);}       \
}

// Display AND log a message as above
#define SHOW_MSG(lvl,...) {                                         \
    LOG_MSG(lvl,__VA_ARGS__);                                       \
    CreateMsgWindow((lvl)==logERR || (lvl)==logFATAL ? WIN_TIME_DISP_ERR : WIN_TIME_DISPLAY,lvl,__VA_ARGS__);              \
}

// Throw in an assert-style (logging takes place in LTErrorFD constructor)
#define LOG_ASSERT_FD(fdref,cond)                                   \
    if (!(cond)) {                                                  \
        THROW_ERROR_FD(fdref,logFATAL,ERR_ASSERT,#cond);            \
    }

// Throw in an assert-style (logging takes place in LTError constructor)
#define LOG_ASSERT(cond)                                            \
    if (!(cond)) {                                                  \
        THROW_ERROR(logFATAL,ERR_ASSERT,#cond);                     \
    }

// MARK: LiveTraffic Exception class
class LTError : public std::logic_error {
protected:
    LogMsgListTy::iterator msgIter;
public:
    LTError (const char* szFile, int ln, const char* szFunc, logLevelTy lvl,
             const char* szMsg, ...) LT_FMTARGS(6);
protected:
    LTError ();     ///< only used by LTErrorFD
public:
    /// returns msgIter->msg.c_str()
    virtual const char* what() const noexcept;
    
public:
    // copy/move constructor/assignment as per default
    LTError (const LTError& o) = default;
    LTError (LTError&& o) = default;
    LTError& operator = (const LTError& o) = default;
    LTError& operator = (LTError&& o) = default;
};

class LTErrorFD : public LTError {
public:
    LTFlightData&   fd;
    std::string     posStr;
public:
    LTErrorFD (LTFlightData& _fd,
               const char* szFile, int ln, const char* szFunc, logLevelTy lvl,
               const char* szMsg, ...) LT_FMTARGS(7);

public:
    // copy/move constructor/assignment as per default
    LTErrorFD (const LTErrorFD& o) = default;
    LTErrorFD (LTErrorFD&& o) = default;
};

#define THROW_ERROR(lvl,...)                                        \
throw LTError(__FILE__, __LINE__, __func__, lvl, __VA_ARGS__);

#define THROW_ERROR_FD(fdref,lvl,...)                               \
throw LTErrorFD(fdref,__FILE__, __LINE__, __func__, lvl, __VA_ARGS__);



#endif /* TextIO_h */
