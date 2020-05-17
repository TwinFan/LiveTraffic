/// @file       ACInfoWnd.cpp
/// @brief      Aircraft information window showing details for a selected aircraft
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

//
//MARK: Window Structure
//

// indexes into the below definition array, must be kept in synch with the same
enum ACI_WIDGET_IDX_T {
    ACI_MAIN_WND     = 0,
    // key / search: input/output
    ACI_CAP_AC_KEY,
    ACI_TXT_AC_KEY,
    ACI_BTN_AUTO,
    ACI_CAP_AUTO,
    // static data
    ACI_CAP_REG,
    ACI_TXT_REG,
    ACI_CAP_ICAO_CLASS,
    ACI_TXT_ICAO,
    ACI_TXT_CLASS,
    ACI_CAP_MANU,
    ACI_TXT_MANU,
    ACI_CAP_MODEL,
    ACI_TXT_MODEL,
    ACI_CAP_OP,
    ACI_TXT_OP,
    ACI_CAP_DISPLAYED_USING,
    ACI_TXT_DISPLAYED_USING,

    ACI_CAP_CALLSIGN_SQUAWK,
    ACI_TXT_CALLSIGN,
    ACI_TXT_SQUAWK,
    ACI_CAP_FLIGHT_ROUTE,
    ACI_TXT_FLIGHT_ROUTE,

    ACI_CAP_SIM_TIME,
    ACI_TXT_SIM_TIME,
    ACI_CAP_LAST_DATA_CHNL,
    ACI_TXT_LAST_DATA,
    ACI_TXT_CHNL,

    // dynamic data
    ACI_CAP_POS,
    ACI_TXT_POS,
    ACI_CAP_BEARING_DIST,
    ACI_TXT_BEARING,
    ACI_TXT_DIST,
    ACI_CAP_PHASE,
    ACI_TXT_PHASE,
    ACI_CAP_GEAR_FLAPS,
    ACI_TXT_GEAR,
    ACI_TXT_FLAPS,
    ACI_CAP_LIGHTS,
    ACI_TXT_LIGHTS,
    ACI_CAP_HEADING,
    ACI_TXT_HEADING,
    ACI_CAP_PITCH_ROLL,
    ACI_TXT_PITCH,
    ACI_TXT_ROLL,

    ACI_CAP_ALT_AGL,
    ACI_TXT_ALT,
    ACI_TXT_AGL,
    ACI_CAP_SPEED_VSI,
    ACI_TXT_SPEED,
    ACI_TXT_VSI,
    
    ACI_BTN_CAMERA_VIEW,
    ACI_CAP_CAMERA_VIEW,
    ACI_BTN_VISIBLE,
    ACI_CAP_VISIBLE,
    ACI_BTN_AUTO_VISIBLE,
    ACI_CAP_AUTO_VISIBLE,
    ACI_BTN_HELP,

    // always last: number of UI elements
    ACI_NUMBER_OF_ELEMENTS
};

// for ease of definition coordinates start at (0|0)
// window will be centered shortly before presenting it
TFWidgetCreate_t ACI_WND[] =
{
    {   0,   0, 270, 365, 1, "LiveTraffic A/C Info", 1, NO_PARENT, xpWidgetClass_MainWindow, {xpProperty_MainWindowHasCloseBoxes, 1, xpProperty_MainWindowType,xpMainWindowStyle_Translucent,0,0} },
    {   5,  20,  95,  10, 1, "A/C key",             0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  20,  70,  15, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_TextField,{xpProperty_TextFieldType,xpTextTranslucent, xpProperty_MaxCharacters,8, 0,0} },
    { 210,  22,  10,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    { 220,  20,  55,  10, 1, "AUTO",                0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  35,  95,  10, 1, "Registr.",            0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  35,  70,  15, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  50,  95,  10, 1, "ICAO Type | Class.",  0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  50,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195,  50,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  65,  95,  10, 1, "Manufacturer",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  65, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  80,  95,  10, 1, "Model",               0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  80, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  95,  95,  10, 1, "Operator",            0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  95, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 110,  95,  10, 1, "CSL Model",           0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 110, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 130,  95,  10, 1, "Call Sign | Squawk",  0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 130,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 130,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 145,  95,  10, 1, "Flight: Route",       0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 145, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 165,  95,  10, 1, "Simulated Time",      0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 165, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 180,  95,  10, 1, "Last Data [s] | Chnl",0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 180,  40,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 165, 180, 100,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 200,  95,  10, 1, "Position",            0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 200, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 215,  95,  10, 1, "Bearing | Dist. [nm]",0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 215,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 215,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 230,  95,  10, 1, "Flight Phase",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 230, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 245,  95,  10, 1, "Gear | Flaps",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 245,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 245,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 260,  95,  10, 1, "Lights",              0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 260, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 280,  95,  10, 1, "Heading [°]",         0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 280,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 295,  95,  10, 1, "Pitch | Roll [°]",    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 295,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 295,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 315,  95,  10, 1, "Altitude | AGL [ft]", 0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 315,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 315,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 330,  95,  10, 1, "Speed [kn] | VSI [ft]", 0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 330,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 330,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    
    {  10, 350,  10,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    {  20, 347,  55,  10, 1, "Camera",              0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {  80, 350,  10,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    {  90, 347,  55,  10, 1, "Visible",             0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 145, 350,  10,  10, 0, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    { 155, 347,  55,  10, 0, "Auto Visible",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 240, 350,  30,  10, 1, "?",                   0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonBehavior, xpButtonBehaviorPushButton,  0,0, 0,0} },
};

const int NUM_WIDGETS = sizeof(ACI_WND)/sizeof(ACI_WND[0]);

static_assert(ACI_NUMBER_OF_ELEMENTS == NUM_WIDGETS,
              "ACI_WIDGET_IDX_T and ACI_WND[] differ in number of elements!");

//
// MARK: TFACSearchEditWidget
//

TFACSearchEditWidget::TFACSearchEditWidget (XPWidgetID _me,
                                            const char* szKey) :
TFTextFieldWidget(_me)
{
    // force upper case
    tfFormat = TFF_UPPER_CASE;
    
    // if there is something to search for...do so
    if (szKey)
        SearchFlightData(szKey);
}

// Use the provided key to seach the list of aircraft if something matches:
// - icao transponder code
// - registration
// - call sign
// - flight number
const LTFlightData* TFACSearchEditWidget::SearchFlightData (std::string ac_key)
{
    mapLTFlightDataTy::const_iterator fdIter = mapFd.cend();
    
    trim(ac_key);
    if (!ac_key.empty()) {
        // is it a small integer number, i.e. used as index?
        if (ac_key.length() <= 3 &&
            ac_key.find_first_not_of("0123456789") == std::string::npos)
        {
            int i = std::stoi(ac_key);
            // let's find the i-th aircraft by looping over all flight data
            // and count those objects, which have an a/c
            if (i > 0) for (fdIter = mapFd.cbegin();
                 fdIter != mapFd.cend();
                 ++fdIter)
            {
                if (fdIter->second.hasAc())         // has an a/c
                    if ( --i == 0 )                 // and it's the i-th!
                        break;
            }
        }
        else
        {
            // search the map of flight data by text key
            fdIter =
            std::find_if(mapFd.cbegin(), mapFd.cend(),
                         [&ac_key](const mapLTFlightDataTy::value_type& mfd)
                         { return mfd.second.IsMatch(ac_key); }
                         );
        }
    }
    
    // found?
    if (fdIter != mapFd.cend()) {
        SetAcKey(fdIter->second.key());
        // return the result
        return &fdIter->second;
    }
    
    // not found
    acKey.clear();
    return nullptr;
}

void TFACSearchEditWidget::SetAcKey (const LTFlightData::FDKeyTy& _key)
{
    // remember key
    acKey = _key;
    // replace my own content with the new key (hex code)
    SetDescriptor(oldDescriptor = acKey);
}

// Get my defined aircraft
// As aircraft can be removed any frame this needs to be called
// over and over again.
// Note: Deleting aircraft happens in a flight loop callback,
//       which is the same thread as this here is running in.
//       So we can safely assume the returned pointer is valid until
//       we return. But not any longer.
LTFlightData* TFACSearchEditWidget::GetFlightData () const
{
    // short-cut if there's no key
    if (acKey.empty())
        return nullptr;
    
    // find the flight data by key
    mapLTFlightDataTy::iterator fdIter = mapFd.find(acKey);
    // return flight data if found
    return fdIter != mapFd.end() ? &fdIter->second : nullptr;

}

// even if FlightData exists, a/c might still be NULL if not yet created!
LTAircraft* TFACSearchEditWidget::GetAircraft () const
{
    LTFlightData* pFD = GetFlightData();
    return pFD ? pFD->GetAircraft() : nullptr;
}

// capture entry into myself -> trigger search for aircraft
bool TFACSearchEditWidget::MsgTextFieldChanged (XPWidgetID textWidget,
                                                std::string text)
{
    // if this is not about me then don't handle...ask the class hierarchy
    if (textWidget != *this)
        return TFTextFieldWidget::MsgTextFieldChanged(textWidget, text);
    
    // it was me...so start a search with what is in my edit field
    if (text == INFO_WND_AUTO_AC)
        acKey.clear();              // AUTO -> handled by main window
    else
        SearchFlightData(text);     // otherwise search for the a/c
    
    // return 'false' on purpose: message also needs to be sent to main window
    return false;
}

// if we lost focus due to the key:
// regain focus if we don't have a valid a/c,
// so that user can try again
bool TFACSearchEditWidget::MsgKeyPress (XPKeyState_t& key)
{
    // what's currently entered?
    const std::string descr(GetDescriptor());
    // normal handling
    bool b = TFTextFieldWidget::MsgKeyPress(key);
    // hit Enter, no focus any longer, but also no a/c and no AUTO mode?
    if ((key.flags & xplm_DownFlag) &&      // 'key down' flag
        (key.key == XPLM_KEY_RETURN) &&     // key is 'return'
        !HaveKeyboardFocus() && !GetFlightData() &&
        descr != INFO_WND_AUTO_AC) {
        // regain focus and give user another chance
        SetKeyboardFocus();
        SelectAll();
        return true;
    }
    return b;
}

//
// MARK: ACIWnd
//

// we keep a list of all created windows
std::forward_list<ACIWnd*> listACIWnd;

// Constructor fully initializes and display the window
ACIWnd::ACIWnd(TFWndMode wndMode, const char* szKey) :
widgetIds(nullptr)
{
    // array, which receives ids of all created widgets
    widgetIds = new XPWidgetID[NUM_WIDGETS];
    LOG_ASSERT(widgetIds);
    memset(widgetIds, 0, sizeof(XPWidgetID)*NUM_WIDGETS );
    
    // create all widgets, i.e. the entire window structure
    if (!TFUCreateWidgetsEx(ACI_WND, NUM_WIDGETS, NULL, widgetIds, wndMode))
    {
        SHOW_MSG(logERR,ERR_WIDGET_CREATE);
        delete widgetIds;       // indicates: not initialized
        widgetIds = nullptr;
        return;
    }
    
    // register myself in base class for message handling
    setId(widgetIds[0]);
    
    // add myself to the list of windows
    listACIWnd.push_front(this);
    
    // text field for a/c key entry, upper case only
    txtAcKey.setId(widgetIds[ACI_TXT_AC_KEY]);
    // button for AUTO mode
    btnAuto.setId(widgetIds[ACI_BTN_AUTO]);

    // setting the initial key (a/c or AUTO)
    if (szKey) {
        // running in auto a/c mode?
        if (strncmp(szKey, INFO_WND_AUTO_AC, sizeof(INFO_WND_AUTO_AC)) == 0)
        {
            btnAuto = true;
            SetDescriptor(GetDescriptor() + " (" INFO_WND_AUTO_AC ")");
            szKey = nullptr;
        }
        else {
            txtAcKey.SearchFlightData(szKey);
            btnAuto = false;
        }
    }
    
    // value fields
    valSquawk.setId(widgetIds[ACI_TXT_SQUAWK]);
    valPos.setId(widgetIds[ACI_TXT_POS]);
    valBearing.setId(widgetIds[ACI_TXT_BEARING]);
    valDist.setId(widgetIds[ACI_TXT_DIST]);
    valPhase.setId(widgetIds[ACI_TXT_PHASE]);
    valGear.setId(widgetIds[ACI_TXT_GEAR]);
    valFlaps.setId(widgetIds[ACI_TXT_FLAPS]);
    valLights.setId(widgetIds[ACI_TXT_LIGHTS]);
    valHeading.setId(widgetIds[ACI_TXT_HEADING]);
    valPitch.setId(widgetIds[ACI_TXT_PITCH]);
    valRoll.setId(widgetIds[ACI_TXT_ROLL]);
    valAlt.setId(widgetIds[ACI_TXT_ALT]);
    valAGL.setId(widgetIds[ACI_TXT_AGL]);
    valSpeed.setId(widgetIds[ACI_TXT_SPEED]);
    valVSI.setId(widgetIds[ACI_TXT_VSI]);
    
    // buttons for camera view and visibility
    btnCamera.setId(widgetIds[ACI_BTN_CAMERA_VIEW]);
    btnVisible.setId(widgetIds[ACI_BTN_VISIBLE]);
    btnAutoVisible.setId(widgetIds[ACI_BTN_AUTO_VISIBLE]);
    capAutoVisible.setId(widgetIds[ACI_CAP_AUTO_VISIBLE]);
    btnAutoVisible.Show(dataRefs.IsAutoHidingActive());    // show button only if visibility is restricted
    capAutoVisible.Show(dataRefs.IsAutoHidingActive());

    // center the UI
    Center();

    // find the focus a/c if in AUTO mode
    if (btnAuto)
        UpdateFocusAc();
    
    // Have no actual aircraft? Give user chance to enter something
    if (!txtAcKey.GetFlightData()) {
        txtAcKey.SetKeyboardFocus();
        txtAcKey.SelectAll();
    }
    else {
        // otherwise start displaying the a/c's data
        UpdateStatValues();
        UpdateDynValues();
    }
}

// destructor frees resources
ACIWnd::~ACIWnd()
{
    if (isEnabled()) {
        // remove widgets and free memory
        Show(false);
        XPDestroyWidget(*widgetIds, 1);
        delete widgetIds;
        widgetIds = nullptr;
    }
    
    // remove myself from the list of windows
    listACIWnd.remove(this);
}

// static function: creates a new window
ACIWnd* ACIWnd::OpenNewWnd (TFWndMode wndMode, const char* szIcao)
{
    // creation of windows only makes sense if windows are shown
    if (!AreShown())
        ToggleHideShowAll();
    
    // now create the new window
    ACIWnd* pWnd = new ACIWnd(wndMode, szIcao);
    if (pWnd && !pWnd->isEnabled()) {       // did not init successfully
        delete pWnd;
        pWnd = nullptr;
    }
    return pWnd;
}

//
// MARK: Static functions on all windows
//

bool ACIWnd::bAreShown = true;

// move all windows into/out of VR
void ACIWnd::MoveAllVR (bool bIntoVR)
{
    // move into VR
    if (bIntoVR) {
        for (ACIWnd* pWnd: listACIWnd) {
            if (pWnd->GetWndMode() == TF_MODE_FLOAT)
                pWnd->SetWindowPositioningMode(xplm_WindowVR, -1);
        }
    }
    // move out of VR
    else {
        int moveOfs = 0;
        for (ACIWnd* pWnd: listACIWnd) {
            if (pWnd->GetWndMode() == TF_MODE_VR) {
                pWnd->SetWindowPositioningMode(xplm_WindowPositionFree, -1);
                pWnd->Center();
                // to avoid all windows being stacked we move them by a few pixels
                pWnd->MoveBy(moveOfs, -moveOfs);
                moveOfs += 20;
            }
        }
    }
}

// show/hide all windows
bool ACIWnd::ToggleHideShowAll()
{
    // Toggle
    bAreShown = !bAreShown;
    
    // now apply that new state to all windows
    for (ACIWnd* pWnd: listACIWnd)
        pWnd->Show(bAreShown);
    
    // return new state
    return bAreShown;
}

// close all windows
void ACIWnd::CloseAll()
{
    // we don't close us when in VR camera view
    if (dataRefs.IsVREnabled() && LTAircraft::IsCameraViewOn())
        return;
    
    // keep closing the first window until map empty
    while (!listACIWnd.empty()) {
        ACIWnd* pWnd = *listACIWnd.begin();
        delete pWnd;                        // destructor removes from list
    }
}

//
// MARK: Message handlers
//

// capture entry into the a/c key field
bool ACIWnd::MsgTextFieldChanged (XPWidgetID textWidget, std::string text)
{
    // not my key field?
    if (txtAcKey != textWidget)
        return TFMainWindowWidget::MsgTextFieldChanged(textWidget, text);
    
    // my key changed!
    btnAuto = (text == INFO_WND_AUTO_AC);   // auto switch a/c?
    if (btnAuto) {
        txtAcKey.SetDescriptor("");         // remove text AUTO
        if (!UpdateFocusAc())               // try finding an aircraft
            UpdateDynValues();              // not found...but at least update window title
    } else {
        UpdateStatValues();
        UpdateDynValues();
    }
    
    // msg handled
    return true;
}

// handles visibility buttons
bool ACIWnd::MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked)
{
    // click of the AUTO button -> maybe update selected a/c, but at least window title
    if (btnAuto == buttonWidget) {
        if (!UpdateFocusAc())
            UpdateDynValues();
    }
    
    LTAircraft* pAc = txtAcKey.GetAircraft();
    if (pAc)
    {
        if (btnCamera == buttonWidget) {
            // Call a/c camera view
            pAc->ToggleCameraView();
            btnCamera = pAc->IsInCameraView();
            
            // in VR view we need to move at least the current window again into VR
            // With switching to camera view the current position changed completely,
            // while the a/c info windows remain in cockpit.
            // Idea is: We move the window briefly back to normal view,
            //          just to move it right away into VR again, where it is
            //          supposed to show up.
            if (dataRefs.IsVREnabled()) {
                SetWindowPositioningMode(xplm_WindowPositionFree, -1);
                SetWindowPositioningMode(xplm_WindowVR, -1);
            }
            
            return true;
        }
        else if (btnVisible == buttonWidget) {
            // visibility set directly, auto-visibility will be off then
            pAc->SetVisible(bNowChecked);
            btnAutoVisible = pAc->IsAutoVisible();
            return true;
        }
        else if (btnAutoVisible == buttonWidget) {
            // auto-visibility changed...returns current a/c visibiliy
            btnVisible = pAc->SetAutoVisible(bNowChecked);
            return true;
        }
    }

    // pass on in class hierarchy
    return TFMainWindowWidget::MsgButtonStateChanged(buttonWidget, bNowChecked);
}

// push button pressed (or press button pushed?)
bool ACIWnd::MsgPushButtonPressed (XPWidgetID buttonWidget)
{
    // *** Help ***
    if (widgetIds[ACI_BTN_HELP] == buttonWidget)
    {
        // open help for a/c info wnd
        LTOpenHelp(HELP_AC_INFO_WND);
        return true;
    }

    // we don't know that button...
    return TFMainWindowWidget::MsgPushButtonPressed(buttonWidget);
}

// triggered every second to update values in the window
bool ACIWnd::TfwMsgMain1sTime ()
{
    // are we visible at all?
    if (!isVisible()) {
        // this happens to popped out windows...when the outer OS window is
        // closed then the widget doesn't receive a message,
        // so we notice the closing only now -> remove ourselves
        MessageCloseButtonPushed();
        return true;
    }
    
    // normal processing
    TFMainWindowWidget::TfwMsgMain1sTime();
    
    if (!UpdateFocusAc())               // changed focus a/c? If not:
        UpdateDynValues();              // update our values
    return true;
}

// remove myself completely
bool ACIWnd::MessageCloseButtonPushed ()
{
    // ignore the closing command if in VR _and_ running the external camera
    if (dataRefs.IsVREnabled() && btnCamera)
        return true;

    // else: remove myself
    delete this;
    return true;
}

//
// MARK: Update myself
//

// switch to another focus a/c?
bool ACIWnd::UpdateFocusAc ()
{
    if (!btnAuto) return false;
    
    // find the current focus a/c and if different from current one then switch
    const LTFlightData* pFocusAc = LTFlightData::FindFocusAc(DataRefs::GetViewHeading());
    if (pFocusAc && pFocusAc->key() != txtAcKey.GetAcKey()) {
        txtAcKey.SetAcKey(pFocusAc->key());
        UpdateStatValues();
        UpdateDynValues();
        if (txtAcKey.HaveKeyboardFocus())
            txtAcKey.LoseKeyboardFocus();
        return true;
    }
    
    // nothing found?
    if (!pFocusAc) {
        // Clear static values - did we actually clear anything?
        if (ClearStaticValues()) {
            // only then (i.e. the first time we really remove text)
            // also remove the key (otherwise the user might have started entering a new key already,
            // which we don't want to clear every second)
            txtAcKey.SetAcKey(LTFlightData::FDKeyTy());
        }
        
        // start at least the timer for regular focus a/c updates
        StartStopTimerMessages(true);
    }
    return false;
}

// clear static fields
bool ACIWnd::ClearStaticValues() {
    bool bRet = false;
    
    // loop all static fields
    for (int i: {
        ACI_TXT_REG, ACI_TXT_ICAO, ACI_TXT_CLASS, ACI_TXT_MANU, ACI_TXT_MODEL,
        ACI_TXT_OP, ACI_TXT_CALLSIGN, ACI_TXT_FLIGHT_ROUTE
    })
    {
        // is there anything to clear away?
        if (XPGetWidgetDescriptor(widgetIds[i], NULL, 0) > 0) {
            bRet = true;
            XPSetWidgetDescriptor(widgetIds[i], "");
        }
    }
    return bRet;
}

// Update all values in the window.
// (Note: We can't use the dataRef trick as the dataRef object doesn,
//        because we support several windows in parallel, but the dataRef object
//        just one.)
void ACIWnd::UpdateStatValues()
{
    const LTFlightData* pFD = txtAcKey.GetFlightData();
    
    // have selected flight data?
    if (pFD) {
        // get good copies (thread safe)
        const LTFlightData::FDStaticData stat (pFD->WaitForSafeCopyStat());
        
        // set static values (we consider the callsign static...)
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_REG], stat.reg.c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_ICAO],
                              stat.acTypeIcao.empty() ? "?" : stat.acTypeIcao.c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_CLASS],
                              stat.pDoc8643 ?
                              stat.pDoc8643->classification.c_str() : "-");
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_MANU], strAtMost(stat.man,  25).c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_MODEL], strAtMost(stat.mdl, 25).c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_OP], strAtMost((stat.opIcao.empty() ? stat.op : stat.opIcao + ": " + stat.op),25).c_str());

        XPSetWidgetDescriptor(widgetIds[ACI_TXT_CALLSIGN], stat.call.c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_FLIGHT_ROUTE], stat.flightRoute().c_str());
        
        // start the timer for regular dyn data updates
        StartStopTimerMessages(true);
        
        // set as 'selected' aircraft for debug output
        dataRefs.LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)),
                            txtAcKey.GetAcKeyNum());
    } else {
        // clear static values
        ClearStaticValues();

        // stop the timer for regular dyn data updates
        StartStopTimerMessages(false);

        // clear 'selected' aircraft for debug output
        dataRefs.LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)), 0);
    }
}

void ACIWnd::UpdateDynValues()
{
    // window title
    std::string title(ACI_WND[ACI_MAIN_WND].descriptor);
    
    // need a valid pointer
    const LTAircraft* pAc = txtAcKey.GetAircraft();

    if (pAc) {
        char szBuf[20];

        // _last_ dyn data object
        const LTFlightData& fd = pAc->fd;
        const LTFlightData::FDDynamicData dyn (fd.WaitForSafeCopyDyn(false));

        // title is dynmic
        title = strAtMost(fd.ComposeLabel(), 25);
        
        // update all field values
        const positionTy& pos = pAc->GetPPos();
        double ts = dataRefs.GetSimTime();
        valSquawk.SetDescriptor(dyn.GetSquawk());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_DISPLAYED_USING],
                              strAtMost(pAc->GetModelName(),25).c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_SIM_TIME], ts2string(time_t(ts)).c_str());
        // last update, relative to youngest timestamp for this plane
        ts -= fd.GetYoungestTS();
        ts *= -1;
        if (-10000 <= ts && ts <= 10000)
        {
            snprintf(szBuf,sizeof(szBuf),"%+.1f", ts);
            XPSetWidgetDescriptor(widgetIds[ACI_TXT_LAST_DATA], szBuf);
        }
        else
            XPSetWidgetDescriptor(widgetIds[ACI_TXT_LAST_DATA], "~");
        
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_CHNL],
                              dyn.pChannel ? strAtMost(dyn.pChannel->ChName(), 15).c_str() : "");
        valPos.SetDescriptor(pos);
        valBearing.SetDescriptor(pAc->GetVecView().angle);
        valDist.SetDescriptor(pAc->GetVecView().dist/M_per_NM, 1);
        valPhase.SetDescriptor(pAc->GetFlightPhaseString());
        valGear.SetDescriptor(pAc->GetGearPos(), 1);
        valFlaps.SetDescriptor(pAc->GetFlapsPos(), 1);
        valLights.SetDescriptor(pAc->GetLightsStr());
        snprintf(szBuf,sizeof(szBuf),"%03.f", pos.heading());   // heading with 3 digits (leading zeros)
        valHeading.SetDescriptor(szBuf);
        valPitch.SetDescriptor(pAc->GetPitch());
        valRoll.SetDescriptor(pAc->GetRoll());
        valAlt.SetDescriptor(round(pos.alt_ft()));
        if (pos.IsOnGnd())
            valAGL.SetDescriptor("On Grnd");
        else
            valAGL.SetDescriptor(pAc->GetPHeight_ft());
        valSpeed.SetDescriptor(pAc->GetSpeed_kt());
        valVSI.SetDescriptor(pAc->GetVSI_ft());
        
        // visibility buttons
        btnCamera = pAc->IsInCameraView();
        btnVisible = pAc->IsVisible();
        btnAutoVisible = pAc->IsAutoVisible();
        btnAutoVisible.Show(dataRefs.IsAutoHidingActive());    // show button only if visibility is restricted
        capAutoVisible.Show(dataRefs.IsAutoHidingActive());

    } else {
        // no current a/c
        // clear all values
        for (int i: {
            ACI_TXT_SQUAWK, ACI_TXT_DISPLAYED_USING, ACI_TXT_SIM_TIME,
            ACI_TXT_LAST_DATA, ACI_TXT_CHNL, ACI_TXT_POS, ACI_TXT_BEARING,
            ACI_TXT_DIST, ACI_TXT_PHASE, ACI_TXT_GEAR, ACI_TXT_FLAPS,
            ACI_TXT_LIGHTS, ACI_TXT_HEADING, ACI_TXT_PITCH, ACI_TXT_ROLL,
            ACI_TXT_ALT, ACI_TXT_AGL, ACI_TXT_SPEED, ACI_TXT_VSI
        })
            XPSetWidgetDescriptor(widgetIds[i], "");

        btnCamera = false;
        btnVisible = false;
        btnAutoVisible = false;
    }
    
    if (btnAuto)
        title += " (" INFO_WND_AUTO_AC ")";
    SetDescriptor(title);
}
