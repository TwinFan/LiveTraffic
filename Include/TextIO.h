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

// MARK: LiveTraffic Exception class
class LTError : public std::logic_error {
protected:
    std::string fileName;
    int ln;
    std::string funcName;
    logLevelTy lvl;
    std::string msg;
public:
    LTError (const char* szFile, int ln, const char* szFunc, logLevelTy lvl,
             const char* szMsg, ...) LT_FMTARGS(6);
protected:
    LTError (const char* szFile, int ln, const char* szFunc, logLevelTy lvl);
public:
    // returns msg.c_str()
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

// MARK: custom X-Plane message Window

// Creates a window and displays the given szMsg for fTimeToDisplay seconds
// fTimeToDisplay == 0 -> no limit
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, logLevelTy lvl, const char* szMsg, ...) LT_FMTARGS(3);

/// Show the special text "Seeing aircraft...showing..."
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, int numSee, int numShow, int bufTime);

// Destroys the windows (if still active)
void DestroyWindow();

// MARK: Write to X-Plane log

// returns ptr to static buffer filled with log string
const char* GetLogString ( const char* szFile, int ln, const char* szFunc, logLevelTy lvl, const char* szMsg, va_list args );
             
// Log Text to log file
void LogMsg ( const char* szFile, int ln, const char* szFunc, logLevelTy lvl, const char* szMsg, ... ) LT_FMTARGS(5);

// Log a message if this is a beta version, or
//               if lvl is greater or equal currently defined log level
// Note: First parameter after lvl must be the message text,
//       which can be a format string with its parameters following like in sprintf
#define LOG_MSG(lvl,...)  {                                         \
    if (VERSION_BETA || ((lvl) >= dataRefs.GetLogLevel()))          \
    {LogMsg(__FILE__, __LINE__, __func__, lvl, __VA_ARGS__);}       \
}

// Display AND log a message as above
#define SHOW_MSG(lvl,...) {                                         \
    LOG_MSG(lvl,__VA_ARGS__);                                       \
    CreateMsgWindow(WIN_TIME_DISPLAY,lvl,__VA_ARGS__);              \
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

#endif /* TextIO_h */
