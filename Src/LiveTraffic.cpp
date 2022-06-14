/// @file       LiveTraffic.cpp
/// @brief      Main entry point by providing the mandatory X-Plane plugin callbacks `XPlugin...`
/// @details    Mandatory X-Plane plugin callbacks `XPlugin...`\n
///             Menu item and command handling\n
///             Controls activitation of one-time setup activities.
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

#if IBM
#include <objbase.h>        // for CoInitializeEx
#endif

// in LTVersion.cpp:
extern bool InitFullVersion ();

//MARK: Globals

// access to data refs
DataRefs dataRefs(logWARN);

//MARK: Plugin Menu
enum menuItems {
    MENU_ID_LIVETRAFFIC = 0,
    MENU_ID_INFO_LIST_WND,
    MENU_ID_AC_INFO_WND,
    MENU_ID_AC_INFO_WND_POPOUT,
    MENU_ID_AC_INFO_WND_SHOWN,
    MENU_ID_AC_INFO_WND_CLOSE_ALL,
    MENU_ID_TOGGLE_AIRCRAFT,
    MENU_ID_HAVE_TCAS,
    MENU_ID_TOGGLE_LABELS,
    MENU_ID_SETTINGS_UI,
    MENU_ID_HELP,
    MENU_ID_HELP_DOCUMENTATION,
    MENU_ID_HELP_FAQ,
    MENU_ID_HELP_MENU_ITEMS,
    MENU_ID_HELP_INFO_LIST_WND,
    MENU_ID_HELP_AC_INFO_WND,
    MENU_ID_HELP_SETTINGS,
    MENU_ID_HELP_INSTALL_CSL,
    MENU_ID_NEWVER,
#ifdef DEBUG
    MENU_ID_RELOAD_PLUGINS,
    MENU_ID_REMOVE_ALL_BUT,
#endif
    CNT_MENU_ID                     // always last, number of elements
};

// ID of the "LiveTraffic" menu within the plugin menu; items numbers of its menu items
XPLMMenuID menuID = 0, menuHelpID = 0;
int aMenuItems[CNT_MENU_ID];

// Callback called by XP, so this is an entry point into the plugin
void MenuHandler(void * /*mRef*/, void * iRef)
{
    // LiveTraffic top level exception handling
    try {
        // act based on menu id
        switch (reinterpret_cast<unsigned long long>(iRef)) {
            case MENU_ID_INFO_LIST_WND:
                XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_INFO_LIST_WND],
                                  InfoListWnd::ToggleDisplay() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
                break;
            case MENU_ID_AC_INFO_WND:
                ACIWnd::OpenNewWnd();
                break;
            case MENU_ID_AC_INFO_WND_POPOUT:
                ACIWnd::OpenNewWnd("", WND_MODE_POPOUT);
                break;
            case MENU_ID_AC_INFO_WND_SHOWN:
                XPLMCheckMenuItem(menuID, aMenuItems[MENU_ID_AC_INFO_WND_SHOWN],
                                  ACIWnd::ToggleHideShowAll() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
                break;
            case MENU_ID_AC_INFO_WND_CLOSE_ALL:
                ACIWnd::CloseAll();
                break;
            case MENU_ID_TOGGLE_AIRCRAFT:
                dataRefs.ToggleAircraftDisplayed();
                break;
            case MENU_ID_HAVE_TCAS:
                LTMainToggleAI(!dataRefs.HaveAIUnderControl() &&
                               !dataRefs.AwaitingAIControl());
                break;
            case MENU_ID_TOGGLE_LABELS:
                XPLMCheckMenuItem(menuID, aMenuItems[MENU_ID_TOGGLE_LABELS],
                                  dataRefs.ToggleLabelDraw() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
                break;
            case MENU_ID_SETTINGS_UI:
                XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_SETTINGS_UI],
                                  LTSettingsUI::ToggleDisplay() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
                break;
            case MENU_ID_NEWVER:
                LTOpenURL(LT_DOWNLOAD_URL);
                break;
#ifdef DEBUG
            case MENU_ID_RELOAD_PLUGINS:
                XPLMReloadPlugins();
                break;
            case MENU_ID_REMOVE_ALL_BUT:
                LTFlightData::RemoveAllAcButSelected();
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

// Callback for the help submenu, just opens the given help path
void MenuHandlerHelp (void * /*mRef*/, void * iRef)
{
    const char* helpPath = static_cast<const char*>(iRef);
    LTOpenHelp(helpPath);
}

// the "Aircraft displayed" menu item includes the number of displayed a/c
// (if the item is checked, i.e. active)
void MenuCheckAircraftDisplayed ( bool bChecked, int numAc )
{
    XPLMCheckMenuItem(// checkmark the menu item if aircraft shown
                      menuID,aMenuItems[MENU_ID_TOGGLE_AIRCRAFT],
                      bChecked ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    
    // update menu item's name with number of a/c
    // (if there are a/c, or is item enabled)
    if (bChecked || numAc > 0)
    {
        char szItemName[50];
        snprintf(szItemName, sizeof(szItemName), MENU_TOGGLE_AC_NUM, numAc);
        XPLMSetMenuItemName(menuID, aMenuItems[MENU_ID_TOGGLE_AIRCRAFT],
                            szItemName, 0);
    }
        else
    {
        XPLMSetMenuItemName(menuID, aMenuItems[MENU_ID_TOGGLE_AIRCRAFT],
                            MENU_TOGGLE_AIRCRAFT, 0);
    }
}

// called regularly from flight-loop callbacks to update the menu items status
void MenuUpdateAllItemStatus()
{
    // Is Info List window open?
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_INFO_LIST_WND],
                      InfoListWnd::IsDisplayed() ? xplm_Menu_Checked : xplm_Menu_Unchecked);

    // don't allow closing of a/c wnds in VR camera view
    XPLMEnableMenuItem(menuID, aMenuItems[MENU_ID_AC_INFO_WND_CLOSE_ALL],
                       !dataRefs.IsVREnabled() || !LTAircraft::IsCameraViewOn());
    // A/c info wnds shown?
    XPLMCheckMenuItem(menuID, aMenuItems[MENU_ID_AC_INFO_WND_SHOWN],
                      ACIWnd::AreShown() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    // A/c labels shown?
    XPLMCheckMenuItem(menuID, aMenuItems[MENU_ID_TOGGLE_LABELS],
                      dataRefs.ShallDrawLabels() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    // a/c displayed?
    MenuCheckAircraftDisplayed (dataRefs.AreAircraftDisplayed(),
                                 dataRefs.GetNumAc() );
    
    // "TCAS controlled" is three-fold:
    // checkmark the menu item if TCAS under control
    XPLMCheckMenuItem(menuID, aMenuItems[MENU_ID_HAVE_TCAS],
                      dataRefs.HaveAIUnderControl() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    // Set menu item's name depending if AI control was requested (but not yet granted)
    XPLMSetMenuItemName(menuID, aMenuItems[MENU_ID_HAVE_TCAS],
                        dataRefs.AwaitingAIControl() ? MENU_HAVE_TCAS_REQUSTD : MENU_HAVE_TCAS,
                        0);

    // Is Settings window open?
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_SETTINGS_UI],
                      LTSettingsUI::IsDisplayed() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}


std::string VerToStr (unsigned ver)
{
    return
    std::to_string(ver / 10000) + '.' +
    std::to_string((ver / 100) % 100) + '.' +
    std::to_string(ver % 100);
}


void HandleNewVersionAvail ()
{
    // not reasonable for no new version or if already added
    if (!verXPlaneOrg || aMenuItems[MENU_ID_NEWVER] != 0)
        return;
    

    // if the X-Plane.org version is not newer don't worry either
    if (verXPlaneOrg <= LT_VER_NO) {
        // save the current timestamp so we don't check too often
        // (we specifically don't do this in case an update is found,
        //  this way we keep reminding the user once there really IS an update.)
        dataRefs.SetLastCheckedNewVerNow();
        return;
    }
    
    // *** New version available! ***
    // add another menu item directing to the new version
    char buf[50];
    snprintf(buf, sizeof(buf), MENU_NEWVER, VerToStr(verXPlaneOrg).c_str());
    aMenuItems[MENU_ID_NEWVER] =
    XPLMAppendMenuItem(menuID, buf, (void *)MENU_ID_NEWVER,1);

    // make the user aware
    SHOW_MSG(logWARN,MSG_LT_NEW_VER_AVAIL,VerToStr(verXPlaneOrg).c_str());
}

/// append a menu item, if given with command
int AppendMenuItem (XPLMMenuID   inMenu,
                       const char*  inItemName,
                       void*        inItemRef,
                       XPLMCommandRef inCommandToExecute = nullptr)
{
    // use XP11 version to also set a command?
    if (inCommandToExecute)
        return XPLMAppendMenuItemWithCommand(inMenu, inItemName, inCommandToExecute);
    else
        return XPLMAppendMenuItem(inMenu, inItemName, inItemRef, 0);
}

bool RegisterMenuItem ()
{
    // clear menu array
    memset(aMenuItems, 0, sizeof(aMenuItems));
    
    // Create menu item and menu
    aMenuItems[MENU_ID_LIVETRAFFIC] = XPLMAppendMenuItem(XPLMFindPluginsMenu(), LIVE_TRAFFIC, NULL, 1);
    menuID = XPLMCreateMenu(LIVE_TRAFFIC, XPLMFindPluginsMenu(), aMenuItems[MENU_ID_LIVETRAFFIC], MenuHandler, NULL);
    if ( !menuID ) { LOG_MSG(logERR,ERR_CREATE_MENU,LIVE_TRAFFIC); return 0; }
    
    // Open the info list window
    aMenuItems[MENU_ID_INFO_LIST_WND] =
    AppendMenuItem(menuID, MENU_INFO_LIST_WND, (void *)MENU_ID_INFO_LIST_WND,
                   dataRefs.cmdLT[CR_INFO_LIST_WND]);
    
    // Open an aircraft info window
    aMenuItems[MENU_ID_AC_INFO_WND] =
    AppendMenuItem(menuID, MENU_AC_INFO_WND, (void *)MENU_ID_AC_INFO_WND,
                   dataRefs.cmdLT[CR_ACINFOWND_OPEN]);
    
    // Open an aircraft info window (popped out)
    aMenuItems[MENU_ID_AC_INFO_WND_POPOUT] =
    AppendMenuItem(menuID, MENU_AC_INFO_WND_POPOUT, (void *)MENU_ID_AC_INFO_WND_POPOUT,
                   dataRefs.cmdLT[CR_ACINFOWND_OPEN_POPPED_OUT]);

    // Show/Hide all info wnds / with checkmark symbol
    aMenuItems[MENU_ID_AC_INFO_WND_SHOWN] =
    AppendMenuItem(menuID, MENU_AC_INFO_WND_SHOWN, (void *)MENU_ID_AC_INFO_WND_SHOWN,
                   dataRefs.cmdLT[CR_ACINFOWND_HIDE_SHOW]);
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_AC_INFO_WND_SHOWN],
                      ACIWnd::AreShown() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    
    // Close all a/c info wnds
    aMenuItems[MENU_ID_AC_INFO_WND_CLOSE_ALL] =
    AppendMenuItem(menuID, MENU_AC_INFO_WND_CLOSEALL, (void *)MENU_ID_AC_INFO_WND_CLOSE_ALL,
                   dataRefs.cmdLT[CR_ACINFOWND_CLOSE_ALL]);

    // Separator
    XPLMAppendMenuSeparator(menuID);
    
    // Show Aircraft / with checkmark symbol
    aMenuItems[MENU_ID_TOGGLE_AIRCRAFT] =
    AppendMenuItem(menuID, MENU_TOGGLE_AIRCRAFT, (void *)MENU_ID_TOGGLE_AIRCRAFT,
                   dataRefs.cmdLT[CR_AC_DISPLAYED]);
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_TOGGLE_AIRCRAFT],xplm_Menu_Unchecked);
    
    // Have/Get TCAS / with checkmark symbol
    aMenuItems[MENU_ID_HAVE_TCAS] =
    AppendMenuItem(menuID, MENU_HAVE_TCAS, (void *)MENU_ID_HAVE_TCAS,
                   dataRefs.cmdLT[CR_AC_TCAS_CONTROLLED]);
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_HAVE_TCAS],xplm_Menu_Unchecked);
    
    // Toggle display of labels
    aMenuItems[MENU_ID_TOGGLE_LABELS] =
    AppendMenuItem(menuID, MENU_TOGGLE_LABELS, (void *)MENU_ID_TOGGLE_LABELS,
                   dataRefs.cmdLT[CR_LABELS_TOGGLE]);
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_TOGGLE_LABELS],
                      dataRefs.ShallDrawLabels() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    
    // Separator
    XPLMAppendMenuSeparator(menuID);
    
    // Show Settings UI
    aMenuItems[MENU_ID_SETTINGS_UI] =
    AppendMenuItem(menuID, MENU_SETTINGS_UI, (void *)MENU_ID_SETTINGS_UI,
                   dataRefs.cmdLT[CR_SETTINGS_UI]);
    XPLMCheckMenuItem(menuID,aMenuItems[MENU_ID_SETTINGS_UI],
                      LTSettingsUI::IsDisplayed() ? xplm_Menu_Checked : xplm_Menu_Unchecked);

    // --- Help submenu ---
    aMenuItems[MENU_ID_HELP] =
    XPLMAppendMenuItem(menuID, MENU_HELP, NULL, 1);
    menuHelpID = XPLMCreateMenu(MENU_HELP, menuID, aMenuItems[MENU_ID_HELP], MenuHandlerHelp, NULL);
    if ( !menuHelpID ) { LOG_MSG(logERR,ERR_CREATE_MENU,MENU_HELP); return 0; }

    aMenuItems[MENU_ID_HELP_DOCUMENTATION]= XPLMAppendMenuItem(menuHelpID, MENU_HELP_DOCUMENTATION, (void*)"",1);
    aMenuItems[MENU_ID_HELP_FAQ] =          XPLMAppendMenuItem(menuHelpID, MENU_HELP_FAQ,           (void*)HELP_FAQ,1);
    aMenuItems[MENU_ID_HELP_MENU_ITEMS] =   XPLMAppendMenuItem(menuHelpID, MENU_HELP_MENU_ITEMS,    (void*)HELP_MENU_ITEMS,1);
    aMenuItems[MENU_ID_HELP_INFO_LIST_WND]= XPLMAppendMenuItem(menuHelpID, MENU_HELP_INFO_LIST_WND, (void*)HELP_ILW,1);
    aMenuItems[MENU_ID_HELP_AC_INFO_WND] =  XPLMAppendMenuItem(menuHelpID, MENU_HELP_AC_INFO_WND,   (void*)HELP_AC_INFO_WND,1);
    aMenuItems[MENU_ID_HELP_SETTINGS] =     XPLMAppendMenuItem(menuHelpID, MENU_HELP_SETTINGS,      (void*)HELP_SETTINGS,1);
    aMenuItems[MENU_ID_HELP_INSTALL_CSL] =  XPLMAppendMenuItem(menuHelpID, MENU_HELP_INSTALL_CSL,   (void*)HELP_INSTALL_CSL,1);

#ifdef DEBUG
    // Separator
    XPLMAppendMenuSeparator(menuID);
    
    // Reload Plugins
    aMenuItems[MENU_ID_RELOAD_PLUGINS] =
        XPLMAppendMenuItem(menuID, MENU_RELOAD_PLUGINS, (void *)MENU_ID_RELOAD_PLUGINS,1);

    // Remove all a/c except for the currently selected one
    aMenuItems[MENU_ID_REMOVE_ALL_BUT] =
        XPLMAppendMenuItem(menuID, MENU_REMOVE_ALL_BUT, (void *)MENU_ID_REMOVE_ALL_BUT,1);
#endif
    
    // check for errors
    for (int item: aMenuItems) {
        if ( item<0 ) {
            LOG_MSG(logERR,ERR_APPEND_MENU_ITEM);
            return false;
        }
    }

    // Success
    LOG_MSG(logDEBUG,DBG_MENU_CREATED)
    return true;
}

//
// MARK: Commands
//

// this one is for commands that correspond directly to menu commands
// they just call the menu...
// (that is kinda contrary to what XPLMAppendMenuItemWithCommand intends to do...but the menu handler is there anyway and has even more)

struct cmdMenuMap {
    cmdRefsLT cmd;
    menuItems menu;
} CMD_MENU_MAP[] = {
    { CR_INFO_LIST_WND,             MENU_ID_INFO_LIST_WND },
    { CR_ACINFOWND_OPEN,            MENU_ID_AC_INFO_WND },
    { CR_ACINFOWND_OPEN_POPPED_OUT, MENU_ID_AC_INFO_WND_POPOUT },
    { CR_ACINFOWND_HIDE_SHOW,       MENU_ID_AC_INFO_WND_SHOWN },
    { CR_ACINFOWND_CLOSE_ALL,       MENU_ID_AC_INFO_WND_CLOSE_ALL },
    { CR_AC_DISPLAYED,              MENU_ID_TOGGLE_AIRCRAFT },
    { CR_AC_TCAS_CONTROLLED,        MENU_ID_HAVE_TCAS },
    { CR_LABELS_TOGGLE,             MENU_ID_TOGGLE_LABELS },
    { CR_SETTINGS_UI,               MENU_ID_SETTINGS_UI },
};

int CommandHandlerMenuItems (XPLMCommandRef       /*inCommand*/,
                             XPLMCommandPhase     inPhase,
                             void *               inRefcon) // contains menuItems
{
    if (inPhase == xplm_CommandBegin)
        MenuHandler(NULL, inRefcon);
    return 1;
}

bool RegisterCommandHandlers ()
{
    for (cmdMenuMap i: CMD_MENU_MAP)
        XPLMRegisterCommandHandler(dataRefs.cmdLT[i.cmd],
                                   CommandHandlerMenuItems,
                                   1, (void*)i.menu);
    return true;
}

//MARK: One-Time Setup (Flight Loop Callback)

/// Puts some timestamps into the log for analysis purposes
void LogTimestamps ()
{
    // Log current timestamp and sim-time-stamp
    LOG_MSG(logMSG, MSG_TIMESTAMPS,
            ts2string(std::time(nullptr)).c_str(),
            dataRefs.GetSimTimeString().c_str());
}

/// One-Time Setup state
static enum ONCE_CB_STATE
{ ONCE_CB_ADD_DREFS=0, ONCE_CB_AUTOSTART, ONCE_WAIT_FOR_VER, ONCE_CB_DONE }
eOneTimeState = ONCE_CB_ADD_DREFS;


/// Flightloop callback for one-time setup
float LoopCBOneTimeSetup (float, float, int, void*)
{
    static std::future<bool> futVerCheck;
    
    switch (eOneTimeState) {
        case ONCE_CB_ADD_DREFS:
        {
            // Create a message window and say hello
            SHOW_MSG(logINFO, MSG_WELCOME, LT_VERSION_FULL);
            if constexpr (LIVETRAFFIC_VERSION_BETA)
                SHOW_MSG(logWARN, BETA_LIMITED_VERSION, LT_BETA_VER_LIMIT_TXT);
#ifdef DEBUG
            SHOW_MSG(logWARN, DBG_DEBUG_BUILD);
#endif
            // Inform dataRef tools about our dataRefs
            dataRefs.InformDataRefEditors();
            
            // next: Auto Start, but wait another 2 seconds for that
            eOneTimeState = ONCE_CB_AUTOSTART;
            return 2;
        }
        case ONCE_CB_AUTOSTART:
            // Log a timestamp to synch timing for analysis purposes
            LogTimestamps ();
            
            // Auto Start display of aircraft
            if (dataRefs.GetAutoStart())
                dataRefs.SetAircraftDisplayed(true);
            
            // check at X-Plane.org for version updates
            if (dataRefs.NeedNewVerCheck()) {
                futVerCheck = std::async(std::launch::async, FetchXPlaneOrgVersion);
                eOneTimeState = ONCE_WAIT_FOR_VER;
                return 2;
            }
            
            // done, don't call me again
            eOneTimeState = ONCE_CB_DONE;
            return 0;
            
        case ONCE_WAIT_FOR_VER:
            // did the version check not yet come back?
            if (std::future_status::ready != futVerCheck.wait_for(std::chrono::microseconds(0)))
                return 2;
                
            // version check successful?
            if (futVerCheck.get())
                HandleNewVersionAvail();      // handle the outcome
            
            // done
            eOneTimeState = ONCE_CB_DONE;
            [[fallthrough]];
        default:
            // don't want to be called again
            return 0;
    }
}


//MARK: XPlugin Callbacks
PLUGIN_API int XPluginStart(
							char *		outName,
							char *		outSig,
							char *		outDesc)
{
    try {
        // init random numbers
         srand((unsigned int)time(NULL));
        
        // tell X-Plane who we are
        std::snprintf(outName, 255, "%s %u.%u.%u", LIVE_TRAFFIC,
                      LIVETRAFFIC_VER_MAJOR, LIVETRAFFIC_VER_MINOR, LIVETRAFFIC_VER_PATCH);
        strncpy_s(outSig,  255, PLUGIN_SIGNATURE,   100);
        strncpy_s(outDesc, 255, PLUGIN_DESCRIPTION, 100);

        // Keep track of this thread's id
        dataRefs.ThisThreadIsXP();
        
#ifdef DEBUG
        // install error handler
        XPLMSetErrorCallback(LTErrorCB);
#endif
        
        // init our version number
        // (also outputs the "LiveTraffic ... starting up" log message)
        if (!InitFullVersion ()) { DestroyWindow(); return 0; }
        
        // use native paths, i.e. Posix style (as opposed to HFS style)
        // https://developer.x-plane.com/2014/12/mac-plugin-developers-you-should-be-using-native-paths/
        XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1);

        // For nice logging, let XPMP2 know our names already
        XPMPSetPluginName(LIVE_TRAFFIC, LIVE_TRAFFIC_XPMP2);
        
        // init DataRefs
        if (!dataRefs.Init()) { DestroyWindow(); return 0; }
        
        // read FlightModel.prf file (which we could live without)
        LTAircraft::FlightModel::ReadFlightModelFile();
        
        // init Aircraft handling (including XPMP)
        if (!LTMainInit()) { DestroyWindow(); return 0; }
        
        // register commands
        if (!RegisterCommandHandlers()) { DestroyWindow(); return 0; }
        
        // create menu
        if (!RegisterMenuItem()) { DestroyWindow(); return 0; }
        
        // Setup ImGui window stuff
        if (!LTImgWindowInit()) { DestroyWindow(); return 0; }
        
#if IBM
        // Windows: Recommended before calling ShellExecuteA
        // https://docs.microsoft.com/en-us/windows/desktop/api/shellapi/nf-shellapi-shellexecutea
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif

        // Success
        return 1;
        
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}

PLUGIN_API int  XPluginEnable(void)
{
    try {
        // Register callback to inform DataRef Editor later on
        eOneTimeState = ONCE_CB_ADD_DREFS;
        XPLMRegisterFlightLoopCallback(LoopCBOneTimeSetup, 1, NULL);
        
        // Start reading apt.dat
        if (dataRefs.GetFdSnapTaxiDist_m() > 0.0)
            LTAptEnable();
        
        // Enable showing aircraft
        if (!LTMainEnable()) return 0;

        // Success
        return 1;

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * /*inParam*/)
{
    // *** Some other plugin asks for AI release,
    //     but as we are showing live planes we won't give up automatically,
    //     but we tell our user
    if (inMsg == XPLM_MSG_RELEASE_PLANES)
    {
        char who[256] = "?";
        char signature[256] = "";
        XPLMGetPluginInfo(inFrom, who, NULL, signature, NULL);
        
        // We do cease control to the XPMP2 Remote Control plugin
        // because that one will display our traffic, too:
        if (strncmp(signature, REMOTE_SIGNATURE, sizeof(signature)) == 0) {
            LOG_MSG(logINFO, INFO_REQU_AI_REMOTE);
            LTMainReleaseAIAircraft();
            MenuUpdateAllItemStatus();
        } else {
            SHOW_MSG(logINFO, INFO_REQU_AI_RELEASE, who);
        }
        return;
    }

    // Otherwise, we only process msgs from X-Plane
    if (inFrom != XPLM_PLUGIN_XPLANE)
        return;
    
#ifdef DEBUG
    // for me not having a VR rig I do some basic testing with sending XPLM_MSG_AIRPLANE_COUNT_CHANGED
    if (inMsg == XPLM_MSG_AIRPLANE_COUNT_CHANGED) {
        dataRefs.bSimVREntered = !dataRefs.bSimVREntered;
        inMsg = dataRefs.bSimVREntered ? XPLM_MSG_ENTERED_VR : XPLM_MSG_EXITING_VR;
    }
#endif
    
    switch (inMsg) {
        // *** entering VR mode ***
        case XPLM_MSG_ENTERED_VR:
            ACIWnd::MoveAllVR(true);
            break;
            
        // *** existing from VR mode ***
        case XPLM_MSG_EXITING_VR:
            ACIWnd::MoveAllVR(false);
            break;
    }
}

PLUGIN_API void XPluginDisable(void) {
    try {
        // unregister the one-time callback, just in case
        XPLMUnregisterFlightLoopCallback(LoopCBOneTimeSetup, NULL);

        // stop showing aircraft
        LTMainDisable ();

        // Stop reading apt.dat
        LTAptDisable();

        // if there still is a message window remove it
        DestroyWindow();
        
        // deregister Settings UI, close all windows, cleanup ImGui stuff
        ACIWnd::CloseAll();
        InfoListWnd::ToggleDisplay(-1);
        LTSettingsUI::ToggleDisplay(-1);
        
        LOG_MSG(logMSG, MSG_DISABLED);

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {
    }
}

PLUGIN_API void    XPluginStop(void)
{
    try {
        // Cleanup aircraft handling (including XPMP library)
        LTMainStop();
        
        // Cleanup dataRef registration, save config file
        dataRefs.Stop();
        
        // last chance to remove the message area window
        DestroyWindow();

        // cleanup ImGui stuff
        LTImgWindowCleanup();

#if IBM
        // Windows: Balance CoInitializeEx
        CoUninitialize();
#endif
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {
    }
}

#ifdef DEBUG
// Error callback, called by X-Plane if we use bogus data in our API calls
void LTErrorCB (const char* msg)
{
    char s[512];
    snprintf(s, sizeof(s), LIVE_TRAFFIC " FATAL ERROR CALLBACK: %s\n", msg);
    XPLMDebugString(s);
}
#endif
