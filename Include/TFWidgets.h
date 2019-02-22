//
//  TFWidgets.h
//
// Defines a number of classes around X-Planes widgets,
// so they are more easily accessible with C++ semantics.
//
// This module is written in the hope of being useful outside LiveTraffic,
// but has not yet been tested stand-alone.
//

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

#ifndef TFWidgets_h
#define TFWidgets_h

#include <stddef.h>
#include <string>
#include <forward_list>
#include <vector>
#include "XPLMDataAccess.h"
#include "XPWidgets.h"
#include "XPWidgetUtils.h"
#include "XPStandardWidgets.h"
#include "XPLMDisplay.h"
#include "XPCompatibility.h"

enum TFWndMode {
    TF_MODE_CLASSIC     = 0,        // XP10 style in main window
    TF_MODE_FLOAT,                  // XP11 modern floating window
    TF_MODE_POPOUT,                 // XP11 popped out window in "first class OS window"
    TF_MODE_VR                      // XP11 moved to VR window
};

//
//MARK: replacement/enhancement for XPUCreateWidgets
//

void TFUCreateWidgets(const XPWidgetCreate_t inWidgetDefs[],
                      int                  inCount,
                      XPWidgetID           inParamParent,
                      XPWidgetID *         ioWidgets);

// slightly changed definition:
// (left|top) is relative to parent with positive top going down
// (right|bottom) is meant to be widht|height if positive, or
//                relative to parent right|bottom if negative

// TFWidgetCreate_t
// adds 3 properties to add after widget creation
struct TFWidgetCreate_t : public XPWidgetCreate_t {
    struct TFProp_t {
        XPWidgetPropertyID  propId;
        intptr_t            propVal;
    } props[3];
};

// combines both above calls, adds widget properties
bool TFUCreateWidgetsEx(const TFWidgetCreate_t  inWidgetDefs[],
                        int                     inCount,
                        XPWidgetID              inParamParent,
                        XPWidgetID *            ioWidgets,
                        TFWndMode               wndMode = TF_MODE_CLASSIC);

// get widget descriptor in a safe way and return as a std::string
std::string TFGetWidgetDescriptor (XPWidgetID me);

// returns the index of the widget under its parent
// (kind of reverse to XPGetNthChildWIdget)
int TFGetWidgetChildIndex (XPWidgetID me);

//
// TFWidget: Base class for any widget
//
class TFWidget
{
private:
    XPWidgetID  me          = NULL;
    XPLMWindowID wndId      = NULL;
    
public:
    TFWidget (XPWidgetID _me = NULL);
    virtual ~TFWidget();
    void setId (XPWidgetID _me);

public:
    // Actions
    virtual void Show (bool bShow = true);
    bool isVisible() const;
    
    void MoveTo (int left, int top);
    void MoveBy (int x, int y);
    void Center ();
    
    void GetGeometry (int* left, int* top, int* right, int* bottom) const;
    int GetWidth () const;
    int GetHeight () const;
    void SetGeometry (int left, int top, int right, int bottom);
    
    TFWndMode GetWndMode () const;
    inline void SetWindowPositioningMode(XPLMWindowPositioningMode inPositioningMode,
                                      int                  inMonitorIndex)
        { XPC_SetWindowPositioningMode(wndId, inPositioningMode, inMonitorIndex); }

    std::string GetDescriptor () const;
    void SetDescriptor (std::string text) { XPSetWidgetDescriptor(me, text.c_str()); }
    void SetDescriptor (double d, int decimals = 0);
    
    bool IsInFront () const         { return XPIsWidgetInFront(me) != 0; }
    void BringToFront ();
    
    XPWidgetID SetKeyboardFocus();
    void LoseKeyboardFocus();
    bool HaveKeyboardFocus() const  { return XPGetWidgetWithFocus() == me; }
    
    intptr_t GetProperty (XPWidgetPropertyID prop) const;
    bool GetBoolProperty (XPWidgetPropertyID prop) const;
    bool ExistsProperty (XPWidgetPropertyID prop) const;
    void SetProperty (XPWidgetPropertyID prop, intptr_t val);
    
    // this allows using '*this' as widget id
    operator XPWidgetID() const { return me; }
    XPWidgetID getId() const { return me; }
    inline bool operator == (const TFWidget& w) const { return me == w.me; }
    inline bool operator == (XPWidgetID wid) const    { return me == wid; }
    
    XPLMWindowID getWndId() const { return wndId; }
    
public:
    // static message dispatching
    static int DispatchMessages (XPWidgetMessage    inMessage,
                                 XPWidgetID         inWidget,
                                 intptr_t           inParam1,
                                 intptr_t           inParam2);

protected:
    void DetermineWindowMode ();
    
    // private messages
    enum {
        TFW_MSG_MAIN_SHOWHIDE = xpMsg_UserStart,    // main window shown/hidden
        TFW_MSG_MAIN_1S_TIMER,                      // triggers every second
    };
    
    // general message handling, overwrite e.g. for custom messages
    virtual bool HandleMessage (XPWidgetMessage    inMessage,
                               intptr_t           inParam1,
                               intptr_t           inParam2);

    // standard widget message handlers
    // (for all but paint/draw messages)
    virtual bool MsgCreate  (bool bAddedAsSubclass);
    virtual bool MsgDestroy (bool bRecursive);
    virtual bool MsgKeyTakeFocus (bool bChildGaveUp);
    virtual bool MsgKeyLoseFocus (bool bTakenByOtherWidget);
    virtual bool MsgMouseDown (const XPMouseState_t& mouse);
    virtual bool MsgMouseDrag (const XPMouseState_t& mouse);
    virtual bool MsgMouseUp (const XPMouseState_t& mouse);
    virtual bool MsgMouseWheel (const XPMouseState_t& mouse);
    virtual bool MsgReshape (XPWidgetID originId, const XPWidgetGeometryChange_t& geoChange);
    virtual bool MsgAcceptChild (XPWidgetID childId);
    virtual bool MsgLoseChild (XPWidgetID childId);
    virtual bool MsgAcceptParent (XPWidgetID parentId);
    virtual bool MsgShown (XPWidgetID shownWidget);
    virtual bool MsgHidden (XPWidgetID hiddenWidget);
    virtual bool MsgDescriptorChanged ();
    virtual bool MsgPropertyChanged (XPWidgetPropertyID propId, intptr_t val);
    virtual bool MsgCursorAdjust (const XPMouseState_t& mouse, XPLMCursorStatus& crsrStatus);
    virtual bool MsgKeyPress (XPKeyState_t& key);

    // button messages, which are passed up the widget hierarchy
    virtual bool MsgPushButtonPressed (XPWidgetID buttonWidget);
    virtual bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
    
    // text field messages
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    
    // scroll bar messages
    virtual bool MsgScrollBarSliderPositionChanged (XPWidgetID scrollBarWidget, int pos);
    
    // private messages
    // main window got shown/hidden
    virtual bool TfwMsgMainShowHide (XPWidgetID mainWidget, bool bShow);
    // triggered every seond (if started by TFMainWindowWidget::StartStopTimerMessages)
    virtual bool TfwMsgMain1sTime ();
};

//
// TFButtonWidget
//
class TFButtonWidget : public TFWidget
{
public:
    TFButtonWidget (XPWidgetID _me = NULL) : TFWidget(_me) {}
    
    // checked or not?
    virtual bool SetChecked (bool bCheck = true);
    virtual bool IsChecked () const;
};

//
// TFTextFieldWidget
//
class TFTextFieldWidget : public TFWidget
{
protected:
    std::string oldDescriptor;
public:
    enum TFTextFieldFormatTy {          // force/filter char formatting
        TFF_ANY = 0,
        TFF_UPPER_CASE,
        TFF_HEX,
        TFF_DIGITS
    } tfFormat = TFF_ANY;
    TFTextFieldWidget (XPWidgetID _me = NULL) : TFWidget(_me) {}
    
    void SetSelection (int startPos, int endPos);
    void SelectAll ();
    
protected:
    virtual bool MsgKeyPress (XPKeyState_t& key);
    virtual bool MsgKeyLoseFocus (bool bTakenByOtherWidget);
    virtual bool MsgKeyTakeFocus (bool bChildGaveUp);
};

//
// TFDataRefLink
// (short-cuts to data ref access, mostly inline)
//
class TFDataRefLink
{
protected:
    XPLMDataRef     ref;
    enum dataType_t {
        DT_UNKNOWN  = 0,    // note: we support just one data type
        DT_INT      = 1,    // while XP's dataRefs theoretically support
        DT_FLOAT    = 2,    // several simultaneously
        DT_DOUBLE   = 4,
    } dataType;
public:
    TFDataRefLink (const char* dataRefName = NULL);
    bool setDataRef (const char* dataRefName);
    bool isValid () const       { return ref != NULL; }
    // get current value
    int     GetInt () const     { return XPLMGetDatai(ref); }
    float   GetFloat () const   { return XPLMGetDataf(ref); }
    double  GetDouble () const  { return XPLMGetDatad(ref); }
    // no need yet for array access...
    
    operator int () const       { return GetInt(); }
    operator float () const     { return GetFloat(); }
    operator double () const    { return GetDouble(); }
    
    // set value
    void Set (int val)          { XPLMSetDatai(ref,val); }
    void Set (float val)        { XPLMSetDataf(ref,val); }
    void Set (double val)       { XPLMSetDatad(ref,val); }
    
    TFDataRefLink& operator = (int val)    { Set(val); return *this; }
    TFDataRefLink& operator = (float val)  { Set(val); return *this; }
    TFDataRefLink& operator = (double val) { Set(val); return *this; }
};


//
// TFButtonDataRef
// (button toggles an int dataRef between 0 and 1)
//
class TFButtonDataRef : public TFButtonWidget, TFDataRefLink
{
public:
    TFButtonDataRef (XPWidgetID _me = NULL, const char* dataRefName=NULL);
    void setId (XPWidgetID _me, const char* dataRefName);
protected:
    void Synch ();       // button state with current data ref value
    virtual bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
    virtual bool TfwMsgMain1sTime ();
};

//
// TFButtonGroup
// helper class to coordinate radio-button-like behaviour
//
class TFButtonGroup
{
protected:
    std::vector<XPWidgetID> group;
public:
    TFButtonGroup() {}
    // takes a list of widget ids to form a group
    TFButtonGroup (std::initializer_list<XPWidgetID> _group);
    // adds group members
    void Add (std::initializer_list<XPWidgetID> _group);
    // verifies group members
    bool isInGroup (XPWidgetID id) const;
    // which one is the activated one? Get attributes of the active button
    XPWidgetID GetChecked () const;
    int GetCheckedIndex () const;
    std::string GetDescriptor () const;
    // set the checked one, all others are unchecked
    void SetChecked (XPWidgetID id);
    void SetCheckedIndex (int i);
    
    // 'equality' is defined by object's address...not nice but works
    bool operator == (const TFButtonGroup& g) const {return this == &g;}
    
    // 'handles' the message push button pressed IF AND ONLY IF the widget is part of the group
    bool MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked);
};

//
// TFIntFieldDataRef
// combines a text field (integer format) with a dataRef
//
class TFIntFieldDataRef : public TFTextFieldWidget, public TFDataRefLink
{
public:
    TFIntFieldDataRef (XPWidgetID _me = NULL,
                       const char* dataRefName=NULL,
                       TFTextFieldFormatTy format=TFF_DIGITS );
    void setId (XPWidgetID _me,
                const char* dataRefName,
                TFTextFieldFormatTy format=TFF_DIGITS);
    void Set (int val);
    
protected:
    void Synch ();          // field value with current data ref value
    virtual bool MsgTextFieldChanged (XPWidgetID textWidget, std::string text);
    virtual bool TfwMsgMain1sTime ();
};

//
// TFMainWindowWidget
//
class TFMainWindowWidget : public TFWidget
{
protected:
    std::forward_list<TFButtonGroup*> lstBtnGrp;
    bool bTimerRunning;
public:
    TFMainWindowWidget (XPWidgetID _me = NULL);
    virtual ~TFMainWindowWidget();

protected:
    // handle specific MainWindow messages
    virtual bool HandleMessage (XPWidgetMessage    inMessage,
                               intptr_t           inParam1,
                               intptr_t           inParam2);
    
    // standard widget message handlers
    virtual bool MsgShown (XPWidgetID shownWidget);
    virtual bool MsgHidden (XPWidgetID hiddenWidget);
    virtual bool MsgReshape (XPWidgetID originId, const XPWidgetGeometryChange_t& geoChange);
    virtual bool MsgKeyPress (XPKeyState_t& key);
    virtual bool MessageCloseButtonPushed();         // hides(!) the window
    virtual bool MsgButtonStateChanged (XPWidgetID btnId, bool bNowChecked);
    virtual void HookButtonGroup (TFButtonGroup& btnGrp);
    virtual void UnhookButtonGroup (TFButtonGroup& btnGrp);
    
    // handle one-second timer messages
protected:
    enum { TFW_TIMER_INTVL=1 };
    void StartStopTimerMessages (bool bStart);
    static float CB1sTimer (float, float, int, void*);
};


#endif /* TFWidgets_h */
