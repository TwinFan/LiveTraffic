/// @file       ACInfoWnd.h
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

#ifndef ACInfoWnd_h
#define ACInfoWnd_h

#include "TFWidgets.h"

//
// Text Edit field searching for a/c
//

// Seach for a/c with text provided by user
// Replace by icao transp hex code
// Provide LTFlighData/LTAircraft object on request
class TFACSearchEditWidget : public TFTextFieldWidget
{
protected:
    LTFlightData::FDKeyTy acKey;            // key to the a/c to display
public:
    TFACSearchEditWidget (XPWidgetID _me = NULL, const char* szKey = NULL);

    // Find my aircraft
    const LTFlightData* SearchFlightData (std::string ac_key);
    void SetAcKey (const LTFlightData::FDKeyTy& _key);

    // Get the found aircraft
    bool HasAcKey () const { return !acKey.empty(); }
    const std::string GetAcKey () const { return acKey; }
    unsigned int GetAcKeyNum () const { return (unsigned)strtoul (acKey.c_str(), NULL, 16); }
    LTFlightData* GetFlightData () const;
    LTAircraft* GetAircraft () const;
    
protected:
    // capture entry into the key field
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    virtual bool MsgKeyPress (XPKeyState_t& key);
};

//
// A/C Info Main Window
//
class ACIWnd : public TFMainWindowWidget
{
protected:
    XPWidgetID* widgetIds = nullptr;    // all widget ids in the dialog
    
    // edit field for a/c key
    TFACSearchEditWidget txtAcKey;
    TFButtonWidget btnAuto;
 //   bool    bAutoAc = false;        // do we pick the a/c to show automatically?
    
    // data output fields
    TFWidget valSquawk;
    TFWidget valPos, valBearing, valDist, valPhase;
    TFWidget valGear, valFlaps, valLights;
    TFWidget valHeading, valPitch, valRoll, valAlt, valAGL, valSpeed, valVSI;
    
    // check boxes for visibility
    TFButtonWidget btnCamera, btnVisible, btnAutoVisible;
    TFWidget capAutoVisible;
    
    static bool bAreShown;
    
public:
    ACIWnd(TFWndMode wndMode, const char* szKey = INFO_WND_AUTO_AC);
    virtual ~ACIWnd();
    
    // constructor finished initialization?
    bool isEnabled () const { return widgetIds && *widgetIds; }
    
    // create a new window
    static ACIWnd* OpenNewWnd (TFWndMode wndMode, const char* szIcao = INFO_WND_AUTO_AC);
    // move all windows into/out of VR
    static void MoveAllVR (bool bIntoVR);
    // hide/show all windows, returns new state
    static bool ToggleHideShowAll();
    static bool AreShown() { return bAreShown; }
    static void CloseAll();
    
protected:
    // capture entry into the key field
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    // handles visibility buttons
    virtual bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
    virtual bool MsgPushButtonPressed (XPWidgetID buttonWidget);
    // triggered every seond to update values in the window
    virtual bool TfwMsgMain1sTime ();
    // close and delete(!) myself
    virtual bool MessageCloseButtonPushed();
    // Updated myself
    bool UpdateFocusAc ();          // switch to another focus a/c?
    bool ClearStaticValues ();      // clear static fields, returns if there was text that was cleared away
    void UpdateStatValues ();       // static fields
    void UpdateDynValues ();        // dynamic fields
};

#endif /* ACInfoWnd_h */
