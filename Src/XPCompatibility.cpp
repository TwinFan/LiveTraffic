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
    }
    return true;
}

//
// MARK: LT functions
//

// determines screen size
void LT_GetScreenSize (int * outLeft,     /* Can be NULL */
                       int * outTop,      /* Can be NULL */
                       int * outRight,    /* Can be NULL */
                       int * outBottom)   /* Can be NULL */
{
    if (pXPLMGetScreenBoundsGlobal) {
        pXPLMGetScreenBoundsGlobal(outLeft, outTop, outRight, outBottom);
    } else {
        if (outLeft) *outLeft = 0;
        XPLMGetScreenSize(outRight,outTop);
        if (outBottom) *outBottom = 0;
    }
}
