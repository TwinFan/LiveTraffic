/// @file       XPCompatibility.cpp
/// @brief      Encapsulates calls to XP11 functionality, to allow the same binary to work with XP10 and use XP11 feature if running under XP11
/// @details    All functions, which are not part of XPLM210 (ie. are available with XP11 only)
///             are found and called via XPLMFindSymbol, ie. dynamically.\n
///             Proxy functions (starting with `XPC_...`) are provided to be used throughout the
///             source code. In the header file it is stated per function how the proxy function
///             reacts if it is called under XP10: failure, silent return, or returning some constant response
///             are typical options.\n
///             Most proxy functons just check for their XP function pointer to be available and then
///             pass on the call.\n
///             Some few proxy function include more elaborated code to work around limitations
///             or slightly different behaviour in XP11 vs XP10.
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

typedef void (f_XPLMGetAllMonitorBoundsGlobal)(XPLMReceiveMonitorBoundsGlobal_f,void*);
f_XPLMGetAllMonitorBoundsGlobal* pXPLMGetAllMonitorBoundsGlobal = nullptr;

typedef void (f_XPLMGetAllMonitorBoundsOS)(XPLMReceiveMonitorBoundsOS_f,void*);
f_XPLMGetAllMonitorBoundsOS* pXPLMGetAllMonitorBoundsOS = nullptr;

typedef void (f_XPLMSetWindowTitle) (XPLMWindowID,const char *);
f_XPLMSetWindowTitle* pXPLMSetWindowTitle = nullptr;

typedef int (f_XPLMWindowIsPoppedOut) (XPLMWindowID);
f_XPLMWindowIsPoppedOut* pXPLMWindowIsPoppedOut = nullptr;

typedef int (f_XPLMWindowIsInVR) (XPLMWindowID);
f_XPLMWindowIsInVR* pXPLMWindowIsInVR = nullptr;

typedef void (f_XPLMGetWindowGeometryOS) (XPLMWindowID,int*,int*,int*,int*);
f_XPLMGetWindowGeometryOS* pXPLMGetWindowGeometryOS = nullptr;

typedef void (f_XPLMSetWindowGeometryOS) (XPLMWindowID,int,int,int,int);
f_XPLMSetWindowGeometryOS* pXPLMSetWindowGeometryOS = nullptr;

typedef void (f_XPLMGetWindowGeometryVR) (XPLMWindowID,int*,int*);
f_XPLMGetWindowGeometryVR* pXPLMGetWindowGeometryVR = nullptr;

typedef void (f_XPLMSetWindowGeometryVR)(XPLMWindowID,int,int);
f_XPLMSetWindowGeometryVR* pXPLMSetWindowGeometryVR = nullptr;

typedef int (f_XPLMAppendMenuItemWithCommand)(XPLMMenuID,const char*,XPLMCommandRef);
f_XPLMAppendMenuItemWithCommand* pXPLMAppendMenuItemWithCommand = nullptr;

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

void XPC_GetAllMonitorBoundsGlobal(XPLMReceiveMonitorBoundsGlobal_f inMonitorBoundsCallback,
                                   void *               inRefcon)
{
    LOG_ASSERT(pXPLMGetAllMonitorBoundsGlobal);
    pXPLMGetAllMonitorBoundsGlobal(inMonitorBoundsCallback, inRefcon);
}

void XPC_GetAllMonitorBoundsOS(XPLMReceiveMonitorBoundsOS_f inMonitorBoundsCallback,
                               void *               inRefcon)
{
    LOG_ASSERT(pXPLMGetAllMonitorBoundsOS);
    pXPLMGetAllMonitorBoundsOS(inMonitorBoundsCallback, inRefcon);
}

void XPC_SetWindowTitle(XPLMWindowID         inWindowID,
                        const char *         inWindowTitle)
{
    if (pXPLMSetWindowTitle)
        pXPLMSetWindowTitle(inWindowID, inWindowTitle);
}

bool XPC_WindowIsPoppedOut(XPLMWindowID      inWindowID)
{
    return pXPLMWindowIsPoppedOut ? pXPLMWindowIsPoppedOut(inWindowID) != 0 : false;
}

bool XPC_WindowIsInVR(XPLMWindowID           inWindowID)
{
    return pXPLMWindowIsInVR ? pXPLMWindowIsInVR(inWindowID) != 0 : false;
}

void XPC_GetWindowGeometryOS(XPLMWindowID         inWindowID,
                             int *                outLeft,
                             int *                outTop,
                             int *                outRight,
                             int *                outBottom)
{
    LOG_ASSERT(pXPLMGetWindowGeometryOS);
    pXPLMGetWindowGeometryOS(inWindowID,outLeft,outTop,outRight,outBottom);
}

void XPC_SetWindowGeometryOS(XPLMWindowID         inWindowID,
                             int                  inLeft,
                             int                  inTop,
                             int                  inRight,
                             int                  inBottom)
{
    if (pXPLMSetWindowGeometryOS)
        pXPLMSetWindowGeometryOS(inWindowID,inLeft,inTop,inRight,inBottom);
}

void XPC_GetWindowGeometryVR(XPLMWindowID         inWindowID,
                             int *                outWidthBoxels,
                             int *                outHeightBoxels)
{
    LOG_ASSERT(pXPLMGetWindowGeometryVR);
    pXPLMGetWindowGeometryVR(inWindowID,outWidthBoxels,outHeightBoxels);
}

void XPC_SetWindowGeometryVR(XPLMWindowID         inWindowID,
                             int                  widthBoxels,
                             int                  heightBoxels)
{
    if (pXPLMSetWindowGeometryVR)
        pXPLMSetWindowGeometryVR(inWindowID,widthBoxels,heightBoxels);
}

int  XPC_AppendMenuItemWithCommand(XPLMMenuID           inMenu,
                                   const char *         inItemName,
                                   XPLMCommandRef       inCommandToExecute)
{
    LOG_ASSERT(pXPLMAppendMenuItemWithCommand);
    return pXPLMAppendMenuItemWithCommand(inMenu, inItemName, inCommandToExecute);
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
        FIND_SYM(XPLMGetAllMonitorBoundsGlobal);
        FIND_SYM(XPLMGetAllMonitorBoundsOS);
        FIND_SYM(XPLMWindowIsPoppedOut);
        FIND_SYM(XPLMWindowIsInVR);
        FIND_SYM(XPLMGetWindowGeometryOS);
        FIND_SYM(XPLMSetWindowGeometryOS);
        FIND_SYM(XPLMGetWindowGeometryVR);
        FIND_SYM(XPLMSetWindowGeometryVR);
        FIND_SYM(XPLMAppendMenuItemWithCommand);
    }
    return true;
}

//
// MARK: LT functions
//

// callback function that receives monitor coordinates
int rm_idx, rm_l, rm_t, rm_r, rm_b;     // window's idx & coordinates

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

void CBLowestIdxMonitorGlobal(int    inMonitorIndex,
                              int    inLeftBx,
                              int    inTopBx,
                              int    inRightBx,
                              int    inBottomBx,
                              void * )          // inRefcon
{
    // right-top-most?
    if (inMonitorIndex < rm_idx) {
        rm_idx = inMonitorIndex;
        rm_l   = inLeftBx;
        rm_t   = inTopBx;
        rm_r   = inRightBx;
        rm_b   = inBottomBx;
    }
}


// determines screen size
void LT_GetScreenSize (int& outLeft,
                       int& outTop,
                       int& outRight,
                       int& outBottom,
                       LTWhichScreenTy whichScreen,
                       bool bOSScreen)
{
    // XP11 using global coordinates
    if ( IS_XPLM301 ) {
        // find window coordinates
        // this will only find full screen monitors!
        rm_l = rm_t = rm_r = rm_b = 0;
        rm_idx = INT_MAX;
        
        if (bOSScreen) {
            // fetch OS window bounds
            XPC_GetAllMonitorBoundsOS
            (whichScreen == LT_SCR_RIGHT_TOP_MOST ?
             CBRightTopMostMonitorGlobal : CBLowestIdxMonitorGlobal,
             NULL);
        } else {
            // fetch global bounds of X-Plane-used windows
            XPC_GetAllMonitorBoundsGlobal
            (whichScreen == LT_SCR_RIGHT_TOP_MOST ?
             CBRightTopMostMonitorGlobal : CBLowestIdxMonitorGlobal,
             NULL);
        }
        
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
    outLeft = outBottom = 0;
    XPLMGetScreenSize(&outRight,&outTop);
}

// append a menu item,
// XP11: if given with command
int LT_AppendMenuItem (XPLMMenuID   inMenu,
                       const char*  inItemName,
                       void*        inItemRef,
                       XPLMCommandRef inCommandToExecute)
{
    // use XP11 version to also set a command?
    if (inCommandToExecute && pXPLMAppendMenuItemWithCommand)
        return XPC_AppendMenuItemWithCommand(inMenu, inItemName, inCommandToExecute);
    else
        return XPLMAppendMenuItem(inMenu, inItemName, inItemRef, 0);
}
