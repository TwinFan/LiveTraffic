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


//
// MARK: Message Window
//

/// Defines an line of text to be shown in the Message Wnd
struct dispTextTy {
    float fTimeDisp;                        ///< until when to display this line? (negative = display duration)
    logLevelTy lvlDisp;                     ///< level of msg (defines text color)
    std::string text;                       ///< text of line
    dispTextTy(float t, logLevelTy l, const std::string& s) :
    fTimeDisp(t), lvlDisp(l), text(s) {}
};
std::list<dispTextTy> listTexts;            ///< lines of text to be displayed
std::recursive_mutex gListMutex;             ///< Controls access to listText

/// Predefined text colors [RGBA] depending on log level
const ImColor COL_LVL[logMSG+1] = {
    {0.00f, 0.00f, 0.00f, 1.00f},           // 0
    {1.00f, 1.00f, 1.00f, 1.00f},           // INFO (white)
    {0xff/255.0f, 0xd1/255.0f, 0x4b/255.0f, 1.00f}, // WARN (FSC yellow)
    {0.75f, 0.00f, 0.00f, 1.00f},           // ERROR (red)
    {0.65f, 0.24f, 0.18f, 1.00f},           // FATAL (purple, A73E54)
    {1.00f, 1.00f, 1.00f, 1.00f},           // MSG (white)
};

/// Background color for error/fatal display
#ifdef SIM_CONNECT
const ImColor WND_MSG_COL_BG = { 0x26 / 255.0f, 0x2a / 255.0f, 0x2b / 255.0f, 1.00f }; // #262a2b, a little transparent
#else
const ImColor WND_MSG_COL_BG = {0.50f, 0.50f, 0.50f, 0.60f}; // dark white, a little transparent
#endif

/// Message window resize limits
const WndRect WND_MSG_LIMITS (100, 20, 600, 400);
/// Message window padding around text
constexpr int WND_MSG_BORDER_PADDING = 5;

/// Represents the message window
class WndMsg : public LTImgWindow
{
protected:
    /// Window border color depends on max msg level in listTexts
    ImColor colBorder = WND_MSG_COL_BG;
    
    /// Current window geometry, reference to dataRefs.wndRectMsg for ease of access
    WndRect& rWnd;

    /// The one and only window object
    static WndMsg* gpWnd;
    
public:
    /// Creates a new message window
    WndMsg (WndMode _wndMode = WND_MODE_FLOAT);
    /// Destruction also happens when closed by the user, reset global pointer
    ~WndMsg () override;
    /// Actually create (or show) the window if there are texts to show
    static bool DoShow();
    /// Actually remove the window
    static void DoRemove();
    
protected:
    /// Changes the window's height and makes sure the position coordinates stay valid
    void PositionWnd (int _height);
    /// Some setup before UI building starts
    ImGuiWindowFlags_ beforeBegin() override;
    /// Main function to render the window's interface
    void buildInterface() override;
};

/// There's at most one global message window
WndMsg* WndMsg::gpWnd = nullptr;

// Creates a new message window
WndMsg::WndMsg (WndMode _wndMode) :
LTImgWindow(_wndMode, WND_STYLE_HUD, dataRefs.MsgRect),     // position is recalculated soon...just pass anything
rWnd(dataRefs.MsgRect)
{
    // Set up window basics
    SetWindowTitle(LIVE_TRAFFIC " Messages");
    SetWindowResizingLimits(WND_MSG_LIMITS.left(), WND_MSG_LIMITS.top(),
                            WND_MSG_LIMITS.right(), WND_MSG_LIMITS.bottom());
    
    // Set the initial window position
    PositionWnd(WND_MSG_LIMITS.top() * int(listTexts.size()));
}

// Destruction also happens when closed by the user, reset global pointer
WndMsg::~WndMsg ()
{
    gpWnd = nullptr;
}

// Actually create (or show) the window
bool WndMsg::DoShow()
{
    // must only do so if executed from XP's main thread
    if (!dataRefs.IsXPThread())
        return false;

    // No message waiting - no deal
    if (listTexts.empty())
        return false;
    
    // Make sure the window object exists...
    if (!gpWnd)
        gpWnd = new WndMsg();
    LOG_ASSERT(gpWnd);
    
    // ...and its window is visible
    gpWnd->SetVisible(true);
    return true;
}

// Actually remove the window
void WndMsg::DoRemove()
{
    if (gpWnd)
        delete gpWnd;
    gpWnd = nullptr;
}

/// Places the window center according to a point and width/height
void WndMsg::PositionWnd (int _height)
{
    // make sure window size limits are adhered to
    _height = std::clamp<int>(_height, WND_MSG_LIMITS.top(), WND_MSG_LIMITS.bottom());
    rWnd.bottom() = rWnd.top() - _height;
    
    // Find initial window position
    if (!rWnd.left() && !rWnd.top() && rWnd.bottom() < 0) {                       // find an initial position
        // Screen size of top right screen
        WndRect screen;
        GetTopRightScreenSize(screen.left(), screen.top(), screen.right(), screen.bottom());
        // Top Right corner, a bit away from the actual corner
        rWnd.left() = screen.right() - WIN_FROM_RIGHT - rWnd.right();   // here, rWnd.right is still the expected wnd width
        rWnd.right() = screen.right() - WIN_FROM_RIGHT;
        rWnd.top() = screen.top() - WIN_FROM_TOP;
        rWnd.bottom() = rWnd.top() - _height;
    }
    else {
        // make sure the window does not cross screen boundaries
        rWnd.keepOnScreen();
    }
    
    // If popped out, we don't touch, user's choice, but inside X-Plane we adapt
    if (!IsPoppedOut())
        SetCurrentWindowGeometry(rWnd);
}

// Some setup before UI building starts
ImGuiWindowFlags_ WndMsg::beforeBegin()
{
    // Give parent class a chance for setup, too, e.g. colors
    ImGuiWindowFlags_ ret = LTImgWindow::beforeBegin();
    
    // Set background and border color (highlights if there is any warning/error on display)
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(WND_MSG_COL_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(colBorder));
    style.WindowBorderSize = 1.0f;

    // Save latest screen size to configuration (if not popped out)
    if (!IsPoppedOut()) {
        rWnd = GetCurrentWindowGeometry();

        // Set drag area so that 3 pixels are available for resizing
        SetWindowDragArea(8, 5, rWnd.width()-16, rWnd.height()-10);

        // Set background opacity if not popped out
        style.Colors[ImGuiCol_WindowBg].w =  float(dataRefs.UIopacity)/100.0f;
    }

    // Don't save settings for this window to ImGui.ini
    return ImGuiWindowFlags_(ret | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
}

// Main function to render the window's interface
void WndMsg::buildInterface()
{
    // Set the Z-Order so it is drawn higher than normal
    ImGui::GetCurrentWindow()->BeginOrderWithinContext = 5;

    // --- Window buttons in top right corner ---
    const ImVec2 crsrPos = ImGui::GetCursorPos();
    buildWndButtons();                  // right-aligned
    ImGui::SetCursorPos(crsrPos);       // back to beginning of line
    ImGui::PushTextWrapPos();
    
    // --- Loop the messages to be displayed ---
    logLevelTy maxLevel = logINFO;      // keep track what the highest warning level was
    const float now = dataRefs.GetMiscNetwTime();
    // Access to list of messages is guarded by a lock
    std::unique_lock<std::recursive_mutex> lock(gListMutex);
    auto iter = listTexts.begin();

    // The very first call to GetMiscNetwTime, right after loading initial scenery,
    // returns way too low a value, which leads to all texts being deleted in the 2nd frame already.
    // So we ignore that first call altogether
    static bool bCalledOnce = false;
    while (bCalledOnce && iter != listTexts.end()) {
        dispTextTy& msg = *iter;
        // Do we now start showing this msg? (msg.timeDisp negative?)
        if (std::signbit(msg.fTimeDisp))
            // until when to show this message? (msg.timeDisp is negative!)
            msg.fTimeDisp = now - msg.fTimeDisp;
        // Special key for resizing the window stays until actively closed
        const bool bDoReposition = msg.text == MSG_REPOSITION_WND;
        if (bDoReposition)
            BringWindowToFront();
        // remove and skip outdated texts (which are not repositioning info)
        else if (msg.fTimeDisp < now) {
            iter = listTexts.erase(iter);
            continue;
        }
        
        // Determine max message level
        if (msg.lvlDisp != logMSG && msg.lvlDisp > maxLevel)
            maxLevel = msg.lvlDisp;
        
        // Set text color and draw the text centered
        ImGui::PushStyleColor(ImGuiCol_Text, COL_LVL[msg.lvlDisp].Value);
        ImGui::TextUnformatted(msg.text.c_str());
        if (bDoReposition && listTexts.size() < 3) {
            ImGui::TextUnformatted(MSG_REPOSITION_LN2);
        }
        // Show the FMOD logo together with the FMOD attribution message
        else if (msg.text == MSG_FMOD_SOUND) {
            // FMOD Logo in white
            int logoId = 0;
            if (FMODLogo::GetTexture(logoId,false)) {
                ImGui::Image((void*)(intptr_t)logoId, ImVec2(FMODLogo::IMG_WIDTH/4, FMODLogo::IMG_HEIGHT/4));
            }
        }
        
        // Finish button for repositioning, remove when donw
        if (bDoReposition && (!LTSettingsUI::IsDisplayed() || ImGui::Button("Finished Repositioning")))
            iter = listTexts.erase(iter);
        else
            // next message
            ++iter;

        ImGui::PopStyleColor();
    }
    const bool bNowEmpty = listTexts.empty();
    lock.unlock();
    
    ImGui::PopTextWrapPos();
    
    // Nothing left to show? -> Hide ourself
    if (bNowEmpty) {
        if (!IsPoppedOut())
            SetVisible(false);
    } else {
        // In case of WARN, ERROR, FATAL make sure a colored border is around the window
        if (maxLevel >= logWARN)
            colBorder = COL_LVL[maxLevel];
        else
            colBorder = WND_MSG_COL_BG;
        
        // Test for need to change window geometry
        const int idealHeight = int(ImGui::GetCursorPosY()) + 2 * WND_MSG_BORDER_PADDING;
        if (rWnd.height() != idealHeight)
            PositionWnd(idealHeight);
    }

    // Restore border/bg color
    ImGui::PopStyleColor(2);
    // Have now been called once, so next time we do proper processing
    bCalledOnce = true;
}

//
//MARK: custom X-Plane message Window - Create / Destroy
//
void CreateMsgWindow(float fTimeToDisplay, logLevelTy lvl, const char* szMsg, ...)
{
    // consider configured level for msg area
    if ( lvl < dataRefs.GetMsgAreaLevel())
        return;
    
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
    
        // add to list of display texts
        std::unique_lock<std::recursive_mutex> lock(gListMutex);
        listTexts.emplace_back(-fTimeToDisplay, lvl, aszMsgTxt);
    }

    // Show the window
    WndMsg::DoShow();
}


// Show the special text "Seeing aircraft...showing..."
void CreateMsgWindow(float fTimeToDisplay, int numSee, int numShow, int bufTime)
{
    // We consider this message logINFO level, so let's see if we are supposed to show:
    if (logINFO < dataRefs.GetMsgAreaLevel())
        return;
    
    // This entry shall always be the first one in the list
    // So let's see if it is already the first one, then we'd remove it first
    std::unique_lock<std::recursive_mutex> lock(gListMutex);
    if (!listTexts.empty() && listTexts.front().text.substr(0,23) == MSG_BUF_FILL_BEGIN)
        listTexts.pop_front();
    
    // Now create the actual message if there is anything to show
    if (bufTime >= 0) {
        char aszMsgTxt[500];
        snprintf(aszMsgTxt, sizeof(aszMsgTxt), MSG_BUF_FILL_COUNTDOWN,
                 numSee, numShow, bufTime);
        // add to list of display texts _at front_
        listTexts.emplace_front(-fTimeToDisplay, logINFO, aszMsgTxt);
    }
    
    // Show the window
    WndMsg::DoShow();
}


// Check if message wait to be shown, then show
bool CheckThenShowMsgWindow()
{
    return WndMsg::DoShow();
}


void DestroyWindow()
{
    WndMsg::DoRemove();
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

/// Return color for log level
ImColor LogLvlColor (logLevelTy _lvl)
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

