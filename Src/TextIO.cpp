/// @file       TextIO.cpp
/// @brief      Output to Log.txt and to the message area
/// @details    Format Log.txt output strings\n
///             Paint the message area window\n
///             Implements the LTError exception handling class\n
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

#include "LiveTraffic.h"

//
// MARK: Helper for finding top right corner
//

static int rm_idx, rm_l, rm_t, rm_r, rm_b;     // window's idx & coordinates

/// callback function that receives monitor coordinates
void CBRightTopMostMonitorGlobal(int    inMonitorIndex,
                                 int    inLeftBx,
                                 int    inTopBx,
                                 int    inRightBx,
                                 int    inBottomBx,
                                 void * )          // inRefcon
{
    // right-top-most?
    if ((inRightBx > rm_r) ||
        (inRightBx == rm_r && inTopBx > rm_t))
    {
        rm_idx = inMonitorIndex;
        rm_l   = inLeftBx;
        rm_t   = inTopBx;
        rm_r   = inRightBx;
        rm_b   = inBottomBx;
    }
}

/// determines screen size
void GetTopRightScreenSize (int& outLeft,
                            int& outTop,
                            int& outRight,
                            int& outBottom)
{
    // find window coordinates
    // this will only find full screen monitors!
    rm_l = rm_t = rm_r = rm_b = 0;
    rm_idx = INT_MAX;
    
    // fetch global bounds of X-Plane-used windows
    XPLMGetAllMonitorBoundsGlobal (CBRightTopMostMonitorGlobal, NULL);
    
    // did we find something? then return it
    if ( rm_l || rm_t || rm_r || rm_b ) {
        outLeft     = rm_l;
        outTop      = rm_t;
        outRight    = rm_r;
        outBottom   = rm_b;
        return;
    }
    
    // if we didn't find anything we are running in windowed mode
    outLeft = outBottom = 0;
    XPLMGetScreenSize(&outRight,&outTop);
}


//MARK: custom X-Plane message Window - Globals

// An opaque handle to the window we will create
XPLMWindowID    g_window = 0;
bool            g_visible = false;      ///< window visible? (locally to avoid API calls)
// when to remove the window
float           fTimeRemove = NAN;

// time until window is to be shown, will be destroyed after that
struct dispTextTy {
    float fTimeDisp;                        // until when to display this line?
    logLevelTy lvlDisp;                     // level of msg (defines text color)
    std::string text;                       // text of line
};
std::list<dispTextTy> listTexts;     // lines of text to be displayed

float COL_LVL[logMSG+1][4] = {          // text colors [RGB] depending on log level
    {0.7019607843f, 0.7137254902f, 0.7176470588f, 1.00f},       // DEBUG (very light gray)
    {1.00f, 1.00f, 1.00f, 1.00f},       // INFO (white)
    {1.00f, 1.00f, 0.00f, 1.00f},       // WARN (yellow)
    {1.00f, .576f, 0.00f, 1.00f},       // ERROR (orange)
    {1.00f, 0.54f, 0.83f, 1.00f},       // FATAL (purple, FF8AD4)
    {1.00f, 1.00f, 1.00f, 1.00f}        // MSG (white)
};

/// Values for "Seeing aircraft...showing..."
int gNumSee = 0, gNumShow = 0, gBufTime = -1;

inline bool needSeeingShowMsg () { return gBufTime >= 0; }

//MARK: custom X-Plane message Window - Private Callbacks
// Callbacks we will register when we create our window
void    draw_msg(XPLMWindowID in_window_id, void * /*in_refcon*/)
{
    // Mandatory: We *must* set the OpenGL state before drawing
    // (we can't make any assumptions about it)
    XPLMSetGraphicsState(
                         0 /* no fog */,
                         0 /* 0 texture units */,
                         0 /* no lighting */,
                         0 /* no alpha testing */,
                         1 /* do alpha blend */,
                         1 /* do depth testing */,
                         0 /* no depth writing */
                         );
    
    int l, t, r, b;
    XPLMGetWindowGeometry(in_window_id, &l, &t, &r, &b);
    
    // Fill as translucent dark box, in case of error messages (in not so well readable red)
    // make the box 3 times so it appears darker
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    if (std::any_of(listTexts.cbegin(), listTexts.cend(),
                    [](const dispTextTy& dt){return dt.lvlDisp == logERR || dt.lvlDisp == logFATAL;}))
    {
        XPLMDrawTranslucentDarkBox(l, t, r, b);
        XPLMDrawTranslucentDarkBox(l, t, r, b);
    }
    
    
    b = WIN_WIDTH;                          // word wrap width = window width
    t -= WIN_ROW_HEIGHT;                    // move down to text's baseline

    // "Seeing aircraft...showing..." message
    if (needSeeingShowMsg())
    {
        char s[100];
        snprintf(s, sizeof(s), MSG_BUF_FILL_COUNTDOWN, gNumSee, gNumShow, gBufTime);
        XPLMDrawString(COL_LVL[logINFO], l, t, s, &b, xplmFont_Proportional);
        fTimeRemove = NAN;
        t -= 2*WIN_ROW_HEIGHT;
    }
    
    // for each line of text to be displayed
    float currTime = dataRefs.GetMiscNetwTime();
    for (auto iter = listTexts.cbegin();
         iter != listTexts.cend();
         t -= 2*WIN_ROW_HEIGHT)             // can't deduce number of rwos (after word wrap)...just assume 2 rows are enough
    {
        // still a valid entry?
        if (iter->fTimeDisp > 0 && currTime <= iter->fTimeDisp)
        {
            // draw text, take color based on msg level
            XPLMDrawString(COL_LVL[iter->lvlDisp], l, t,
                           const_cast<char*>(iter->text.c_str()),
                           &b, xplmFont_Proportional);
            // cancel any idea of removing the msg window
            fTimeRemove = NAN;
            // next element
            iter++;
        }
        else {
            // now outdated. Move on to next line, but remove this one
            auto iterRemove = iter++;
            listTexts.erase(iterRemove);
        }
    }
    
    // No texts left? Remove window in 1s
    if ((g_window == in_window_id) &&
        listTexts.empty() && !needSeeingShowMsg())
    {
        if (std::isnan(fTimeRemove))
            // set time when to remove
            fTimeRemove = currTime + WIN_TIME_REMAIN;
        else if (currTime >= fTimeRemove) {
            // time's up: remove
            XPLMSetWindowIsVisible ( g_window, g_visible=false );
            fTimeRemove = NAN;
        }
    }
}

int dummy_mouse_handler(XPLMWindowID /*in_window_id*/, int /*x*/, int /*y*/, int /*is_down*/, void * /*in_refcon*/)
{ return 0; }

XPLMCursorStatus dummy_cursor_status_handler(XPLMWindowID /*in_window_id*/, int /*x*/, int /*y*/, void * /*in_refcon*/)
{ return xplm_CursorDefault; }

int dummy_wheel_handler(XPLMWindowID /*in_window_id*/, int /*x*/, int /*y*/, int /*wheel*/, int /*clicks*/, void * /*in_refcon*/)
{ return 0; }

void dummy_key_handler(XPLMWindowID /*in_window_id*/, char /*key*/, XPLMKeyFlags /*flags*/, char /*virtual_key*/, void * /*in_refcon*/, int /*losing_focus*/)
{ }

/// Create/show the message window
XPLMWindowID DoShowMsgWindow()
{
    // must only do so if executed from XP's main thread
    if (!dataRefs.IsXPThread())
        return nullptr;

    // Create the message window
    XPLMCreateWindow_t params;
    params.structSize = sizeof(params);
    params.visible = 1;
    params.drawWindowFunc = draw_msg;
    // Note on "dummy" handlers:
    // Even if we don't want to handle these events, we have to register a "do-nothing" callback for them
    params.handleMouseClickFunc = dummy_mouse_handler;
    params.handleRightClickFunc = dummy_mouse_handler;
    params.handleMouseWheelFunc = dummy_wheel_handler;
    params.handleKeyFunc = dummy_key_handler;
    params.handleCursorFunc = dummy_cursor_status_handler;
    params.refcon = NULL;
    params.layer = xplm_WindowLayerFloatingWindows;
    // No decoration...this is just message output and shall stay where it is
    params.decorateAsFloatingWindow = xplm_WindowDecorationNone;

    // Set the window's initial bounds
    // Note that we're not guaranteed that the main monitor's lower left is at (0, 0)...
    // We'll need to query for the global desktop bounds!
    GetTopRightScreenSize(params.left, params.top, params.right, params.bottom);

    // define a window in the top right corner,
    // WIN_FROM_TOP point down from the top, WIN_WIDTH points wide,
    // enough height for all lines of text
    params.top -= WIN_FROM_TOP;
    params.right -= WIN_FROM_RIGHT;
    params.left = params.right - WIN_WIDTH;
    params.bottom = params.top - (WIN_ROW_HEIGHT * (2 * int(listTexts.size()) + 1 +
        (needSeeingShowMsg() ? 2 : 0)));

    // if the window still exists just resize it
    if (g_window) {
        if (!XPLMGetWindowIsVisible(g_window))
            XPLMSetWindowIsVisible(g_window, true);
        XPLMSetWindowGeometry(g_window, params.left, params.top, params.right, params.bottom);
    }
    else {
        // otherwise create a new one
        g_window = XPLMCreateWindowEx(&params);
        LOG_ASSERT(g_window);
    }
    g_visible = true;

    return g_window;
}

//MARK: custom X-Plane message Window - Create / Destroy
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, logLevelTy lvl, const char* szMsg, ...)
{
    // consider configured level for msg area
    if ( lvl < dataRefs.GetMsgAreaLevel())
        return g_window;
    
    // put together the formatted message if given
    if (szMsg) {
        va_list args;
        char aszMsgTxt[500];
        va_start (args, szMsg);
        vsnprintf(aszMsgTxt,
                  sizeof(aszMsgTxt),
                  szMsg,
                  args);
        va_end (args);
    
        // define the text to display:
        dispTextTy dispTxt = {
            // set the timer if a limit is given
            fTimeToDisplay >= 0.0f ? dataRefs.GetMiscNetwTime() + fTimeToDisplay : 0,
            // log level to define the color
            lvl,
            // finally the text
            aszMsgTxt
        };
        
        // add to list of display texts
        listTexts.emplace_back(std::move(dispTxt));
    }

    // Show the window
    return DoShowMsgWindow();
}


// Show the special text "Seeing aircraft...showing..."
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, int numSee, int numShow, int bufTime)
{
    gNumSee     = numSee;
    gNumShow    = numShow;
    gBufTime    = bufTime;
    return needSeeingShowMsg() ? CreateMsgWindow(fTimeToDisplay, logINFO, nullptr) : nullptr;
}


// Check if message wait to be shown, then show
bool CheckThenShowMsgWindow()
{
    if ((!listTexts.empty() || needSeeingShowMsg()) &&      // something to show
        (!g_window || !g_visible))                          // no window/visible
    {
        DoShowMsgWindow();
        return true;
    }
    return false;
}


void DestroyWindow()
{
    if ( g_window )
    {
        XPLMDestroyWindow(g_window);
        g_window = NULL;
        g_visible = false;
        listTexts.clear();
   }
}

//
// MARK: Log message storage
//

/// The global list of log messages
LogMsgListTy gLog;

/// The global counter
static unsigned long gLogCnt = 0;

/// Controls access to the log list
std::recursive_mutex gLogMutex;

static char gBuf[4048];

const char* LOG_LEVEL[] = {
    "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL", "MSG  "
};

// forward declaration: returns ptr to static buffer filled with log string
const char* GetLogString (const LogMsgTy& l);


// Constructor fills all fields and removes personal info
LogMsgTy::LogMsgTy (const char* _fn, int _ln, const char* _func,
                    logLevelTy _lvl, const char* _msg) :
counter(++gLogCnt),
wallTime(std::chrono::system_clock::now()),
netwTime(dataRefs.GetMiscNetwTime()),
fileName(_fn), ln(_ln), func(_func), lvl(_lvl), msg(_msg), bFlushed(false)
{
    // Remove personal information
    str_replPers(msg);

}

// does the entry match the given string (expected in upper case)?
bool LogMsgTy::matches (const char* _s) const
{
    // catch the trivial case of no search term
    if (!_s || !*_s)
        return true;
    
    // Re-Create the complete log line and turn it upper case
    std::string logText = GetLogString(*this);
    str_toupper(logText);
    return logText.find(_s) != std::string::npos;
}

// returns ptr to static buffer filled with log string
const char* GetLogString (const LogMsgTy& l)
{
    // Access to static buffer and list guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);
    
    // Network time string
    const std::string netwT = NetwTimeString(l.netwTime);

    // prepare timestamp
    if (l.lvl < logMSG)                             // normal messages without, all other with location info
    {
        snprintf(gBuf, sizeof(gBuf)-1, "%s " LIVE_TRAFFIC " %s %s:%d/%s: %s",
                 netwT.c_str(),                     // network time (string)
                 LOG_LEVEL[l.lvl],                  // logging level
                 l.fileName.c_str(), l.ln,          // source file and line number
                 l.func.c_str(),                    // function name
                 l.msg.c_str());                    // actual message
    }
    else
        snprintf(gBuf, sizeof(gBuf)-1, "%s " LIVE_TRAFFIC ": %s",
                 netwT.c_str(),                     // network time (string)
                 l.msg.c_str());                    // actual message
    
    // ensure there's a trailing CR
    size_t sl = strlen(gBuf);
    if (gBuf[sl-1] != '\n')
    {
        gBuf[sl]   = '\n';
        gBuf[sl+1] = 0;
    }

    // return the static buffer
    return gBuf;
}

/// Actually adds an entry to the log list, flushes immediately if in main thread
LogMsgListTy::iterator AddLogMsg (const char* szPath, int ln, const char* szFunc,
                                  logLevelTy lvl, const char* szMsg, va_list args)
{
    // We get the lock already to avoid having to lock twice if in main thread
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);
    
    // Cut off path from file name
    const char* szFile = strrchr(szPath, PATH_DELIM);  // extract file from path
    if (!szFile) szFile = szPath; else szFile++;

    // Prepare the formatted string if variable arguments are given
    if (args)
        vsnprintf(gBuf, sizeof(gBuf), szMsg, args);

    // Add the list entry
    gLog.emplace_front(szFile, ln, szFunc, lvl,
                       args ? gBuf : szMsg);

    // Flush immediately if called from main thread
    if (dataRefs.IsXPThread())
        FlushMsg();
    
    // was added to the front
    return gLog.begin();
}

// Add a message to the list, flush immediately if in main thread
void LogMsg ( const char* szPath, int ln, const char* szFunc, logLevelTy lvl, const char* szMsg, ... )
{
    // We get the lock already to avoid having to lock twice if in main thread
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);

    // Prepare the formatted message
    va_list args;
    va_start (args, szMsg);
    AddLogMsg(szPath, ln, szFunc, lvl, szMsg, args);
    va_end (args);

}

// Might be used in macros of other packages like ImGui
void LogFatalMsg ( const char* szPath, int ln, const char* szFunc, const char* szMsg, ... )
{
    // Access to static buffer and list guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);

    // Add to the message buffer
    va_list args;
    va_start (args, szMsg);
    AddLogMsg(szPath, ln, szFunc, logFATAL, szMsg, args);
    va_end (args);
}

// Force writing of all not yet flushed messages
/// @details As new messages are added to the front, we start searching from
///          the front and go forward until we find either the end or
///          an already flushed message. Then we go back and actually write
///          message in sequence
void FlushMsg ()
{
    // Access to static buffer and list guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);

    // Quick exit if empty or nothing to write
    if (gLog.empty() || gLog.front().bFlushed)
        return;
    
    // Move forward to first flushed msg or end
    LogMsgListTy::iterator logIter = gLog.begin();
    while (logIter != gLog.end() && !logIter->bFlushed)
        logIter++;
    
    // Now move back and on the way write out the messages into `Log.txt`
    do {
        logIter--;
        // write to log (flushed immediately -> expensive!)
        XPLMDebugString (GetLogString(*logIter));
        logIter->bFlushed = true;
    } while (logIter != gLog.begin());
}

// Remove old message (>1h XP network time)
void PurgeMsgList ()
{
    // How many messages to keep?
    const size_t nKeep = (size_t)DataRefs::GetCfgInt(DR_CFG_LOG_LIST_LEN);
    
    // Access to static buffer and list guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(gLogMutex);
    if (gLog.size() > nKeep)
        gLog.resize(nKeep);
}

/// Return text for log level
const char* LogLvlText (logLevelTy _lvl)
{
    LOG_ASSERT(logDEBUG <= _lvl && _lvl <= logMSG);
    return LOG_LEVEL[_lvl];
}

/// Return color for log level (as float[3])
float* LogLvlColor (logLevelTy _lvl)
{
    LOG_ASSERT(logDEBUG <= _lvl && _lvl <= logMSG);
    return COL_LVL[_lvl];
}


//
// MARK: LiveTraffic Exception classes
//

// standard constructor
LTError::LTError (const char* _szFile, int _ln, const char* _szFunc,
                  logLevelTy _lvl,
                  const char* _szMsg, ...) :
std::logic_error(_szMsg)
{
    va_list args;
    va_start (args, _szMsg);
    msgIter = AddLogMsg(_szFile, _ln, _szFunc, _lvl, _szMsg, args);
    va_end (args);
}

// protected constructor, only called by LTErrorFD
LTError::LTError () :
std::logic_error("")
{}

const char* LTError::what() const noexcept
{
    return msgIter->msg.c_str();
}

// includsive reference to LTFlightData
LTErrorFD::LTErrorFD (LTFlightData& _fd,
                      const char* _szFile, int _ln, const char* _szFunc,
                      logLevelTy _lvl,
                      const char* _szMsg, ...) :
LTError(),
fd(_fd),
posStr(_fd.Positions2String())
{
    // Add the formatted message to the log list
    va_list args;
    va_start (args, _szMsg);
    msgIter = AddLogMsg(_szFile, _ln, _szFunc, _lvl, _szMsg, args);
    va_end (args);
    
    // Add the position information also to the log list
    AddLogMsg(_szFile, _ln, _szFunc, _lvl, posStr.c_str(), nullptr);
}

