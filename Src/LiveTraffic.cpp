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
 *
 * Uses
 * - XPlaneMP, see https://github.com/kuroneko/libxplanemp and http://xsb.xsquawkbox.net/developers/
 * - libpng 1.2.59, see http://www.libpng.org/pub/png/libpng.html
 */

// All includes are collected in one header
#include "LiveTraffic.h"

// in LTVersion.cpp:
extern bool InitFullVersion ();

//MARK: Globals

#if !defined(INIT_LOG_LEVEL)
    #if !defined(DEBUG)
        #define INIT_LOG_LEVEL logERR
    #else
        #define INIT_LOG_LEVEL logDEBUG
    #endif
#endif

// access to data refs
DataRefs dataRefs (INIT_LOG_LEVEL);

// Settings Dialog
LTSettingsUI settingsUI;

//MARK: Reload Plugins menu item
enum menuItems {
    MENU_ID_TOGGLE_AIRCRAFTS = 0,
    MENU_ID_AC_INFO_WND,
    MENU_ID_SETTINGS_UI,
#ifdef DEBUG
    MENU_ID_RELOAD_PLUGINS,
#endif
    CNT_MENU_ID                     // always last, number of elements
};

// ID of the "LiveTraffic" menu within the plugin menu; items numbers of its menu items
XPLMMenuID menuID = 0;
int aMenuItems[CNT_MENU_ID];

// Callback called by XP, so this is an entry point into the plugin
void MenuHandler(void * /*mRef*/, void * iRef)
{
    // LiveTraffic top level exception handling
    try {
        // act based on menu id
        switch (reinterpret_cast<unsigned long long>(iRef)) {
                
            case MENU_ID_TOGGLE_AIRCRAFTS:
                dataRefs.ToggleAircraftsDisplayed();
                break;
            case MENU_ID_AC_INFO_WND:
                ACIWnd::OpenNewWnd();
                break;
            case MENU_ID_SETTINGS_UI:
                settingsUI.Show();
                break;
#ifdef DEBUG
            case MENU_ID_RELOAD_PLUGINS:
                XPLMReloadPlugins();
                break;
#endif
        }
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
        // otherwise ignore
    } catch (...) {
        // ignore
    }
}

void MenuCheckAircraftsDisplayed ( int bChecked )
{
    XPLMCheckMenuItem(// checkmark the menu item if aircrafts shown
                      menuID,aMenuItems[MENU_ID_TOGGLE_AIRCRAFTS],
                      bChecked ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

int RegisterMenuItem ()
{
    // Create menu and menu item
    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), LIVE_TRAFFIC, NULL, 1);
    if ( item<0 ) { LOG_MSG(logERR,ERR_APPEND_MENU_ITEM); return 0; }
    
    menuID = XPLMCreateMenu(LIVE_TRAFFIC, XPLMFindPluginsMenu(), item, MenuHandler, NULL);
    if ( !menuID ) { LOG_MSG(logERR,ERR_CREATE_MENU); return 0; }
    
    // Show Aircrafts
    aMenuItems[MENU_ID_TOGGLE_AIRCRAFTS] =
        XPLMAppendMenuItem(menuID, MENU_TOGGLE_AIRCRAFTS, (void *)MENU_ID_TOGGLE_AIRCRAFTS,1);
    if ( aMenuItems[MENU_ID_TOGGLE_AIRCRAFTS]<0 ) { LOG_MSG(logERR,ERR_APPEND_MENU_ITEM); return 0; }
    // no checkmark symbol (but room for one later)
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_TOGGLE_AIRCRAFTS],xplm_Menu_Unchecked);

    // Open an aircraft info window
    aMenuItems[MENU_ID_AC_INFO_WND] =
    XPLMAppendMenuItem(menuID, MENU_AC_INFO_WND, (void *)MENU_ID_AC_INFO_WND,1);
    if ( aMenuItems[MENU_ID_AC_INFO_WND]<0 ) { LOG_MSG(logERR,ERR_APPEND_MENU_ITEM); return 0; }
    
    // Show Settings UI
    aMenuItems[MENU_ID_SETTINGS_UI] =
    XPLMAppendMenuItem(menuID, MENU_SETTINGS_UI, (void *)MENU_ID_SETTINGS_UI,1);
    if ( aMenuItems[MENU_ID_SETTINGS_UI]<0 ) { LOG_MSG(logERR,ERR_APPEND_MENU_ITEM); return 0; }
    
#ifdef DEBUG
    // Separator
    XPLMAppendMenuSeparator(menuID);
    
    // Reload Plugins
    aMenuItems[MENU_ID_RELOAD_PLUGINS] =
        XPLMAppendMenuItem(menuID, MENU_RELOAD_PLUGINS, (void *)MENU_ID_RELOAD_PLUGINS,1);
    if ( aMenuItems[MENU_ID_RELOAD_PLUGINS]<0 ) { LOG_MSG(logERR,ERR_APPEND_MENU_ITEM); return 0; }
#endif

    // Success
    LOG_MSG(logDEBUG,DBG_MENU_CREATED)
    return 1;
}

//MARK: XPlugin Callbacks
PLUGIN_API int XPluginStart(
							char *		outName,
							char *		outSig,
							char *		outDesc)
{
    // init our version number
    if (!InitFullVersion ()) return 0;

    // init random numbers
     srand((unsigned int)time(NULL));
    
    // tell X-Plane who we are
     strcpy_s(outName, 255, LIVE_TRAFFIC);
     strcpy_s(outSig,  255, PLUGIN_SIGNATURE);
     strcpy_s(outDesc, 255, PLUGIN_DESCRIPTION);

    // init DataRefs
    if (!dataRefs.Init()) return 0;
    
    // read Doc8643 file (which we could live without)
    Doc8643::ReadDoc8643File();
    
    // read FlightModel.prf file (which we could live without)
    LTAircraft::FlightModel::ReadFlightModelFile();
    
    // init Aircraft handling (including XPMP)
    if (!LTMainInit()) return 0;
    
    // create menu
    if (!RegisterMenuItem()) return 0;
    
    // Success
    return 1;
}

PLUGIN_API int  XPluginEnable(void)
{
    // Enable showing aircrafts
    if (!LTMainEnable()) return 0;

    // Create a message window and say hello
    SHOW_MSG(logMSG, MSG_WELCOME, LT_VERSION_FULL);
    if (LT_BETA_VER_LIMIT)
        SHOW_MSG(logWARN, BETA_LIMITED_VERSION, LT_BETA_VER_LIMIT_TXT);
#ifdef DEBUG
    SHOW_MSG(logWARN, DBG_DEBUG_BUILD);
#endif

    
    // Success
    return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*inFrom*/, int /*inMsg*/, void * /*inParam*/)
{ }

PLUGIN_API void XPluginDisable(void) {
    // if there still is a message window remove it
    DestroyWindow();
    
    // deregister Settings UI
    settingsUI.Disable();
    
    // stop showing aircrafts
    LTMainDisable ();

    // Meu item "Aircrafts displayed" no checkmark symbol (but room one later)
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_TOGGLE_AIRCRAFTS],xplm_Menu_Unchecked);

    LOG_MSG(logMSG, MSG_DISABLED);
}

PLUGIN_API void    XPluginStop(void)
{
    // Cleanup aircraft handling (including XPMP library)
    LTMainStop();
    
    // Cleanup dataRef registration
    dataRefs.Stop();
}

