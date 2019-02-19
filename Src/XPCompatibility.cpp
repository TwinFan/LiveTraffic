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

// All includes are collected in one header
#include "LiveTraffic.h"

// X-Plane's versions
int XPC_XPlaneVersion = 0;
int XPC_XPLMVersion = 0;

//
// MARK: Types and Pointer to actual XPLM API functions, if available
//

typedef void* (f_XPLMGetScreenBoundsGlobal)(int*,int*,int*,int*);
f_XPLMGetScreenBoundsGlobal* pXPLMGetScreenBoundsGlobal = nullptr;

typedef void (f_XPLMSetWindowPositioningMode)(XPLMWindowID,XPLMWindowPositioningMode,int);
f_XPLMSetWindowPositioningMode* pXPLMSetWindowPositioningMode = nullptr;

typedef XPLMWindowID (f_XPGetWidgetUnderlyingWindow)(XPWidgetID);
f_XPGetWidgetUnderlyingWindow* pXPGetWidgetUnderlyingWindow = nullptr;

typedef void (f_XPLMGetAllMonitorBoundsGlobal)(XPLMReceiveMonitorBoundsGlobal_f,void*);
f_XPLMGetAllMonitorBoundsGlobal* pXPLMGetAllMonitorBoundsGlobal = nullptr;

//
// MARK: XPC functions
//

void XPC_SetWindowPositioningMode(XPLMWindowID              inWindowID,
                                  XPLMWindowPositioningMode inPositioningMode,
                                  int                       inMonitorIndex)
{
    LOG_ASSERT(pXPLMSetWindowPositioningMode);
    pXPLMSetWindowPositioningMode(inWindowID, inPositioningMode, inMonitorIndex);
}

XPLMWindowID XPC_GetWidgetUnderlyingWindow(XPWidgetID       inWidget)
{
    return pXPGetWidgetUnderlyingWindow ? pXPGetWidgetUnderlyingWindow(inWidget) : 0;
}

void XPC_GetAllMonitorBoundsGlobal(XPLMReceiveMonitorBoundsGlobal_f inMonitorBoundsCallback,
                                   void *               inRefcon)
{
    LOG_ASSERT(pXPLMGetAllMonitorBoundsGlobal);
    pXPLMGetAllMonitorBoundsGlobal(inMonitorBoundsCallback, inRefcon);
}

// find and initialize all function pointers
#define FIND_SYM(func) { p##func = (f_##func*)XPLMFindSymbol(#func); LOG_ASSERT(p##func); }
bool XPC_Init()
{
    // XP Version, need to distinguish per XP version
    XPLMHostApplicationID hostID = 0;
    XPLMGetVersions(&XPC_XPlaneVersion, &XPC_XPLMVersion, &hostID);
    if (hostID != xplm_Host_XPlane ||
        XPC_XPLMVersion < 210) {
        LOG_MSG(logFATAL,ERR_XPLANE_ONLY);
        return false;
    }
    
    // if XP11 then find new API functions
    if ( IS_XPLM301 ) {
        FIND_SYM(XPLMGetScreenBoundsGlobal);
        FIND_SYM(XPLMSetWindowPositioningMode);
        FIND_SYM(XPGetWidgetUnderlyingWindow);
        FIND_SYM(XPLMGetAllMonitorBoundsGlobal);
    }
    return true;
}

//
// MARK: LT functions
//

// callback function that receives monitor coordinates
int rm_l, rm_t, rm_r, rm_b;     // right-most window's coordinates
void CBReceiveMonitorBoundsGlobal(int,              // inMonitorIndex
                                  int    inLeftBx,
                                  int    inTopBx,
                                  int    inRightBx,
                                  int    inBottomBx,
                                  void * )          // inRefcon
{
    // right-most?
    if (inRightBx > rm_r) {
        rm_l = inLeftBx;
        rm_t = inTopBx;
        rm_r = inRightBx;
        rm_b = inBottomBx;
    }
}


// determines screen size
void LT_GetScreenSize (int& outLeft,
                       int& outTop,
                       int& outRight,
                       int& outBottom)
{
    // XP11 using global coordinates
    if (pXPLMGetAllMonitorBoundsGlobal) {
        // find righ-most window coordinates
        // this will only find full screen monitors!
        rm_l = rm_t = rm_r = rm_b = 0;
        pXPLMGetAllMonitorBoundsGlobal(CBReceiveMonitorBoundsGlobal, NULL);
        
        // did we find something? then return it
        if ( rm_l || rm_t || rm_r || rm_b ) {
            outLeft     = rm_l;
            outTop      = rm_t;
            outRight    = rm_r;
            outBottom   = rm_b;
            return;
        }
        // if we didn't find anything we are running in windowed mode
        // fall through to classic XP10 way of figuring out screen size
    }
    
    // XP10 or windowed mode
    outLeft = 0;
    XPLMGetScreenSize(&outRight,&outTop);
    outBottom = 0;
}
