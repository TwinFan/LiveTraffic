//
//  TextIO.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LiveTraffic.h"

// MARK: LiveTraffic Exception classes

// standard constructor
LTError::LTError (const char* _szFile, int _ln, const char* _szFunc,
                  logLevelTy _lvl,
                  const char* _szMsg, ...) :
fileName(_szFile), ln(_ln), funcName(_szFunc),
lvl(_lvl),
std::logic_error(GetLogString(_szFile, _ln, _szFunc, _lvl, _szMsg, NULL))
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
fileName(_szFile), ln(_ln), funcName(_szFunc),
lvl(_lvl),
std::logic_error(GetLogString(_szFile, _ln, _szFunc, _lvl, "", NULL))
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
fd(_fd),
posStr(_fd.Positions2String()),
LTError(_szFile,_ln,_szFunc,_lvl)
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
static XPLMWindowID    g_window = 0;

// time until window is to be shown, will be destroyed after that
static float fTimeDispWin = 0.0;                    // time when window to be destroyed
static logLevelTy lvlDisp = logMSG;                 // level of msg (defines text color)
static char aszMsgTxt[500] = "";                    // Message to be displayed

float COL_LVL[logMSG+1][3] = {          // text colors [RGB] depending on log level
    {0, 0, 0},                  // 0
    {1.0, 1.0, 1.0},            // INFO (white)
    {1.0, 1.0, 0.0},            // WARN (yellow)
    {1.0, 0.0, 0.0},            // ERROR (red)
    {0.63, 0.13, 0.94},         // FATAL (purple)
    {1.0, 1.0, 1.0}             // MSG (white)
};

//MARK: custom X-Plane message Window - Private Callbacks
// Callbacks we will register when we create our window
void    draw_msg(XPLMWindowID in_window_id, void * in_refcon)
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
    
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    
    b = WIN_WIDTH;                          // word wrap width
    
    // draw text, take color based on msg level
    XPLMDrawString(COL_LVL[lvlDisp], l, t - 20, aszMsgTxt, &b, xplmFont_Proportional);
    
    // time's up? -> remove the window, thus, the message
    if ((g_window == in_window_id) &&
        (fTimeDispWin > 0) &&           // if a time is set
        (dataRefs.GetTotalRunningTimeSec() >= fTimeDispWin))
    {
        DestroyWindow();
    }
}

int dummy_mouse_handler(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon)
{ return 0; }

XPLMCursorStatus dummy_cursor_status_handler(XPLMWindowID in_window_id, int x, int y, void * in_refcon)
{ return xplm_CursorDefault; }

int dummy_wheel_handler(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void * in_refcon)
{ return 0; }

void dummy_key_handler(XPLMWindowID in_window_id, char key, XPLMKeyFlags flags, char virtual_key, void * in_refcon, int losing_focus)
{ }


//MARK: custom X-Plane message Window - Create / Destroy
XPLMWindowID CreateMsgWindow(float fTimeToDisplay, logLevelTy lvl, const char* szMsg, ...)
{
    va_list args;

    // save the text in a static buffer queried by the drawing callback
    va_start (args, szMsg);
    vsnprintf(aszMsgTxt,
              sizeof(aszMsgTxt),
              szMsg,
              args);
    va_end (args);
    
    // (re)set the timer if a limit is given
    fTimeDispWin = fTimeToDisplay ? dataRefs.GetTotalRunningTimeSec() + fTimeToDisplay : 0;
    
    // save the level to be displayed (defines text color)
    lvlDisp = lvl;
    
    // if the window still exists just return
    if (g_window) return g_window;
    
    // Otherwise: Create the message window
    XPLMCreateWindow_t params;
    params.structSize = sizeof(params);
    params.visible = 1;
    params.drawWindowFunc = draw_msg;
    // Note on "dummy" handlers:
    // Even if we don't want to handle these events, we have to register a "do-nothing" callback for them
    params.handleMouseClickFunc = dummy_mouse_handler;
#if defined(XPLM300)
    params.handleRightClickFunc = dummy_mouse_handler;
#endif
    params.handleMouseWheelFunc = dummy_wheel_handler;
    params.handleKeyFunc = dummy_key_handler;
    params.handleCursorFunc = dummy_cursor_status_handler;
    params.refcon = NULL;
#if defined(XPLM300)
    params.layer = xplm_WindowLayerFloatingWindows;
#endif
#if defined(XPLM301)
    // Opt-in to styling our window like an X-Plane 11 native window
    // If you're on XPLM300, not XPLM301, swap this enum for the literal value 1.
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
#endif
    
    // Set the window's initial bounds
    // Note that we're not guaranteed that the main monitor's lower left is at (0, 0)...
    // We'll need to query for the global desktop bounds!
#if defined(XPLM300)
    XPLMGetScreenBoundsGlobal(&params.left, &params.top, &params.right, &params.bottom);
#else
    params.left = 0;
    XPLMGetScreenSize(&params.right,&params.top);
    params.bottom = 0;
#endif
    
    // define a window in the top right corner,
    // WIN_FROM_TOP point down from the top, WIN_WIDTH points wide, WIN_HEIGHT points high
    params.top -= WIN_FROM_TOP;
    params.right -= WIN_FROM_RIGHT;
    params.left = params.right - WIN_WIDTH;
    params.bottom = params.top - WIN_HEIGHT;
    
    g_window = XPLMCreateWindowEx(&params);
    if ( !g_window ) return NULL;
    
#if defined(XPLM300)
    // Position the window as a "free" floating window, which the user can drag around
    XPLMSetWindowPositioningMode(window, xplm_WindowPositionFree, -1);
    // Limit resizing our window: maintain a minimum width/height of 100 boxels and a max width/height of 300 boxels
    XPLMSetWindowResizingLimits(window, 200, 200, 300, 300);
    XPLMSetWindowTitle(window, LIVE_TRAFFIC);
#endif
    
    LOG_MSG(logDEBUG, DBG_WND_CREATED_UNTIL, fTimeDispWin, aszMsgTxt);
    
    return g_window;
}


void DestroyWindow()
{
    if ( g_window )
    {
        XPLMDestroyWindow(g_window);
        g_window = NULL;
        aszMsgTxt[0] = 0;
        fTimeDispWin = 0.0;
        LOG_MSG(logDEBUG,DBG_WND_DESTROYED);
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
    static char aszMsg[2048];
    const double simTime = dataRefs.GetSimTime();

    // prepare timestamp
    if ( lvl < logMSG )                             // normal messages without, all other with location info
    {
        const char* szFile = strrchr(szPath,'/');   // extract file from path
        if ( !szFile ) szFile = szPath; else szFile++;
        sprintf(aszMsg,"%s %.1f %s %s:%d/%s: ",
                LIVE_TRAFFIC, simTime, LOG_LEVEL[lvl],
                szFile, ln, szFunc);
    }
    else
        sprintf(aszMsg,"%s: ", LIVE_TRAFFIC);
    
    // append given message
    if (args) {
        vsnprintf(&aszMsg[strlen(aszMsg)],
                  sizeof(aszMsg)-strlen(aszMsg)-1,      // we save one char for the CR
                  szMsg,
                  args);
    }

    // ensure there's a trailing CR
    unsigned long l = strlen(aszMsg);
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
