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

// MARK: LiveTraffic Exception classes

// standard constructor
LTError::LTError (const char* _szFile, int _ln, const char* _szFunc,
                  logLevelTy _lvl,
                  const char* _szMsg, ...) :
std::logic_error(GetLogString(_szFile, _ln, _szFunc, _lvl, _szMsg, NULL)),
fileName(_szFile), ln(_ln), funcName(_szFunc),
lvl(_lvl)
{
    va_list args;
    va_start (args, _szMsg);
    msg = GetLogString(_szFile, _ln, _szFunc, _lvl, _szMsg, args);
    va_end (args);
    
    // write to log (flushed immediately -> expensive!)
    if (_lvl >= dataRefs.GetLogLevel())
        XPLMDebugString ( msg.c_str() );
}

// protected constructor, only called by LTErrorFD
LTError::LTError (const char* _szFile, int _ln, const char* _szFunc,
                  logLevelTy _lvl) :
std::logic_error(GetLogString(_szFile, _ln, _szFunc, _lvl, "", NULL)),
fileName(_szFile), ln(_ln), funcName(_szFunc),
lvl(_lvl)
{}

const char* LTError::what() const noexcept
{
    return msg.c_str();
}

// includsive reference to LTFlightData
LTErrorFD::LTErrorFD (LTFlightData& _fd,
                      const char* _szFile, int _ln, const char* _szFunc,
                      logLevelTy _lvl,
                      const char* _szMsg, ...) :
LTError(_szFile,_ln,_szFunc,_lvl),
fd(_fd),
posStr(_fd.Positions2String())
{
    va_list args;
    va_start (args, _szMsg);
    msg = GetLogString(_szFile, _ln, _szFunc, _lvl, _szMsg, args);
    va_end (args);
    
    // write to log (flushed immediately -> expensive!)
    if (_lvl >= dataRefs.GetLogLevel()) {
        XPLMDebugString ( msg.c_str() );
        XPLMDebugString ( posStr.c_str() );
    }
}


//MARK: custom X-Plane message Window - Globals

// An opaque handle to the window we will create
XPLMWindowID    g_window = 0;
// when to remove the window
float           fTimeRemove = NAN;

// time until window is to be shown, will be destroyed after that
struct dispTextTy {
    float fTimeDisp;                        // until when to display this line?
    logLevelTy lvlDisp;                     // level of msg (defines text color)
    std::string text;                       // text of line
};
std::list<dispTextTy> listTexts;     // lines of text to be displayed

float COL_LVL[logMSG+1][3] = {          // text colors [RGB] depending on log level
    {0.00f, 0.00f, 0.00f},              // 0
    {1.00f, 1.00f, 1.00f},              // INFO (white)
    {1.00f, 1.00f, 0.00f},              // WARN (yellow)
    {1.00f, 0.00f, 0.00f},              // ERROR (red)
    {1.00f, 0.54f, 0.83f},              // FATAL (purple, FF8AD4)
    {1.00f, 1.00f, 1.00f}               // MSG (white)
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
    
    // Full as translucent dark box
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    
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
            XPLMSetWindowIsVisible ( g_window, false );
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

    // Create the message window
    XPLMCreateWindow_t params;
    params.structSize = IS_XPLM301 ? sizeof(params) : XPLMCreateWindow_s_210;
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
    LT_GetScreenSize(params.left, params.top, params.right, params.bottom,
                     LT_SCR_RIGHT_TOP_MOST);
    
    // define a window in the top right corner,
    // WIN_FROM_TOP point down from the top, WIN_WIDTH points wide,
    // enough height for all lines of text
    params.top -= WIN_FROM_TOP;
    params.right -= WIN_FROM_RIGHT;
    params.left = params.right - WIN_WIDTH;
    params.bottom = params.top - (WIN_ROW_HEIGHT * (2*int(listTexts.size())+1+
                                                    (needSeeingShowMsg() ? 2 : 0)));
    
    // if the window still exists just resize it
    if (g_window) {
        if (!XPLMGetWindowIsVisible( g_window))
            XPLMSetWindowIsVisible ( g_window, true );
        XPLMSetWindowGeometry(g_window, params.left, params.top, params.right, params.bottom);
    }
    else {
        // otherwise create a new one
        g_window = XPLMCreateWindowEx(&params);
        LOG_ASSERT(g_window);
    }
    
    return g_window;
}


// Show the special text "Seeing aircraft...showing..."
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, int numSee, int numShow, int bufTime)
{
    gNumSee     = numSee;
    gNumShow    = numShow;
    gBufTime    = bufTime;
    return needSeeingShowMsg() ? CreateMsgWindow(fTimeToDisplay, logINFO, nullptr) : nullptr;
}


void DestroyWindow()
{
    if ( g_window )
    {
        XPLMDestroyWindow(g_window);
        g_window = NULL;
        listTexts.clear();
   }
}

//
//MARK: Log
//

const char* LOG_LEVEL[] = {
    "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL", "MSG  "
};

// returns ptr to static buffer filled with log string
const char* GetLogString (const char* szPath, int ln, const char* szFunc,
                          logLevelTy lvl, const char* szMsg, va_list args )
{
    static char aszMsg[3072];
    float runS = dataRefs.GetMiscNetwTime();
    const unsigned runH = unsigned(runS / 3600.0f);
    runS -= runH * 3600.0f;
    const unsigned runM = unsigned(runS / 60.0f);
    runS -= runM * 60.0f;

    // prepare timestamp
    if (lvl < logMSG)                             // normal messages without, all other with location info
    {
        const char* szFile = strrchr(szPath, PATH_DELIM);  // extract file from path
        if (!szFile) szFile = szPath; else szFile++;
        snprintf(aszMsg, sizeof(aszMsg), "%u:%02u:%06.3f " LIVE_TRAFFIC " %s %s:%d/%s: ",
                 runH, runM, runS,                  // Running time stamp
                 LOG_LEVEL[lvl],                    // logging level
                 szFile, ln, szFunc);               // source code location info
    }
    else
        snprintf(aszMsg, sizeof(aszMsg), "%u:%02u:%06.3f " LIVE_TRAFFIC ": ",
                 runH, runM, runS);                 // Running time stamp
    
    // append given message
    if (args) {
        vsnprintf(&aszMsg[strlen(aszMsg)],
                  sizeof(aszMsg)-strlen(aszMsg)-1,      // we save one char for the CR
                  szMsg,
                  args);
    }

    // ensure there's a trailing CR
    size_t l = strlen(aszMsg);
    if ( aszMsg[l-1] != '\n' )
    {
        aszMsg[l]   = '\n';
        aszMsg[l+1] = 0;
    }

    // return the static buffer
    return aszMsg;
}

void LogMsg ( const char* szPath, int ln, const char* szFunc, logLevelTy lvl, const char* szMsg, ... )
{
    va_list args;

    va_start (args, szMsg);
    // write to log (flushed immediately -> expensive!)
    XPLMDebugString ( GetLogString(szPath, ln, szFunc, lvl, szMsg, args) );
    va_end (args);
}

// Might be used in macros of other packages like ImGui
void LogFatalMsg ( const char* szPath, int ln, const char* szFunc, const char* szMsg, ... )
{
    va_list args;

    va_start (args, szMsg);
    // write to log (flushed immediately -> expensive!)
    XPLMDebugString ( GetLogString(szPath, ln, szFunc, logFATAL, szMsg, args) );
    va_end (args);
}
