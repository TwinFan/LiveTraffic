//
//  SettingsUI.h
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

#ifndef SettingsUI_h
#define SettingsUI_h

#include "ACInfoWnd.h"

//
// Settings UI Main window
//
class LTSettingsUI : public TFMainWindowWidget
{
protected:
    XPWidgetID* widgetIds;              // all widget ids in the dialog
    TFButtonGroup tabGrp;               // button group to switch 'tabs'
    // sub-windows ('tabs')
    TFWidget subBasicsLive, subBasicsRight, subAcLabel, subAdvcd, subCSL, subDebug;
    
    // Basics tab
    TFButtonDataRef btnBasicsEnable,    // enable display of aircrafts
                    btnBasicsAutoStart;
    // enable/disable flight data channels
    TFButtonDataRef btnOpenSkyLive, btnOpenSkyMasterdata, btnADSBLive;
    TFTextFieldWidget txtADSBAPIKey;
    TFWidget capADSBOutput;
    TFButtonDataRef btnRealTraffic;
    TFWidget capRealTrafficStatus, capRealTrafficMetar;
    TFButtonDataRef btnForeFlight;
    TFIntFieldDataRef intFFTrfcIntvl;
    TFButtonDataRef btnFFUsersPlane, btnFFTraffic;
    // right hand side
    TFButtonDataRef btnLndLightsTaxi;
    TFIntFieldDataRef intHideBelowAGL;
    TFButtonDataRef btnHideTaxiing;
    TFButtonDataRef btnAIonRequest;

    // A/C Labels tab
    TFDataRefLink drCfgLabels;          // links to dataRef livetraffic/cfg/labels
    TFDataRefLink drCfgLabelShow;       // links to dataRef livetraffic/cfg/label_show
    TFButtonGroup btnGrpLabelColorDyn;
    TFDataRefLink drLabelColDyn;
    TFIntFieldDataRef intLabelColor;

    // Advanced tab
    TFButtonGroup logLevelGrp;          // radio buttons to select logging level
    TFButtonGroup msgAreaLevelGrp;      // radio buttons to select msg area level
    TFIntFieldDataRef intMaxNumAc, intMaxFullNumAc, intFullDistance;
    TFIntFieldDataRef intFdStdDistance, intFdRefreshIntvl;
    TFIntFieldDataRef intFdBufPeriod, intAcOutdatedIntvl;
    TFIntFieldDataRef intNetwTimeout;

    // CSL tab
    enum { SETUI_CSL_PATHS=7, SETUI_CSL_ELEMS_PER_PATH=3 };
    static constexpr int SETUI_CSL_PATHS_NUM_ELEMS = SETUI_CSL_PATHS * SETUI_CSL_ELEMS_PER_PATH;
    TFButtonDataRef btnRegLibXPlanempDR;
    TFTextFieldWidget txtCSLPaths[SETUI_CSL_PATHS];
    TFTextFieldWidget txtDefaultAcType, txtGroundVehicleType;
    
    // Debug tab
    TFACSearchEditWidget txtDebugFilter;
    TFButtonWidget btnDebugLogLevelDebug;
    TFButtonDataRef btnDebugLogACPos, btnDebugLogModelMatch, btnDebugLogRawFd;
    TFTextFieldWidget txtFixAcType, txtFixOp, txtFixLivery;

public:
    LTSettingsUI();
    ~LTSettingsUI();
    
    // (de)register widgets (not in constructor to be able to use global variable)
    void Enable();
    void Disable();
    bool isEnabled () const { return widgetIds && *widgetIds; }
    
    // first creates the structure, then shows the window
    virtual void Show (bool bShow = true);

protected:
    // capture entry into 'filter for transponder hex code' field
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    // writes current values out into config file
    virtual bool MsgHidden (XPWidgetID hiddenWidget);

    // update state of log-level buttons from dataRef
    virtual bool TfwMsgMain1sTime ();

    // handles show/hide of 'tabs'
    virtual bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
    virtual bool MsgPushButtonPressed (XPWidgetID buttonWidget);
    
    // Handle checkboxes for a/c labels
    void LabelBtnInit();
    void LabelBtnSave();
    
    // Handle RealTraffic status
    void UpdateRealTraffic();
    
    // Save CSL path / Load a CSL package
    void SaveCSLPath(int idx);
};

#endif /* SettingsUI_h */
