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
    std::string transpIcao;         // key to the a/c to display
public:
    TFACSearchEditWidget (XPWidgetID _me = NULL, const char* szKey = NULL);

    // Find my aircraft
    const LTFlightData* SearchFlightData (const std::string key);
    void SetTranspIcao (const std::string transpIcao);

    // Get the found aircraft
    bool HasTranspIcao () const { return !transpIcao.empty(); }
    const std::string GetTranspIcao () const { return transpIcao; }
    unsigned int GetTranspIcaoInt () const { return (unsigned)strtoul (transpIcao.c_str(), NULL, 16); }
    LTFlightData* GetFlightData () const;
    LTAircraft* GetAircraft () const;
    
protected:
    // capture entry into the key field
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    
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
    bool    bAutoAc = false;        // do we pick the a/c to show automatically?
    
    // data output fields
    TFWidget valSquawk;
    TFWidget valPos, valBearing, valDist, valPhase;
    TFWidget valGear, valFlaps, valLights;
    TFWidget valHeading, valPitch, valRoll, valAlt, valAGL, valSpeed, valVSI;
    
    // check boxes for visibility
    TFButtonWidget btnCamera, btnVisible, btnAutoVisible;
    TFWidget capAutoVisible;
    
public:
    ACIWnd(const char* szKey = nullptr);
    ACIWnd(bool bAuto) : ACIWnd(bAuto ? INFO_WND_AUTO_AC : nullptr) {}
    virtual ~ACIWnd();
    
    // constructor finished initialization?
    bool isEnabled () const { return widgetIds && *widgetIds; }
    
    // create a new window
    static ACIWnd* OpenNewWnd (const char* szIcao = nullptr);
    
protected:
    // capture entry into the key field
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    // handles visibility buttons
    virtual bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
    // triggered every seond to update values in the window
    virtual bool TfwMsgMain1sTime ();
    // Updated myself
    bool UpdateFocusAc ();          // switch to another focus a/c?
    void UpdateStatValues ();       // static fields
    void UpdateDynValues ();        // dynamic fields
};

#endif /* ACInfoWnd_h */
