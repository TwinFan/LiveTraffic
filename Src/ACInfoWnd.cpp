//
//  ACInfoWnd.cpp
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
//MARK: Window Structure
//

// indexes into the below definition array, must be kept in synch with the same
enum ACI_WIDGET_IDX_T {
    ACI_MAIN_WND     = 0,
    // key / search: input/output
    ACI_CAP_AC_KEY_REG,
    ACI_TXT_AC_KEY,
    // static data
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

    // always last: number of UI elements
    ACI_NUMBER_OF_ELEMENTS
};

// for ease of definition coordinates start at (0|0)
// window will be centered shortly before presenting it
TFWidgetCreate_t ACI_WND[] =
{
    {   0,   0, 270, 350, 1, "LiveTraffic A/C Info", 1, NO_PARENT, xpWidgetClass_MainWindow, {xpProperty_MainWindowHasCloseBoxes, 1, xpProperty_MainWindowType,xpMainWindowStyle_Translucent,0,0} },
    {   5,  20,  95,  10, 1, "A/C key | Registr.",  0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  20,  70,  15, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_TextField,{xpProperty_TextFieldType,xpTextTranslucent, xpProperty_MaxCharacters,8, 0,0} },
    { 195,  20,  70,  15, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  35,  95,  10, 1, "ICAO Type | Class.",  0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  35,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195,  35,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  50,  95,  10, 1, "Manufacturer",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  50, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  65,  95,  10, 1, "Model",               0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  65, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  80,  95,  10, 1, "Operator",            0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  80, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5,  95,  95,  10, 1, "CSL Model",           0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120,  95, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 115,  95,  10, 1, "Call Sign | Squawk",  0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 115,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 115,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 130,  95,  10, 1, "Flight: Route",       0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 130, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 150,  95,  10, 1, "Simulated Time",      0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 150, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 165,  95,  10, 1, "Last Data [s] | Chnl",0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 165,  40,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 165, 165, 100,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 185,  95,  10, 1, "Position",            0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 185, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 200,  95,  10, 1, "Bearing | Dist. [nm]",0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 200,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 200,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 215,  95,  10, 1, "Flight Phase",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 215, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 230,  95,  10, 1, "Gear | Flaps",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 230,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 230,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 245,  95,  10, 1, "Lights",              0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 245, 145,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 265,  95,  10, 1, "Heading [°]",         0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 265,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 280,  95,  10, 1, "Pitch | Roll [°]",    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 280,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 280,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

    {   5, 300,  95,  10, 1, "Altitude | AGL [ft]", 0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 300,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 300,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    {   5, 315,  95,  10, 1, "Speed [kn] | VSI [ft]", 0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 120, 315,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 195, 315,  70,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    
    {  10, 335,  10,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    {  20, 332,  55,  10, 1, "Camera",              0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 110, 335,  10,  10, 1, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    { 120, 332,  55,  10, 1, "Visible",             0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },
    { 185, 335,  10,  10, 0, "",                    0, ACI_MAIN_WND, xpWidgetClass_Button,  {xpProperty_ButtonType, xpRadioButton, xpProperty_ButtonBehavior,xpButtonBehaviorCheckBox, 0,0} },
    { 195, 332,  55,  10, 0, "Auto Visible",        0, ACI_MAIN_WND, xpWidgetClass_Caption, {xpProperty_CaptionLit,1, 0,0, 0,0} },

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

// Use the provided key to seach the list of aircrafts if something matches:
// - icao transponder code
// - registration
// - call sign
// - flight number
const LTFlightData* TFACSearchEditWidget::SearchFlightData (const std::string key)
{
    mapLTFlightDataTy::const_iterator fdIter = mapFd.cend();
    
    if (!key.empty()) {
        // is it a small integer number, i.e. used as index?
        if (key.length() <= 3 &&
            key.find_first_not_of("0123456789") == std::string::npos)
        {
            int i = std::stoi(key);
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
                         [&key](const mapLTFlightDataTy::value_type& mfd)
                         { return mfd.second.IsMatch(key); }
                         );
        }
    }
    
    // found?
    if (fdIter != mapFd.cend()) {
        SetTranspIcao(fdIter->second.key());
        // return the result
        return &fdIter->second;
    }
    
    // not found
    transpIcao.clear();
    return nullptr;
}

void TFACSearchEditWidget::SetTranspIcao (const std::string _icao)
{
    // remember transpIcao
    oldDescriptor = transpIcao = _icao;
    // replace my own content with the transpIcao hex code
    SetDescriptor(transpIcao);
}

// Get my defined aircraft
// As aircrafts can be removed any frame this needs to be called
// over and over again.
// Note: Deleting aircrafts happens in a flight loop callback,
//       which is the same thread as this here is running in.
//       So we can safely assume the returned pointer is valid until
//       we return. But not any longer.
LTFlightData* TFACSearchEditWidget::GetFlightData () const
{
    // find the flight data by key
    mapLTFlightDataTy::iterator fdIter = mapFd.find(transpIcao);
    // return flight data if found
    return fdIter != mapFd.end() ? &fdIter->second : nullptr;

}

// even if FlightData existis, a/c might still be NULL if not yet created!
LTAircraft* TFACSearchEditWidget::GetAircraft () const
{
    LTFlightData* pFD = GetFlightData();
    return pFD ? pFD->GetAircraft() : nullptr;
}

// capture entry into myself -> trigger search for aircrafts
bool TFACSearchEditWidget::MsgTextFieldChanged (XPWidgetID textWidget,
                                                std::string text)
{
    // if this is not about me then don't handle...ask the class hierarchy
    if (textWidget != *this)
        return TFTextFieldWidget::MsgTextFieldChanged(textWidget, text);
    
    // it was me...so start a search with what is in my edit field
    if (text == INFO_WND_AUTO_AC)
        transpIcao.clear();         // AUTO -> handled by main window
    else
        SearchFlightData(text);     // otherwise search for the a/c
    
    // return 'false' on purpose: message also needs to be sent to main window
    return false;
}


//
// MARK: ACIWnd
//

// Constructor fully initializes and display the window
ACIWnd::ACIWnd(const char* szKey) :
widgetIds(nullptr)
{
    // array, which receives ids of all created widgets
    widgetIds = new XPWidgetID[NUM_WIDGETS];
    LOG_ASSERT(widgetIds);
    memset(widgetIds, 0, sizeof(XPWidgetID)*NUM_WIDGETS );
    
    // create all widgets, i.e. the entire window structure
    if (!TFUCreateWidgetsEx(ACI_WND, NUM_WIDGETS, NULL, widgetIds))
    {
        SHOW_MSG(logERR,ERR_WIDGET_CREATE);
        delete widgetIds;       // indicates: not initialized
        widgetIds = nullptr;
        return;
    }
    
    // register myself in base class for message handling
    setId(widgetIds[0]);
    
    // text field for a/c key entry, upper case only
    txtAcKey.setId(widgetIds[ACI_TXT_AC_KEY]);
    if (szKey) {
        // running in auto a/c mode?
        if (strncmp(szKey, INFO_WND_AUTO_AC, sizeof(INFO_WND_AUTO_AC)) == 0)
        {
            bAutoAc = true;
            SetDescriptor(GetDescriptor() + " (" INFO_WND_AUTO_AC ")");
            szKey = nullptr;
        }
        else
            txtAcKey.SearchFlightData(szKey);
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

    // if we don't have an a/c yet give the use the focus and let him enter one
    if (bAutoAc)
        UpdateFocusAc();
    else if (!txtAcKey.HasTranspIcao())
        txtAcKey.SetKeyboardFocus();
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
}

// static function: creates a new window
ACIWnd* ACIWnd::OpenNewWnd (const char* szIcao)
{
    ACIWnd* pWnd = new ACIWnd(szIcao);
    if (pWnd && !pWnd->isEnabled()) {       // did not init successfully
        delete pWnd;
        pWnd = nullptr;
    }
    return pWnd;
}

// capture entry into the a/c key field
bool ACIWnd::MsgTextFieldChanged (XPWidgetID textWidget, std::string text)
{
    // not my key field?
    if (txtAcKey != textWidget)
        return TFMainWindowWidget::MsgTextFieldChanged(textWidget, text);
    
    // my key changed!
    bAutoAc = text == INFO_WND_AUTO_AC;     // auto switch a/c?
    if (bAutoAc) {
        UpdateFocusAc();
    } else {
        UpdateStatValues();
        UpdateDynValues();
    }
    
    // stored a valid entry?
    if (txtAcKey.HasTranspIcao())
        // give up keyboard focus, return to X-Plane
        txtAcKey.LoseKeyboardFocus();
    else
        // have user try again:
        txtAcKey.SetKeyboardFocus();
    
    // msg handled
    return true;
}

// handles visibility buttons
bool ACIWnd::MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked)
{
    LTAircraft* pAc = txtAcKey.GetAircraft();
    if (pAc)
    {
        if (btnCamera == buttonWidget) {
            // Call a/c camera view
            pAc->ToggleCameraView();
            btnCamera.SetChecked(pAc->IsInCameraView());
            return true;
        }
        else if (btnVisible == buttonWidget) {
            // visibility set directly, auto-visibility will be off then
            pAc->SetVisible(bNowChecked);
            btnAutoVisible.SetChecked(pAc->IsAutoVisible());
            return true;
        }
        else if (btnAutoVisible == buttonWidget) {
            // auto-visibility changed...returns current a/c visibiliy
            btnVisible.SetChecked(pAc->SetAutoVisible(bNowChecked));
            return true;
        }
    }

    // pass on in class hierarchy
    return TFMainWindowWidget::MsgButtonStateChanged(buttonWidget, bNowChecked);
}

// triggered every second to update values in the window
bool ACIWnd::TfwMsgMain1sTime ()
{
    TFMainWindowWidget::TfwMsgMain1sTime();
    if (!UpdateFocusAc())               // changed focus a/c? If not:
        UpdateDynValues();              // update our values
    return true;
}

// switch to another focus a/c?
bool ACIWnd::UpdateFocusAc ()
{
    if (!bAutoAc) return false;
    
    // find the current focus a/c and if different from current one then switch
    const LTFlightData* pFocusAc = LTFlightData::FindFocusAc(DataRefs::GetViewHeading());
    if (pFocusAc && pFocusAc->key() != txtAcKey.GetTranspIcao()) {
        txtAcKey.SetTranspIcao(pFocusAc->key());
        UpdateStatValues();
        UpdateDynValues();
        return true;
    }
    
    // nothing found?
    if (!pFocusAc) {
        // start at least the timer for regular focus a/c updates
        txtAcKey.SetTranspIcao("");
        StartStopTimerMessages(true);
    }
    return false;
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
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_OP], strAtMost(stat.opIcao + " " + stat.op,25).c_str());

        XPSetWidgetDescriptor(widgetIds[ACI_TXT_CALLSIGN], stat.call.c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_FLIGHT_ROUTE], stat.flightRoute().c_str());
        
        // start the timer for regular dyn data updates
        StartStopTimerMessages(true);
        
        // set as 'selected' aircraft for debug output
        dataRefs.LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)),
                            txtAcKey.GetTranspIcaoInt());
    } else {
        // clear static values
        for (int i: {
            ACI_TXT_REG, ACI_TXT_ICAO, ACI_TXT_CLASS, ACI_TXT_MANU, ACI_TXT_MODEL,
            ACI_TXT_OP, ACI_TXT_CALLSIGN, ACI_TXT_FLIGHT_ROUTE
        })
            XPSetWidgetDescriptor(widgetIds[i], "");

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
        // _last_ dyn data object
        const LTFlightData::FDDynamicData dyn (pAc->fd.WaitForSafeCopyDyn(false));

        // title is dynmic
        title = strAtMost(pAc->fd.ComposeLabel(), 25);
        
        // update all field values
        const positionTy& pos = pAc->GetPPos();
        double ts = dataRefs.GetSimTime();
        valSquawk.SetDescriptor(dyn.radar.code);
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_DISPLAYED_USING],
                              strAtMost(pAc->GetModelName(),25).c_str());
        XPSetWidgetDescriptor(widgetIds[ACI_TXT_SIM_TIME], ts2string(time_t(ts)).c_str());
        // last update
        ts -= dyn.ts;                   // difference 'dyn.ts - simTime'
        ts *= -1;
        if (-10000 <= ts && ts <= 10000)
        {
            char szBuf[20];
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
        valHeading.SetDescriptor(pos.heading());
        valPitch.SetDescriptor(pAc->GetPitch());
        valRoll.SetDescriptor(pAc->GetRoll());
        valAlt.SetDescriptor(pos.alt_ft());
        if (pos.IsOnGnd())
            valAGL.SetDescriptor(positionTy::GrndE2String(positionTy::GND_ON));
        else
            valAGL.SetDescriptor(pAc->GetPHeight_ft());
        valSpeed.SetDescriptor(pAc->GetSpeed_kt());
        valVSI.SetDescriptor(pAc->GetVSI_ft());
        
        // visibility buttons
        btnCamera.SetChecked(pAc->IsInCameraView());
        btnVisible.SetChecked(pAc->IsVisible());
        btnAutoVisible.SetChecked(pAc->IsAutoVisible());
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

        btnCamera.SetChecked(false);
        btnVisible.SetChecked(false);
        btnAutoVisible.SetChecked(false);
    }
    
    if (bAutoAc)
        title += " (" INFO_WND_AUTO_AC ")";
    SetDescriptor(title);
}
