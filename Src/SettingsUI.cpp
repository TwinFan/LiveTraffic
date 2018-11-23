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

#include <regex>

//
// MARK: LTCapDateTime
//

// it's all about setting the caption to current sim time
LTCapDateTime::LTCapDateTime (XPWidgetID _me) :
TFTextFieldWidget(_me)
{}

void LTCapDateTime::SetCaption ()
{
    // set text of widget
    SetDescriptor(dataRefs.GetSimTimeString().c_str());
}

bool LTCapDateTime::TfwMsgMain1sTime ()
{
    TFTextFieldWidget::TfwMsgMain1sTime();
    if (!HaveKeyboardFocus())       // don't overwrite while use is editing
        SetCaption();
    return true;
}

// take care of my own text field having changed
bool LTCapDateTime::MsgTextFieldChanged (XPWidgetID textWidget, std::string text)
{
    bool bOK = false;

    if (textWidget != *this)
        return false;
    
    // interpret user input with this regex:
    // [YYYY-][M]M-[D]D [H]H:[M]M[:[S]S]
    enum { D_YMIN=1, D_Y, D_M, D_D, T_H, T_M, T_SCOL, T_S, DT_EXPECTED };
    std::regex re("^((\\d{4})-)?(\\d{1,2})-(\\d{1,2}) (\\d{1,2}):(\\d{1,2})(:(\\d{1,2}))?");
    std::smatch m;
    std::regex_search(text, m, re);
    size_t n = m.size();                // how many matches? expected: 9
    
    // matched
    if (n == DT_EXPECTED) {
        time_t t = time(NULL);
        struct tm tm;                   // now contains _current_ time, only use: current year
        gmtime_s(&tm, &t);

        int yyyy = tm.tm_year + 1900;
        if (m[D_Y].matched)
            yyyy = std::stoi(m[D_Y]);
        int mm = std::stoi(m[D_M]);
        int dd = std::stoi(m[D_D]);
        int HH = std::stoi(m[T_H]);
        int MM = std::stoi(m[T_M]);
        int SS = 0;
        if (m[T_S].matched)
            SS = std::stoi(m[T_S]);
        
        // verify valid values
        if (2000 <= yyyy && yyyy < 2999 &&
            1 <= mm && mm <= 12 &&
            1 <= dd && dd <= 31 &&
            0 <= HH && HH <= 23 &&
            0 <= MM && MM <= 59 &&
            0 <= SS && SS <= 59)
        {
            bOK = true;
            
            // send the date to ourselves via a dataRef
            if (simDate.isValid() || simDate.setDataRef(DATA_REFS_LT[DR_SIM_DATE]))
                simDate.Set(yyyy*10000 + mm*100 + dd);
            // send the time to ourselves via a dataRef
            if (simTime.isValid() || simTime.setDataRef(DATA_REFS_LT[DR_SIM_TIME]))
                simTime.Set(  HH*10000 + MM*100 + SS);
        }
    }

    // can't interpret input: keep keyboard focus in the field for the user to fix it
    if (!bOK)
        SetKeyboardFocus();

    return true;
}

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
    // "Basics" tab
    UI_BASICS_LIVE_SUB_WND,
    UI_BASICS_BTN_ENABLE,
    UI_BASICS_BTN_AUTO_START,
    UI_BASICS_CAP_FDCHANNELS,
    UI_BASICS_BTN_OPENSKY_LIVE,
    UI_BASICS_BTN_OPENSKY_MASTERDATA,
    UI_BASICS_BTN_ADSB_LIVE,

    UI_BASICS_CAP_VERSION_TXT,
    UI_BASICS_CAP_VERSION,

    UI_BASICS_HISTORIC_SUB_WND,
    UI_BASICS_BTN_HISTORIC,
    UI_BASICS_CAP_DATETIME,
    UI_BASICS_TXT_DATETIME,
    UI_BASICS_CAP_HISTORICCHANNELS,
    UI_BASICS_BTN_ADSB_HISTORIC,
    
    // "A/C Labels" tab
    UI_LABELS_SUB_WND,
    UI_LABELS_CAP_STATIC,
    UI_LABELS_BTN_TYPE,
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

    // "Advanced" tab
    UI_ADVCD_SUB_WND,
    UI_ADVCD_CAP_LOGLEVEL,
    UI_ADVCD_BTN_LOG_FATAL,
    UI_ADVCD_BTN_LOG_ERROR,
    UI_ADVCD_BTN_LOG_WARNING,
    UI_ADVCD_BTN_LOG_INFO,
    UI_ADVCD_BTN_LOG_DEBUG,
    UI_ADVCD_BTN_LOG_ACPOS,
    UI_ADVCD_CAP_FILTER,
    UI_ADVCD_TXT_FILTER,
    UI_ADVCD_BTN_LOG_MODELMATCH,
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
    
    // always last: number of UI elements
    UI_NUMBER_OF_ELEMENTS
};

// for ease of definition coordinates start at (0|0)
// window will be centered shortly before presenting it
TFWidgetCreate_t SETTINGS_UI[] =
{
    {   0,   0, 400, 330, 0, "LiveTraffic Settings", 1, NO_PARENT, xpWidgetClass_MainWindow, {xpProperty_MainWindowHasCloseBoxes, 1, xpProperty_MainWindowType,xpMainWindowStyle_Translucent,0,0} },
    // Buttons to select 'tabs'
    {  10,  30,  75,  10, 1, "Basics",               0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    {  85,  30,  75,  10, 1, "A/C Labels",           0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    { 160,  30,  75,  10, 1, "Advanced",             0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    // "Basics" tab
    {  10,  50, 190, -10, 0, "Basics Live",          0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {  10,  10,  10,  10, 1, "Show Live Aircrafts",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  25,  10,  10, 1, "Auto Start",           0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  50,  -5,  10, 1, "Flight Data Channels:",0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  70,  10,  10, 1, "OpenSky Network Live", 0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  85,  10,  10, 1, "OpenSky Network Master Data",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "ADS-B Exchange Live",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5, -15,  -5,  10, 1, "Version",              0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  50, -15,  -5,  10, 1, "",                     0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },

    { 200,  50, -10, -10, 0, "Basics Historic",      0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {  10,  10,  10,  10, 1, "Use Historic Data",    0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  30,  50,  10, 1, "Time:",                0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {-140,  30, 130,  15, 1, "",                     0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_TextField, {xpProperty_MaxCharacters,19, 0,0, 0,0} },
    {   5,  50, -10,  10, 1, "Historic Channels:",   0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10, 105,  10,  10, 1, "ADS-B Exchange Historic",  0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    // "A/C Label" tab
    {  10,  50, -10, -10, 0, "A/C Label",           0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10, 190,  10, 1, "Static info:",        0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "ICAO A/C Type Code",  0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  45,  10,  10, 1, "Transponder Hex Code",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  60,  10,  10, 1, "Registration",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  75,  10,  10, 1, "ICAO Operator Code",  0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  90,  10,  10, 1, "Call Sign",           0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "Flight Number (rare)",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 120,  10,  10, 1, "Route         (rare)",0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  10, -10,  10, 1, "Dynamic data:",       0, UI_LABELS_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 200,  30,  10,  10, 1, "Flight Phase",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  45,  10,  10, 1, "Heading",             0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  60,  10,  10, 1, "Altitude [ft]",       0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  75,  10,  10, 1, "Height AGL [ft]",     0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200,  90,  10,  10, 1, "Speed [kn]",          0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    { 200, 105,  10,  10, 1, "VSI [ft/min]",        0, UI_LABELS_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    // "Advanced" tab
    {  10,  50, -10, -10, 0, "Advanced",            0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10,  -5,  10, 1, "Logging Level:",      0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "Fatal",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  80,  30,  10,  10, 1, "Error",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 150,  30,  10,  10, 1, "Warning",             0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 220,  30,  10,  10, 1, "Info",                0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 290,  30,  10,  10, 1, "Debug",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  10,  50,  10,  10, 1, "Debug: Log a/c positions",  0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  70, 215,  10, 1, "Filter for transponder hex code",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220,  70,  70,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,8, 0,0, 0,0} },
    {  10,  90,  10,  10, 1, "Debug: Log model matching (XPlaneMP)",  0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5, 110, 215,  10, 1, "Max number of aircrafts",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 110,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 130, 215,  10, 1, "Max number of full a/c to draw",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 130,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 150, 215,  10, 1, "Max distance for drawing full a/c [km]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 150,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,2, 0,0, 0,0} },
    {   5, 170, 215,  10, 1, "Search distance [km]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 170,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 190, 215,  10, 1, "Live data refresh [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 190,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 210, 215,  10, 1, "Buffering period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 210,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 230, 215,  10, 1, "a/c outdated period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 220, 230,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },


};

const int NUM_WIDGETS = sizeof(SETTINGS_UI)/sizeof(SETTINGS_UI[0]);

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
        if (!TFUCreateWidgetsEx(SETTINGS_UI, NUM_WIDGETS, NULL, widgetIds))
        {
            SHOW_MSG(logERR,ERR_WIDGET_CREATE);
            return;
        }
        setId(widgetIds[0]);        // register in base class for message handling
        
        // some widgets with objects
        subBasicsLive.setId(widgetIds[UI_BASICS_LIVE_SUB_WND]);
        subBasicsHistoric.setId(widgetIds[UI_BASICS_HISTORIC_SUB_WND]);
        subAcLabel.setId(widgetIds[UI_LABELS_SUB_WND]);
        subAdvcd.setId(widgetIds[UI_ADVCD_SUB_WND]);
        
        // organise the tab button group
        tabGrp.Add({
            widgetIds[UI_BTN_BASICS],
            widgetIds[UI_BTN_AC_LABELS],
            widgetIds[UI_BTN_ADVANCED]});
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
        btnBasicsHistoric.setId(widgetIds[UI_BASICS_BTN_HISTORIC],
                              DATA_REFS_LT[DR_CFG_USE_HISTORIC_DATA]);
        btnOpenSkyLive.setId(widgetIds[UI_BASICS_BTN_OPENSKY_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_ONLINE]);
        btnOpenSkyMasterdata.setId(widgetIds[UI_BASICS_BTN_OPENSKY_MASTERDATA],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA]);
        btnADSBLive.setId(widgetIds[UI_BASICS_BTN_ADSB_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_ADSB_EXCHANGE_ONLINE]);
        btnADSBHistoric.setId(widgetIds[UI_BASICS_BTN_ADSB_HISTORIC],
                              DATA_REFS_LT[DR_CHANNEL_ADSB_EXCHANGE_HISTORIC]);

        // version number
        XPSetWidgetDescriptor(widgetIds[UI_BASICS_CAP_VERSION],
                              LT_VERSION_FULL);
        
        // Historic data timestamp
        txtDateTime.setId(widgetIds[UI_BASICS_TXT_DATETIME]);
        txtDateTime.SetCaption();
        
        // *** A/C Labels ***
        drCfgLabels.setDataRef(DATA_REFS_LT[DR_CFG_LABELS]);
        LabelBtnInit();
        
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
        
        // filter for transponder hex code
        txtAdvcdFilter.setId(widgetIds[UI_ADVCD_TXT_FILTER]);
        txtAdvcdFilter.SearchFlightData(dataRefs.GetDebugAcFilter());
        
        // link some buttons directly to dataRefs:
        btnAdvcdLogACPos.setId(widgetIds[UI_ADVCD_BTN_LOG_ACPOS],
                              DATA_REFS_LT[DR_DBG_AC_POS]);
        btnAdvcdLogModelMatch.setId(widgetIds[UI_ADVCD_BTN_LOG_MODELMATCH],
                                    DATA_REFS_LT[DR_DBG_MODEL_MATCHING]);
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
}

// capture entry into 'filter for transponder hex code' field
bool LTSettingsUI::MsgTextFieldChanged (XPWidgetID textWidget, std::string text)
{
    // not my key field?
    if (textWidget != txtAdvcdFilter.getId() )
        return TFMainWindowWidget::MsgTextFieldChanged(textWidget, text);

    // set the filter a/c if defined
    if (txtAdvcdFilter.HasTranspIcao())
        DataRefs::LTSetDebugAcFilter(NULL,txtAdvcdFilter.GetTranspIcaoInt());
    else
        DataRefs::LTSetDebugAcFilter(NULL,0);
    return true;
}


// writes current values out into config file
bool LTSettingsUI::MsgHidden (XPWidgetID hiddenWidget)
{
    if (hiddenWidget == *this)          // only if it was me who got hidden
        dataRefs.SaveConfigFile();
    // pass on in class hierarchy
    return TFMainWindowWidget::MsgHidden(hiddenWidget);
}

// update state of log-level buttons from dataRef every second
bool LTSettingsUI::TfwMsgMain1sTime ()
{
    TFMainWindowWidget::TfwMsgMain1sTime();
    logLevelGrp.SetCheckedIndex(dataRefs.GetLogLevel());
    return true;
}

// handles show/hide of 'tabs', values of logging level
bool LTSettingsUI::MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked)
{
    // first pass up the class hierarchy to make sure the button groups are handled correctly
    bool bRet = TFMainWindowWidget::MsgButtonStateChanged(buttonWidget, bNowChecked);
    
    // if the button is one of our tab buttons show/hide the appropriate subwindow
    if (widgetIds[UI_BTN_BASICS] == buttonWidget) {
        subBasicsLive.Show(bNowChecked);
        subBasicsHistoric.Show(bNowChecked);
        bRet = true;
    }
    else if (widgetIds[UI_BTN_AC_LABELS] == buttonWidget) {
        subAcLabel.Show(bNowChecked);
        bRet = true;
    }
    else if (widgetIds[UI_BTN_ADVANCED] == buttonWidget) {
        subAdvcd.Show(bNowChecked);
        bRet = true;
    }
    
    // if any of the a/c label check boxes changes we set the config accordingly
    if (widgetIds[UI_LABELS_BTN_TYPE]       == buttonWidget ||
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
        widgetIds[UI_LABELS_BTN_VSI]        == buttonWidget)
    {
        LabelBtnSave();
        bRet = true;
    }
    
    // if any of the log-level radio buttons changes we set log-level accordingly
    if (bNowChecked && logLevelGrp.isInGroup(buttonWidget))
    {
        dataRefs.SetLogLevel(logLevelGrp.GetCheckedIndex());
        bRet = true;
    }
    
    return bRet;
}

// Handle checkboxes for a/c labels
void LTSettingsUI::LabelBtnInit()
{
    // read current label configuration and init the checkboxes accordingly
    DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg().b;
    XPSetWidgetProperty(widgetIds[UI_LABELS_BTN_TYPE],xpProperty_ButtonState,cfg.bIcaoType);
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
}

void LTSettingsUI::LabelBtnSave()
{
    // store the checkboxes states in a zero-inited configuration
    DataRefs::LabelCfgUTy cfg = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cfg.b.bIcaoType     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TYPE],xpProperty_ButtonState,NULL);
    cfg.b.bTranspCode   = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TRANSP],xpProperty_ButtonState,NULL);
    cfg.b.bReg          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_REG],xpProperty_ButtonState,NULL);
    cfg.b.bIcaoOp       = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_OP],xpProperty_ButtonState,NULL);
    cfg.b.bCallSign     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_CALL_SIGN],xpProperty_ButtonState,NULL);
    cfg.b.bFlightNo     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_FLIGHT_NO],xpProperty_ButtonState,NULL);
    cfg.b.bRoute        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ROUTE],xpProperty_ButtonState,NULL);
    cfg.b.bPhase        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_PHASE],xpProperty_ButtonState,NULL);
    cfg.b.bHeading      = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEADING],xpProperty_ButtonState,NULL);
    cfg.b.bAlt          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ALT],xpProperty_ButtonState,NULL);
    cfg.b.bHeightAGL    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEIGHT],xpProperty_ButtonState,NULL);
    cfg.b.bSpeed        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_SPEED],xpProperty_ButtonState,NULL);
    cfg.b.bVSI          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_VSI],xpProperty_ButtonState,NULL);
    // save as current config
    drCfgLabels.Set(cfg.i);
}
