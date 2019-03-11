//
//  SettingsUI.cpp
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

//
//MARK: LTSettingsUI
//

LTSettingsUI::LTSettingsUI () :
widgetIds(nullptr)
{}


LTSettingsUI::~LTSettingsUI()
{
    // just in case...
    Disable();
}

//
//MARK: Window Structure
// Basics | Debug
//

// indexes into the below definition array, must be kept in synch with the same
enum UI_WIDGET_IDX_T {
    UI_MAIN_WND     = 0,
    // Buttons to select 'tabs'
    UI_BTN_BASICS,
    UI_BTN_AC_LABELS,
    UI_BTN_ADVANCED,
    UI_BTN_CSL,
    UI_BTN_DEBUG,
    UI_BTN_HELP,
    // "Basics" tab
    UI_BASICS_LIVE_SUB_WND,
    UI_BASICS_BTN_ENABLE,
    UI_BASICS_BTN_AUTO_START,
    UI_BASICS_CAP_FDCHANNELS,
    UI_BASICS_BTN_OPENSKY_LIVE,
    UI_BASICS_BTN_OPENSKY_MASTERDATA,
    UI_BASICS_BTN_ADSB_LIVE,
    UI_BASICS_BTN_REALTRAFFIC_LIVE,
    UI_BASICS_CAP_REALTRAFFIC_STATUS,
    UI_BASICS_CAP_REALTRAFFIC_METAR,

    UI_BASICS_CAP_VERSION_TXT,
    UI_BASICS_CAP_VERSION,

    UI_BASICS_RIGHT_SUB_WND,
    UI_BASICS_CAP_MISC,
    UI_BASICS_BTN_LND_LIGHTS_TAXI,
    UI_BASICS_CAP_PARALLEL,
    UI_BASICS_CAP_HIDE_BELOW_AGL,
    UI_BASICS_INT_HIDE_BELOW_AGL,
    UI_BASICS_BTN_HIDE_TAXIING,
    UI_BASICS_BTN_AI_ON_REQUEST,

    UI_BASICS_CAP_DBG_LIMIT,
    
    // "A/C Labels" tab
    UI_LABELS_SUB_WND,
    UI_LABELS_CAP_STATIC,
    UI_LABELS_BTN_TYPE,
    UI_LABELS_BTN_AC_ID,
    UI_LABELS_BTN_TRANSP,
    UI_LABELS_BTN_REG,
    UI_LABELS_BTN_OP,
    UI_LABELS_BTN_CALL_SIGN,
    UI_LABELS_BTN_FLIGHT_NO,
    UI_LABELS_BTN_ROUTE,

    UI_LABELS_CAP_DYNAMIC,
    UI_LABELS_BTN_PHASE,
    UI_LABELS_BTN_HEADING,
    UI_LABELS_BTN_ALT,
    UI_LABELS_BTN_HEIGHT,
    UI_LABELS_BTN_SPEED,
    UI_LABELS_BTN_VSI,
    
    UI_LABELS_CAP_COLOR,
    UI_LABELS_BTN_DYNAMIC,
    UI_LABELS_BTN_FIXED,
    UI_LABELS_TXT_COLOR,
    UI_LABELS_BTN_YELLOW,
    UI_LABELS_BTN_RED,
    UI_LABELS_BTN_GREEN,
    UI_LABELS_BTN_BLUE,
    
    UI_LABELS_CAP_WHEN,
    UI_LABELS_BTN_EXTERNAL,
    UI_LABELS_BTN_INTERNAL,
    UI_LABELS_BTN_VR,

    // "Advanced" tab
    UI_ADVCD_SUB_WND,
    UI_ADVCD_CAP_LOGLEVEL,
    UI_ADVCD_BTN_LOG_FATAL,
    UI_ADVCD_BTN_LOG_ERROR,
    UI_ADVCD_BTN_LOG_WARNING,
    UI_ADVCD_BTN_LOG_INFO,
    UI_ADVCD_BTN_LOG_DEBUG,
    UI_ADVCD_CAP_MSGAREA_LEVEL,
    UI_ADVCD_BTN_MSGAREA_FATAL,
    UI_ADVCD_BTN_MSGAREA_ERROR,
    UI_ADVCD_BTN_MSGAREA_WARNING,
    UI_ADVCD_BTN_MSGAREA_INFO,
    UI_ADVCD_CAP_MAX_NUM_AC,
    UI_ADVCD_INT_MAX_NUM_AC,
    UI_ADVCD_CAP_MAX_FULL_NUM_AC,
    UI_ADVCD_INT_MAX_FULL_NUM_AC,
    UI_ADVCD_CAP_FULL_DISTANCE,
    UI_ADVCD_INT_FULL_DISTANCE,
    UI_ADVCD_CAP_FD_STD_DISTANCE,
    UI_ADVCD_INT_FD_STD_DISTANCE,
    UI_ADVCD_CAP_FD_REFRESH_INTVL,
    UI_ADVCD_INT_FD_REFRESH_INTVL,
    UI_ADVCD_CAP_FD_BUF_PERIOD,
    UI_ADVCD_INT_FD_BUF_PERIOD,
    UI_ADVCD_CAP_AC_OUTDATED_INTVL,
    UI_ADVCD_INT_AC_OUTDATED_INTVL,

    // "CSL" tab
    UI_CSL_SUB_WND,
    UI_CSL_CAP_PATHS,
    UI_CSL_BTN_ENABLE_1,
    UI_CSL_TXT_PATH_1,
    UI_CSL_BTN_LOAD_1,
    UI_CSL_BTN_ENABLE_2,
    UI_CSL_TXT_PATH_2,
    UI_CSL_BTN_LOAD_2,
    UI_CSL_BTN_ENABLE_3,
    UI_CSL_TXT_PATH_3,
    UI_CSL_BTN_LOAD_3,
    UI_CSL_BTN_ENABLE_4,
    UI_CSL_TXT_PATH_4,
    UI_CSL_BTN_LOAD_4,
    UI_CSL_BTN_ENABLE_5,
    UI_CSL_TXT_PATH_5,
    UI_CSL_BTN_LOAD_5,
    UI_CSL_BTN_ENABLE_6,
    UI_CSL_TXT_PATH_6,
    UI_CSL_BTN_LOAD_6,
    UI_CSL_BTN_ENABLE_7,
    UI_CSL_TXT_PATH_7,
    UI_CSL_BTN_LOAD_7,

    UI_CSL_CAP_DEFAULT_AC_TYPE,
    UI_CSL_TXT_DEFAULT_AC_TYPE,
    UI_CSL_CAP_GROUND_VEHICLE_TYPE,
    UI_CSL_TXT_GROUND_VEHICLE_TYPE,
    
    // "Debug" tab
    UI_DEBUG_SUB_WND,
    UI_DEBUG_CAP_FILTER,
    UI_DEBUG_TXT_FILTER,
    UI_DEBUG_BTN_LOG_ACPOS,
    UI_DEBUG_BTN_LOG_MODELMATCH,
    UI_DEBUG_BTN_LOG_RAW_FD,
    UI_DEBUG_CAP_CSL_MODEL_MATCHING,
    UI_DEBUG_CAP_FIX_AC_ICAO_TYPE,
    UI_DEBUG_TXT_FIX_AC_ICAO_TYPE,
    UI_DEBUG_CAP_FIX_OP_ICAO,
    UI_DEBUG_TXT_FIX_OP_ICAO,
    UI_DEBUG_CAP_FIX_LIVERY,
    UI_DEBUG_TXT_FIX_LIVERY,

    // always last: number of UI elements
    UI_NUMBER_OF_ELEMENTS
};

// for ease of definition coordinates start at (0|0)
// window will be centered shortly before presenting it
TFWidgetCreate_t SETTINGS_UI[] =
{
    {   0,   0, 400, 330, 0, "LiveTraffic Settings", 1, NO_PARENT, xpWidgetClass_MainWindow, {xpProperty_MainWindowHasCloseBoxes, 1, xpProperty_MainWindowType,xpMainWindowStyle_Translucent,0,0} },
    // Buttons to select 'tabs'
    {  10,  30,  65,  10, 1, "Basics",               0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    {  75,  30,  65,  10, 1, "A/C Labels",           0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    { 140,  30,  65,  10, 1, "Advanced",             0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    { 205,  30,  65,  10, 1, "CSL",                  0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    { 270,  30,  65,  10, 1, "Debug",                0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    // Push button for help
    { 360,  30,  30,  10, 1, "?",                    0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorPushButton,  0,0, 0,0} },
    // "Basics" tab
    {  10,  50, 190, -10, 0, "Basics Live",          0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {  10,  10,  10,  10, 1, "Show Live Aircrafts",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  25,  10,  10, 1, "Auto Start",           0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  50,  -5,  10, 1, "Flight Data Channels:",0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  70,  10,  10, 1, "OpenSky Network",      0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  85,  10,  10, 1, "OpenSky Network Master Data",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "ADS-B Exchange",       0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 125,  10,  10, 1, "RealTraffic",          0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  80, 123,  -5,  10, 1, "",                     0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  20, 140,  -5,  10, 1, "",                     0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },

    {   5, -15,  -5,  10, 1, "Version",              0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  50, -15,  -5,  10, 1, "",                     0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },

    { 200,  50, -10, -10, 0, "Basics Right",         0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {   5,   8,  -5,  10, 1, "Misc options:",        0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  25,  10,  10, 1, "Landing lights during taxi", 0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  50,  -5,  10, 1, "Parallel with other traffic plugins:",0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {   5,  68, 140,  10, 1, "No a/c below [ft AGL]",0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { -50,  68, -10,  15, 1, "",                     0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,6, 0,0, 0,0} },
    {  10,  85,  10,  10, 1, "Hide a/c while taxiing", 0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "AI/TCAS on request only", 0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },

    {   5, -15,  -5,  10, 1, "",                    0, UI_BASICS_RIGHT_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    // "A/C Label" tab
    {  10,  50, -10, -10, 0, "A/C Label",           0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10, 190,  10, 1, "Static info:",        0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "ICAO A/C Type Code",  0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  45,  10,  10, 1, "Any A/C ID",          0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  60,  10,  10, 1, "Transponder Hex Code",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  75,  10,  10, 1, "Registration",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  90,  10,  10, 1, "ICAO Operator Code",  0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "Call Sign",           0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 120,  10,  10, 1, "Flight Number (rare)",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 135,  10,  10, 1, "Route",               0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  10, -10,  10, 1, "Dynamic data:",       0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 200,  30,  10,  10, 1, "Flight Phase",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  45,  10,  10, 1, "Heading",             0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  60,  10,  10, 1, "Altitude [ft]",       0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  75,  10,  10, 1, "Height AGL [ft]",     0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  90,  10,  10, 1, "Speed [kn]",          0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200, 105,  10,  10, 1, "VSI [ft/min]",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5, 155,  50,  10, 1, "Label Color:",        0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10, 170,  10,  10, 1, "Dynamic by Flight Model",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  10, 185,  10,  10, 1, "Fixed:",              0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  60, 182,  60,  15, 1, "",                    0, UI_LABELS_SUB_WND, xpWidgetClass_TextField, {xpProperty_MaxCharacters,6, 0,0, 0,0} },
    { 120, 185,  50,  10, 1, "Yellow",              0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    { 170, 185,  50,  10, 1, "Red",                 0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    { 220, 185,  50,  10, 1, "Green",               0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    { 270, 185,  50,  10, 1, "Blue",                0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {   5, 205,  50,  10, 1, "In which views to show A/C labels:", 0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10, 225,  10,  10, 1, "External",            0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  80, 225,  10,  10, 1, "Internal",            0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 150, 225,  10,  10, 1, "VR",                  0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    // "Advanced" tab
    {  10,  50, -10, -10, 0, "Advanced",            0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10,  -5,  10, 1, "Log Level:",          0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  80,  10,  10,  10, 1, "Fatal",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 140,  10,  10,  10, 1, "Error",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 200,  10,  10,  10, 1, "Warning",             0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 270,  10,  10,  10, 1, "Info",                0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 320,  10,  10,  10, 1, "Debug",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {   5,  30,  -5,  10, 1, "Msg Area:",           0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  80,  30,  10,  10, 1, "Fatal",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 140,  30,  10,  10, 1, "Error",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 200,  30,  10,  10, 1, "Warning",             0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 270,  30,  10,  10, 1, "Info",                0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {   5,  50, 225,  10, 1, "Max number of aircrafts",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230,  50,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5,  70, 225,  10, 1, "Max number of full a/c to draw",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230,  70,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5,  90, 225,  10, 1, "Max distance for drawing full a/c [nm]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230,  90,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,2, 0,0, 0,0} },
    {   5, 110, 225,  10, 1, "Search distance [nm]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230, 110,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 130, 225,  10, 1, "Live data refresh [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230, 130,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 150, 225,  10, 1, "Buffering period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230, 150,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 170, 225,  10, 1, "a/c outdated period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 230, 170,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    // "CSL" tab
    {  10,  50, -10, -10, 0, "CSL",                 0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10,  -5,  10, 1, "Enabled | Paths to CSL packages:", 0, UI_CSL_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25,  27, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330,  30,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10,  50,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25,  47, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330,  50,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10,  70,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25,  67, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330,  70,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10,  90,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25,  87, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330,  90,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10, 110,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25, 107, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330, 110,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10, 130,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25, 127, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330, 130,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {  10, 150,  10,  10, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  25, 147, 300,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField, {0,0, 0,0, 0,0} },
    { 330, 150,  50,  10, 1, "Load",                0, UI_CSL_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpPushButton, xpProperty_ButtonBehavior,xpButtonBehaviorPushButton, 0,0} },
    {   5, 230, 130,  10, 1, "Default a/c type",    0, UI_CSL_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 135, 227,  50,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,4, 0,0, 0,0} },
    {   5, 250, 130,  10, 1, "Ground vehicle type", 0, UI_CSL_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 135, 247,  50,  15, 1, "",                    0, UI_CSL_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,4, 0,0, 0,0} },
    // "Debug" tab
    {  10,  50, -10, -10, 0, "Debug",               0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10, 215,  10, 1, "Filter for transponder hex code:", 0, UI_DEBUG_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220,  10,  70,  15, 1, "",                    0, UI_DEBUG_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,8, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "Debug: Log a/c positions",  0, UI_DEBUG_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  45,  10,  10, 1, "Debug: Log model matching (XPlaneMP)",  0, UI_DEBUG_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  60,  10,  10, 1, "Debug: Log raw network flight data (LTRawFD.log)",  0, UI_DEBUG_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  80,  -5,  10, 1, "Forced model matching parameters for next aircrafts to create:", 0, UI_DEBUG_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {   5,  95, 215,  10, 1, "ICAO a/c type:",       0, UI_DEBUG_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220,  95,  70,  15, 1, "",                    0, UI_DEBUG_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,4, 0,0, 0,0} },
    {   5, 115, 215,  10, 1, "ICAO operator/airline:", 0, UI_DEBUG_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 115,  70,  15, 1, "",                    0, UI_DEBUG_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 135, 215,  10, 1, "livery/registration:", 0, UI_DEBUG_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 135,  70,  15, 1, "",                    0, UI_DEBUG_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,8, 0,0, 0,0} },
};

constexpr int NUM_WIDGETS = sizeof(SETTINGS_UI)/sizeof(SETTINGS_UI[0]);

static_assert(UI_NUMBER_OF_ELEMENTS == NUM_WIDGETS,
              "UI_WIDGET_IDX_T and SETTINGS_UI[] differ in number of elements!");

// creates the main window and all its widgets based on the above
// definition array
void LTSettingsUI::Enable()
{
    if (!isEnabled()) {
        // array, which receives ids of all created widgets
        widgetIds = new XPWidgetID[NUM_WIDGETS];
        LOG_ASSERT(widgetIds);
        memset(widgetIds, 0, sizeof(XPWidgetID)*NUM_WIDGETS );

        // create all widgets, i.e. the entire window structure, but keep invisible
        if (!TFUCreateWidgetsEx(SETTINGS_UI, NUM_WIDGETS, NULL, widgetIds,
                                GetDefaultWndOpenMode()))
        {
            SHOW_MSG(logERR,ERR_WIDGET_CREATE);
            return;
        }
        setId(widgetIds[0]);        // register in base class for message handling
        
        // some widgets with objects
        subBasicsLive.setId(widgetIds[UI_BASICS_LIVE_SUB_WND]);
        subBasicsRight.setId(widgetIds[UI_BASICS_RIGHT_SUB_WND]);
        subAcLabel.setId(widgetIds[UI_LABELS_SUB_WND]);
        subAdvcd.setId(widgetIds[UI_ADVCD_SUB_WND]);
        subCSL.setId(widgetIds[UI_CSL_SUB_WND]);
        subDebug.setId(widgetIds[UI_DEBUG_SUB_WND]);

        // organise the tab button group
        tabGrp.Add({
            widgetIds[UI_BTN_BASICS],
            widgetIds[UI_BTN_AC_LABELS],
            widgetIds[UI_BTN_ADVANCED],
            widgetIds[UI_BTN_CSL],
            widgetIds[UI_BTN_DEBUG]
        });
        tabGrp.SetChecked(widgetIds[UI_BTN_BASICS]);
        HookButtonGroup(tabGrp);
        
        // *** Basic ***
        // the following widgets are linked to dataRefs,
        // i.e. the dataRefs are changed automatically as soon as the
        //      widget's status/contents changes. This in turn
        //      directly controls LiveTraffic (see class DataRefs,
        //      which receives the callbacks for the changed dataRefs)
        //      Class LTSettingsUI has no more code for handling these:
        btnBasicsEnable.setId(widgetIds[UI_BASICS_BTN_ENABLE],
                              DATA_REFS_LT[DR_CFG_AIRCRAFTS_DISPLAYED]);
        btnBasicsAutoStart.setId(widgetIds[UI_BASICS_BTN_AUTO_START],
                              DATA_REFS_LT[DR_CFG_AUTO_START]);
        btnOpenSkyLive.setId(widgetIds[UI_BASICS_BTN_OPENSKY_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_ONLINE]);
        btnOpenSkyMasterdata.setId(widgetIds[UI_BASICS_BTN_OPENSKY_MASTERDATA],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA]);
        btnADSBLive.setId(widgetIds[UI_BASICS_BTN_ADSB_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_ADSB_EXCHANGE_ONLINE]);
        btnRealTraffic.setId(widgetIds[UI_BASICS_BTN_REALTRAFFIC_LIVE],
                             DATA_REFS_LT[DR_CHANNEL_REAL_TRAFFIC_ONLINE]);
        capRealTrafficStatus.setId(widgetIds[UI_BASICS_CAP_REALTRAFFIC_STATUS]);
        capRealTrafficMetar.setId(widgetIds[UI_BASICS_CAP_REALTRAFFIC_METAR]);
        UpdateRealTraffic();
        
        // * right-hand side *
        // landing lights during taxi?
        btnLndLightsTaxi.setId(widgetIds[UI_BASICS_BTN_LND_LIGHTS_TAXI],
                               DATA_REFS_LT[DR_CFG_LND_LIGHTS_TAXI]);

        // enhance parallel operation with other multiplayer clients
        intHideBelowAGL.setId(widgetIds[UI_BASICS_INT_HIDE_BELOW_AGL],
                              DATA_REFS_LT[DR_CFG_HIDE_BELOW_AGL]);
        // hide a/c while taxiing?
        btnHideTaxiing.setId(widgetIds[UI_BASICS_BTN_HIDE_TAXIING],
                             DATA_REFS_LT[DR_CFG_HIDE_TAXIING]);
        // AI/TCAS on request only?
        btnAIonRequest.setId(widgetIds[UI_BASICS_BTN_AI_ON_REQUEST],
                             DATA_REFS_LT[DR_CFG_AI_ON_REQUEST]);
        
        // version number
        XPSetWidgetDescriptor(widgetIds[UI_BASICS_CAP_VERSION],
                              LT_VERSION_FULL);
        if (LT_BETA_VER_LIMIT)
        {
            char dbgLimit[100];
            snprintf(dbgLimit,sizeof(dbgLimit), BETA_LIMITED_VERSION,LT_BETA_VER_LIMIT_TXT);
            XPSetWidgetDescriptor(widgetIds[UI_BASICS_CAP_DBG_LIMIT],
                                  dbgLimit);
        }
        
        // *** A/C Labels ***
        drCfgLabels.setDataRef(DATA_REFS_LT[DR_CFG_LABELS]);
        drCfgLabelShow.setDataRef(DATA_REFS_LT[DR_CFG_LABEL_SHOWN]);
        LabelBtnInit();
        
        // Color
        btnGrpLabelColorDyn.Add({
            widgetIds[UI_LABELS_BTN_DYNAMIC],
            widgetIds[UI_LABELS_BTN_FIXED]
        });
        btnGrpLabelColorDyn.SetChecked(dataRefs.IsLabelColorDynamic() ?
                                       widgetIds[UI_LABELS_BTN_DYNAMIC] :
                                       widgetIds[UI_LABELS_BTN_FIXED]
                                       );
        HookButtonGroup(btnGrpLabelColorDyn);
        drLabelColDyn.setDataRef(DATA_REFS_LT[DR_CFG_LABEL_COL_DYN]);
        intLabelColor.setId(widgetIds[UI_LABELS_TXT_COLOR],
                            DATA_REFS_LT[DR_CFG_LABEL_COLOR],
                            TFTextFieldWidget::TFF_HEX);

        // *** Advanced ***
        logLevelGrp.Add({
            widgetIds[UI_ADVCD_BTN_LOG_DEBUG],      // index 0 equals logDEBUG, which is also 0
            widgetIds[UI_ADVCD_BTN_LOG_INFO],       // ...
            widgetIds[UI_ADVCD_BTN_LOG_WARNING],
            widgetIds[UI_ADVCD_BTN_LOG_ERROR],
            widgetIds[UI_ADVCD_BTN_LOG_FATAL],      // index 4 equals logFATAL, which is also 4
        });
        logLevelGrp.SetCheckedIndex(dataRefs.GetLogLevel());
        HookButtonGroup(logLevelGrp);
        
        msgAreaLevelGrp.Add({
            widgetIds[UI_ADVCD_BTN_MSGAREA_INFO],       // index 0 is logINFO, which is 1
            widgetIds[UI_ADVCD_BTN_MSGAREA_WARNING],
            widgetIds[UI_ADVCD_BTN_MSGAREA_ERROR],
            widgetIds[UI_ADVCD_BTN_MSGAREA_FATAL],      // index 4 equals logFATAL, which is also 4
        });
        msgAreaLevelGrp.SetCheckedIndex(dataRefs.GetMsgAreaLevel() - 1);
        HookButtonGroup(msgAreaLevelGrp);

        // link some buttons directly to dataRefs:
        intMaxNumAc.setId(widgetIds[UI_ADVCD_INT_MAX_NUM_AC],
                          DATA_REFS_LT[DR_CFG_MAX_NUM_AC]);
        intMaxFullNumAc.setId(widgetIds[UI_ADVCD_INT_MAX_FULL_NUM_AC],
                          DATA_REFS_LT[DR_CFG_MAX_FULL_NUM_AC]);
        intFullDistance.setId(widgetIds[UI_ADVCD_INT_FULL_DISTANCE],
                          DATA_REFS_LT[DR_CFG_FULL_DISTANCE]);
        intFdStdDistance.setId(widgetIds[UI_ADVCD_INT_FD_STD_DISTANCE],
                          DATA_REFS_LT[DR_CFG_FD_STD_DISTANCE]);
        intFdRefreshIntvl.setId(widgetIds[UI_ADVCD_INT_FD_REFRESH_INTVL],
                          DATA_REFS_LT[DR_CFG_FD_REFRESH_INTVL]);
        intFdBufPeriod.setId(widgetIds[UI_ADVCD_INT_FD_BUF_PERIOD],
                          DATA_REFS_LT[DR_CFG_FD_BUF_PERIOD]);
        intAcOutdatedIntvl.setId(widgetIds[UI_ADVCD_INT_AC_OUTDATED_INTVL],
                          DATA_REFS_LT[DR_CFG_AC_OUTDATED_INTVL]);

        // *** CSL ***
        // Initialize all paths (3 elements each: check box, text field, button)
        const DataRefs::vecCSLPaths& paths = dataRefs.GetCSLPaths();
        for (size_t i=0; i < SETUI_CSL_PATHS; i++) {
            const size_t wIdx = UI_CSL_BTN_ENABLE_1 + i*SETUI_CSL_ELEMS_PER_PATH;
            txtCSLPaths[i].setId(widgetIds[wIdx+1]);            // connect text object to widget
            if (i < paths.size()) {                             // if there is a configured path for this line
                XPSetWidgetProperty(widgetIds[wIdx  ],          // check box
                                    xpProperty_ButtonState,
                                    paths[i].enabled());
                txtCSLPaths[i].SetDescriptor(paths[i].path);    // text field
            }
        }
        
        txtDefaultAcType.setId(widgetIds[UI_CSL_TXT_DEFAULT_AC_TYPE]);
        txtDefaultAcType.tfFormat = TFTextFieldWidget::TFF_UPPER_CASE;
        txtDefaultAcType.SetDescriptor(dataRefs.GetDefaultAcIcaoType());
        
        txtGroundVehicleType.setId(widgetIds[UI_CSL_TXT_GROUND_VEHICLE_TYPE]);
        txtGroundVehicleType.tfFormat = TFTextFieldWidget::TFF_UPPER_CASE;
        txtGroundVehicleType.SetDescriptor(dataRefs.GetDefaultCarIcaoType());
        
        // *** Debug ***

        // filter for transponder hex code
        txtDebugFilter.setId(widgetIds[UI_DEBUG_TXT_FILTER]);
        txtDebugFilter.SearchFlightData(dataRefs.GetDebugAcFilter());
        
        // debug options
        btnDebugLogACPos.setId(widgetIds[UI_DEBUG_BTN_LOG_ACPOS],
                               DATA_REFS_LT[DR_DBG_AC_POS]);
        btnDebugLogModelMatch.setId(widgetIds[UI_DEBUG_BTN_LOG_MODELMATCH],
                                    DATA_REFS_LT[DR_DBG_MODEL_MATCHING]);
        btnDebugLogRawFd.setId(widgetIds[UI_DEBUG_BTN_LOG_RAW_FD],
                               DATA_REFS_LT[DR_DBG_LOG_RAW_FD]);
        
        // forced values for CSL model matching tests
        txtFixAcType.setId(widgetIds[UI_DEBUG_TXT_FIX_AC_ICAO_TYPE]);
        txtFixAcType.tfFormat = TFTextFieldWidget::TFF_UPPER_CASE;
        txtFixAcType.SetDescriptor(dataRefs.cslFixAcIcaoType);
        
        txtFixOp.setId(widgetIds[UI_DEBUG_TXT_FIX_OP_ICAO]);
        txtFixOp.tfFormat = TFTextFieldWidget::TFF_UPPER_CASE;
        txtFixOp.SetDescriptor(dataRefs.cslFixOpIcao);
        
        txtFixLivery.setId(widgetIds[UI_DEBUG_TXT_FIX_LIVERY]);
        txtFixLivery.tfFormat = TFTextFieldWidget::TFF_UPPER_CASE;
        txtFixOp.SetDescriptor(dataRefs.cslFixLivery);

        // center the UI
        Center();
    }
}

void LTSettingsUI::Disable()
{
    if (isEnabled()) {
        // remove widgets and free memory
        XPDestroyWidget(*widgetIds, 1);
        delete widgetIds;
        widgetIds = nullptr;
    }
}

// make sure I'm created before first use
void LTSettingsUI::Show (bool bShow)
{
    if (bShow)              // create before use
        Enable();
    TFWidget::Show(bShow);  // show/hide
    
    // make sure we are in the right window mode
    if (GetWndMode() != GetDefaultWndOpenMode()) {      // only possible in XP11
        if (GetDefaultWndOpenMode() == TF_MODE_VR)
            SetWindowPositioningMode(xplm_WindowVR, -1);
        else
            SetWindowPositioningMode(xplm_WindowPositionFree, -1);	
    }
}

// capture entry into 'filter for transponder hex code' field
// and into CSL paths
bool LTSettingsUI::MsgTextFieldChanged (XPWidgetID textWidget, std::string text)
{
    // *** Advanced ***
    if (txtDebugFilter == textWidget) {
        // set the filter a/c if defined
        if (txtDebugFilter.HasTranspIcao())
            DataRefs::LTSetDebugAcFilter(NULL,txtDebugFilter.GetTranspIcaoInt());
        else
            DataRefs::LTSetDebugAcFilter(NULL,0);
        return true;
    }
    
    // *** CSL ***
    // if any of the paths changed we store that path
    for (int i = 0; i < SETUI_CSL_PATHS; i++)
        if (widgetIds[UI_CSL_TXT_PATH_1 + i*SETUI_CSL_ELEMS_PER_PATH] == textWidget) {
            SaveCSLPath(i);
            return true;
        }
    
    // if the types change we store them (and if setting fails after validation,
    // then  we restore the current value)
    if (txtDefaultAcType == textWidget) {
        if (!dataRefs.SetDefaultAcIcaoType(text))
            txtDefaultAcType.SetDescriptor(dataRefs.GetDefaultAcIcaoType());
        return true;
    }
    if (txtGroundVehicleType == textWidget) {
        if (!dataRefs.SetDefaultCarIcaoType(text))
            txtGroundVehicleType.SetDescriptor(dataRefs.GetDefaultCarIcaoType());
        return true;
    }

    // *** Debug ***
    // save forced model matching parameters
    if (txtFixAcType == textWidget  ||
        txtFixOp     == textWidget  ||
        txtFixLivery == textWidget)
    {
        dataRefs.cslFixAcIcaoType   = txtFixAcType.GetDescriptor();
        dataRefs.cslFixOpIcao       = txtFixOp.GetDescriptor();
        dataRefs.cslFixLivery       = txtFixLivery.GetDescriptor();
        
        if (dataRefs.cslFixAcIcaoType.empty()   &&
            dataRefs.cslFixOpIcao.empty()       &&
            dataRefs.cslFixLivery.empty())
            SHOW_MSG(logWARN, MSG_MDL_NOT_FORCED)
        else
            SHOW_MSG(logWARN, MSG_MDL_FORCED,
                     dataRefs.cslFixAcIcaoType.c_str(),
                     dataRefs.cslFixOpIcao.c_str(),
                     dataRefs.cslFixLivery.c_str());
        return true;
    }

    
    // not ours
    return TFMainWindowWidget::MsgTextFieldChanged(textWidget, text);
}


// writes current values out into config file
bool LTSettingsUI::MsgHidden (XPWidgetID hiddenWidget)
{
    if (hiddenWidget == *this) {        // only if it was me who got hidden
        // then only save the config file
        dataRefs.SaveConfigFile();
    }
    // pass on in class hierarchy
    return TFMainWindowWidget::MsgHidden(hiddenWidget);
}

// update state of some buttons from dataRef every second
bool LTSettingsUI::TfwMsgMain1sTime ()
{
    TFMainWindowWidget::TfwMsgMain1sTime();
    logLevelGrp.SetCheckedIndex(dataRefs.GetLogLevel());
    msgAreaLevelGrp.SetCheckedIndex(dataRefs.GetMsgAreaLevel() - 1);
    // real traffic stuff
    UpdateRealTraffic();
    // read current 'when-to-show' config and set accordingly
    DataRefs::LabelShowCfgTy show = dataRefs.GetLabelShowCfg();
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_EXTERNAL],xpProperty_ButtonState,show.bExternal);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_INTERNAL],xpProperty_ButtonState,show.bInternal);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_VR],xpProperty_ButtonState,show.bVR);

    return true;
}

// handles show/hide of 'tabs', values of logging level
bool LTSettingsUI::MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked)
{
    // first pass up the class hierarchy to make sure the button groups are handled correctly
    bool bRet = TFMainWindowWidget::MsgButtonStateChanged(buttonWidget, bNowChecked);
    
    // *** Tab Groups ***
    // if the button is one of our tab buttons show/hide the appropriate subwindow
    if (widgetIds[UI_BTN_BASICS] == buttonWidget) {
        subBasicsLive.Show(bNowChecked);
        subBasicsRight.Show(bNowChecked);
        return true;
    }
    else if (widgetIds[UI_BTN_AC_LABELS] == buttonWidget) {
        subAcLabel.Show(bNowChecked);
        return true;
    }
    else if (widgetIds[UI_BTN_ADVANCED] == buttonWidget) {
        subAdvcd.Show(bNowChecked);
        return true;
    }
    else if (widgetIds[UI_BTN_CSL] == buttonWidget) {
        subCSL.Show(bNowChecked);
        return true;
    }
    else if (widgetIds[UI_BTN_DEBUG] == buttonWidget) {
        subDebug.Show(bNowChecked);
        return true;
    }

    // *** A/C Labels ***
    // if any of the a/c label check boxes changes we set the config accordingly
    if (widgetIds[UI_LABELS_BTN_TYPE]       == buttonWidget ||
        widgetIds[UI_LABELS_BTN_AC_ID]      == buttonWidget ||
        widgetIds[UI_LABELS_BTN_TRANSP]     == buttonWidget ||
        widgetIds[UI_LABELS_BTN_REG]        == buttonWidget ||
        widgetIds[UI_LABELS_BTN_OP]         == buttonWidget ||
        widgetIds[UI_LABELS_BTN_CALL_SIGN]  == buttonWidget ||
        widgetIds[UI_LABELS_BTN_FLIGHT_NO]  == buttonWidget ||
        widgetIds[UI_LABELS_BTN_ROUTE]      == buttonWidget ||
        widgetIds[UI_LABELS_BTN_PHASE]      == buttonWidget ||
        widgetIds[UI_LABELS_BTN_HEADING]    == buttonWidget ||
        widgetIds[UI_LABELS_BTN_ALT]        == buttonWidget ||
        widgetIds[UI_LABELS_BTN_HEIGHT]     == buttonWidget ||
        widgetIds[UI_LABELS_BTN_SPEED]      == buttonWidget ||
        widgetIds[UI_LABELS_BTN_VSI]        == buttonWidget ||
        // when-to-show selection
        widgetIds[UI_LABELS_BTN_EXTERNAL]   == buttonWidget ||
        widgetIds[UI_LABELS_BTN_INTERNAL]   == buttonWidget ||
        widgetIds[UI_LABELS_BTN_VR]         == buttonWidget)
    {
        LabelBtnSave();
        return true;
    }
    
    // dynamic / fixed label colors?
    if (widgetIds[UI_LABELS_BTN_DYNAMIC]    == buttonWidget ||
        widgetIds[UI_LABELS_BTN_FIXED]      == buttonWidget)
    {
        drLabelColDyn.Set(buttonWidget == widgetIds[UI_LABELS_BTN_DYNAMIC]);
        return true;
    }
    
    // *** Advanced ***
    // if any of the log-level radio buttons changes we set log-level accordingly
    if (bNowChecked && logLevelGrp.isInGroup(buttonWidget))
    {
        dataRefs.SetLogLevel(logLevelGrp.GetCheckedIndex());
        return true;
    }
    if (bNowChecked && msgAreaLevelGrp.isInGroup(buttonWidget))
    {
        dataRefs.SetMsgAreaLevel(msgAreaLevelGrp.GetCheckedIndex() + 1);
        return true;
    }

    // *** CSL ***
    // if any of the enable-check boxes changed we store that setting
    for (int i = 0; i < SETUI_CSL_PATHS; i++)
        if (widgetIds[UI_CSL_BTN_ENABLE_1 + i*SETUI_CSL_ELEMS_PER_PATH] == buttonWidget) {
            SaveCSLPath(i);
            return true;
        }
    
    return bRet;
}

// push buttons
bool LTSettingsUI::MsgPushButtonPressed (XPWidgetID buttonWidget)
{
    // *** Help ***
    if (widgetIds[UI_BTN_HELP] == buttonWidget)
    {
        // open help for the currently selected tab of the settings dialog
        const char* helpSettingsPaths[5] = HELP_SETTINGS_PATHS;
        LTOpenHelp(helpSettingsPaths[tabGrp.GetCheckedIndex()]);
        return true;
    }
    
    // *** A/C Labels ***
    // color presets?
    if (widgetIds[UI_LABELS_BTN_YELLOW] == buttonWidget) { intLabelColor.Set(COLOR_YELLOW); return true; }
    if (widgetIds[UI_LABELS_BTN_RED]    == buttonWidget) { intLabelColor.Set(COLOR_RED);    return true; }
    if (widgetIds[UI_LABELS_BTN_GREEN]  == buttonWidget) { intLabelColor.Set(COLOR_GREEN);  return true; }
    if (widgetIds[UI_LABELS_BTN_BLUE]   == buttonWidget) { intLabelColor.Set(COLOR_BLUE);   return true; }
    
    // *** CSL ***
    // any of the "Load" buttons pushed?
    for (int i=0; i < SETUI_CSL_PATHS; i++) {
        if (widgetIds[UI_CSL_BTN_LOAD_1 + i*SETUI_CSL_ELEMS_PER_PATH] == buttonWidget) {
            SaveCSLPath(i);
            if (dataRefs.LoadCSLPackage(i))
                // successfully loaded...not update all CSL models in use
                LTFlightData::UpdateAllModels();
            return true;
        }
    }
    
    // we don't know that button...
    return TFMainWindowWidget::MsgPushButtonPressed(buttonWidget);
}

// Handle checkboxes for a/c labels
void LTSettingsUI::LabelBtnInit()
{
    // read current label configuration and init the checkboxes accordingly
    DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg();
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_TYPE],xpProperty_ButtonState,cfg.bIcaoType);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_AC_ID],xpProperty_ButtonState,cfg.bAnyAcId);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_TRANSP],xpProperty_ButtonState,cfg.bTranspCode);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_REG],xpProperty_ButtonState,cfg.bReg);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_OP],xpProperty_ButtonState,cfg.bIcaoOp);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_CALL_SIGN],xpProperty_ButtonState,cfg.bCallSign);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_FLIGHT_NO],xpProperty_ButtonState,cfg.bFlightNo);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_ROUTE],xpProperty_ButtonState,cfg.bRoute);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_PHASE],xpProperty_ButtonState,cfg.bPhase);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_HEADING],xpProperty_ButtonState,cfg.bHeading);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_ALT],xpProperty_ButtonState,cfg.bAlt);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_HEIGHT],xpProperty_ButtonState,cfg.bHeightAGL);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_SPEED],xpProperty_ButtonState,cfg.bSpeed);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_VSI],xpProperty_ButtonState,cfg.bVSI);
    
    // read current 'when-to-show' config and init accordingly
    DataRefs::LabelShowCfgTy show = dataRefs.GetLabelShowCfg();
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_EXTERNAL],xpProperty_ButtonState,show.bExternal);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_INTERNAL],xpProperty_ButtonState,show.bInternal);
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_VR],xpProperty_ButtonState,show.bVR);
}

void LTSettingsUI::LabelBtnSave()
{
    // store the checkboxes states in a zero-inited configuration
    DataRefs::LabelCfgTy cfg = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cfg.bIcaoType     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TYPE],xpProperty_ButtonState,NULL);
    cfg.bAnyAcId      = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_AC_ID],xpProperty_ButtonState,NULL);
    cfg.bTranspCode   = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TRANSP],xpProperty_ButtonState,NULL);
    cfg.bReg          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_REG],xpProperty_ButtonState,NULL);
    cfg.bIcaoOp       = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_OP],xpProperty_ButtonState,NULL);
    cfg.bCallSign     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_CALL_SIGN],xpProperty_ButtonState,NULL);
    cfg.bFlightNo     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_FLIGHT_NO],xpProperty_ButtonState,NULL);
    cfg.bRoute        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ROUTE],xpProperty_ButtonState,NULL);
    cfg.bPhase        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_PHASE],xpProperty_ButtonState,NULL);
    cfg.bHeading      = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEADING],xpProperty_ButtonState,NULL);
    cfg.bAlt          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ALT],xpProperty_ButtonState,NULL);
    cfg.bHeightAGL    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEIGHT],xpProperty_ButtonState,NULL);
    cfg.bSpeed        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_SPEED],xpProperty_ButtonState,NULL);
    cfg.bVSI          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_VSI],xpProperty_ButtonState,NULL);
    // save as current config
    drCfgLabels.Set(cfg.GetInt());
    
    // store the when-to-show information in a similar way
    DataRefs::LabelShowCfgTy show = { 0, 0, 0 };
    show.bExternal    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_EXTERNAL],xpProperty_ButtonState,NULL);
    show.bInternal    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_INTERNAL],xpProperty_ButtonState,NULL);
    show.bVR          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_VR],xpProperty_ButtonState,NULL);
    drCfgLabelShow.Set(show.GetInt());
}

void LTSettingsUI::UpdateRealTraffic()
{
    if (dataRefs.pRTConn) {
        capRealTrafficStatus.SetDescriptor(dataRefs.pRTConn->GetStatusStr());
        capRealTrafficMetar.SetDescriptor(dataRefs.pRTConn->IsConnected() ?
                                          std::to_string(std::lround(dataRefs.pRTConn->GetHPA())) +
                                          " hPa @ " + dataRefs.pRTConn->GetMetarIcao() : "");
    } else {
        capRealTrafficStatus.SetDescriptor("");
        capRealTrafficMetar.SetDescriptor("");
    }
}

void LTSettingsUI::SaveCSLPath(int idx)
{
    // what to save
    DataRefs::CSLPathCfgTy newPath {
        static_cast<bool>(XPGetWidgetProperty(widgetIds[UI_CSL_BTN_ENABLE_1 + idx*SETUI_CSL_ELEMS_PER_PATH],
                                              xpProperty_ButtonState,NULL)),
        txtCSLPaths[idx].GetDescriptor()
    };
    
    // save
    dataRefs.SaveCSLPath(idx, newPath);
}
