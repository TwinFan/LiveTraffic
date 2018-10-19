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
    // current sim time
    time_t t = time_t(dataRefs.GetSimTime());
    struct tm tm = *gmtime(&t);
    
    // format it nicely
    char szBuf[50];
    strftime(szBuf,
             sizeof(szBuf) - 1,
             "%F %T",
             &tm);

    // set text of widget
    SetDescriptor(szBuf);
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
    
    // interpret user input with this regex:
    // [YYYY-][M]M-[D]D [H]H:[M]M[:[S]S]
    enum { D_YMIN=1, D_Y, D_M, D_D, T_H, T_M, T_SCOL, T_S, DT_EXPECTED };
    std::regex re("^((\\d{4})-)?(\\d{1,2})-(\\d{1,2}) (\\d{1,2}):(\\d{1,2})(:(\\d{1,2}))?");
    std::smatch m;
    std::regex_search(text, m, re);
    long n = m.size();                  // how many matches? expected: 9
    
    // matched
    if (n == DT_EXPECTED) {
        time_t t = time(NULL);
        struct tm tm = *gmtime(&t);     // now contains _current_ time, only use: current year
        
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
    UI_BTN_DEBUG,
    // "Basics" tab
    UI_BASICS_LIVE_SUB_WND,
    UI_BASICS_BTN_ENABLE,
    UI_BASICS_CAP_FDCHANNELS,
    UI_BASICS_BTN_FR24_LIVE,
    UI_BASICS_BTN_OPENSKY_LIVE,
    UI_BASICS_BTN_OPENSKY_MASTERDATA,
    UI_BASICS_BTN_ADSB_LIVE,

    UI_BASICS_HISTORIC_SUB_WND,
    UI_BASICS_BTN_HISTORIC,
    UI_BASICS_CAP_DATETIME,
    UI_BASICS_TXT_DATETIME,
    UI_BASICS_CAP_HISTORICCHANNELS,
    UI_BASICS_BTN_ADSB_HISTORIC,
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
    {   0,   0, 400, 300, 0, "LiveTraffic Settings", 1, NO_PARENT, xpWidgetClass_MainWindow, {xpProperty_MainWindowHasCloseBoxes, 1, xpProperty_MainWindowType,xpMainWindowStyle_Translucent,0,0} },
    // Buttons to select 'tabs'
    {  10,  30,  75,  10, 1, "Basics",               0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    {  85,  30,  75,  10, 1, "Advanced",             0, UI_MAIN_WND, xpWidgetClass_Button, {xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0, 0,0} },
    // "Basics" tab
    {  10,  50, 190, -10, 0, "Basics Live",          0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {  10,  10,  10,  10, 1, "Show Live Aircrafts",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  50,  -5,  10, 1, "Flight Data Channels:",0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  70,  10,  10, 1, "Flightradar24 Live",   0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10,  90,  10,  10, 1, "OpenSky Network Live", 0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 105,  10,  10, 1, "OpenSky Network Master Data",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {  10, 125,  10,  10, 1, "ADS-B Exchange Live",  0, UI_BASICS_LIVE_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },

    { 200,  50, -10, -10, 0, "Basics Historic",      0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0, 0,0, 0,0} },
    {  10,  10,  10,  10, 1, "Use Historic Data",    0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  30,  50,  10, 1, "Time:",                0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {-140,  30, 130,  15, 1, "",                     0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_TextField, {xpProperty_MaxCharacters,19, 0,0, 0,0} },
    {   5,  50, -10,  10, 1, "Historic Channels:",   0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10, 125,  10,  10, 1, "ADS-B Exchange Historic",  0, UI_BASICS_HISTORIC_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    // "Advanced" tab
    {  10,  50, -10, -10, 0, "Advanced",            0, UI_MAIN_WND, xpWidgetClass_SubWindow, {0,0,0,0,0,0} },
    {   5,  10,  -5,  10, 1, "Logging Level:",      0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    {  10,  30,  10,  10, 1, "Fatal",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  80,  30,  10,  10, 1, "Error",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 150,  30,  10,  10, 1, "Warning",             0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 220,  30,  10,  10, 1, "Info",                0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    { 290,  30,  10,  10, 1, "Debug",               0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton, 0,0} },
    {  10,  50,  10,  10, 1, "Debug: Log a/c positions",  0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5,  70, 180,  10, 1, "Filter for transponder hex code",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185,  70,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,6, 0,0, 0,0} },
    {  10,  90,  10,  10, 1, "Debug: Log model matching (XPlaneMP)",  0, UI_ADVCD_SUB_WND, xpWidgetClass_Button, {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox, 0,0} },
    {   5, 110, 180,  10, 1, "Max number of aircrafts",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185, 110,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 130, 180,  10, 1, "Search distance [km]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185, 130,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 150, 180,  10, 1, "Live data refresh [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185, 150,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 170, 180,  10, 1, "Buffering period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185, 170,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },
    {   5, 190, 180,  10, 1, "a/c outdated period [s]",   0, UI_ADVCD_SUB_WND, xpWidgetClass_Caption, {0,0, 0,0, 0,0} },
    { 185, 190,  50,  15, 1, "",                    0, UI_ADVCD_SUB_WND, xpWidgetClass_TextField,{xpProperty_MaxCharacters,3, 0,0, 0,0} },


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
        subAdvcd.setId(widgetIds[UI_ADVCD_SUB_WND]);
        
        // organise the tab button group
        tabGrp.Add({widgetIds[UI_BTN_BASICS], widgetIds[UI_BTN_DEBUG]});
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
        btnBasicsHistoric.setId(widgetIds[UI_BASICS_BTN_HISTORIC],
                              DATA_REFS_LT[DR_CFG_USE_HISTORIC_DATA]);
        btnFR24Live.setId(widgetIds[UI_BASICS_BTN_FR24_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_FLIGHTRADAR24_ONLINE]);
        btnOpenSkyLive.setId(widgetIds[UI_BASICS_BTN_OPENSKY_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_ONLINE]);
        btnOpenSkyMasterdata.setId(widgetIds[UI_BASICS_BTN_OPENSKY_MASTERDATA],
                              DATA_REFS_LT[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA]);
        btnADSBLive.setId(widgetIds[UI_BASICS_BTN_ADSB_LIVE],
                              DATA_REFS_LT[DR_CHANNEL_ADSB_EXCHANGE_ONLINE]);
        btnADSBHistoric.setId(widgetIds[UI_BASICS_BTN_ADSB_HISTORIC],
                              DATA_REFS_LT[DR_CHANNEL_ADSB_EXCHANGE_HISTORIC]);

        txtDateTime.setId(widgetIds[UI_BASICS_TXT_DATETIME]);
        txtDateTime.SetCaption();
        
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
        // FIXME: field not yet linked to dataRefs.uDebugAcFilter
        txtAdvcdFilter.setId(widgetIds[UI_ADVCD_TXT_FILTER]);
        txtAdvcdFilter.tfFormat = TFTextFieldWidget::TFF_HEX;
        
        // link some buttons directly to dataRefs:
        btnAdvcdLogACPos.setId(widgetIds[UI_ADVCD_BTN_LOG_ACPOS],
                              DATA_REFS_LT[DR_DBG_AC_POS]);
        btnAdvcdLogModelMatch.setId(widgetIds[UI_ADVCD_BTN_LOG_MODELMATCH],
                                    DATA_REFS_LT[DR_DBG_MODEL_MATCHING]);
        intMaxNumAc.setId(widgetIds[UI_ADVCD_INT_MAX_NUM_AC],
                          DATA_REFS_LT[DR_CFG_MAX_NUM_AC]);
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
    else if (widgetIds[UI_BTN_DEBUG] == buttonWidget) {
        subAdvcd.Show(bNowChecked);
        bRet = true;
    }
    
    // if any of the log-level radio buttons changes we set log-level accordingly
    if (bNowChecked &&
        (widgetIds[UI_ADVCD_BTN_LOG_DEBUG]   == buttonWidget ||
         widgetIds[UI_ADVCD_BTN_LOG_INFO]    == buttonWidget ||
         widgetIds[UI_ADVCD_BTN_LOG_WARNING] == buttonWidget ||
         widgetIds[UI_ADVCD_BTN_LOG_ERROR]   == buttonWidget ||
         widgetIds[UI_ADVCD_BTN_LOG_FATAL]   == buttonWidget))
    {
        dataRefs.SetLogLevel(logLevelGrp.GetCheckedIndex());
        bRet = true;
    }
    
    return bRet;
}
