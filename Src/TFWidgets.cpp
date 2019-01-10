//
//  TFWidgets.cpp
//
// Implements a number of classes around X-Planes widgets,
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

#include "TFWidgets.h"

#include <string>
#include <climits>
#include <cmath>
#include <forward_list>

#include "XPLMProcessing.h"

//
//MARK: replacement/enhancement for XPUCreateWidgets
//
void TFUCreateWidgets(const XPWidgetCreate_t inWidgetDefs[],
                      int                  inCount,
                      XPWidgetID           inParamParent,
                      XPWidgetID *         ioWidgets)
{
    for (int i = 0; i < inCount; i++) {
        ioWidgets[i] = XPCreateWidget(inWidgetDefs[i].left,
                                      inWidgetDefs[i].top,
                                      inWidgetDefs[i].right,
                                      inWidgetDefs[i].bottom,
                                      inWidgetDefs[i].visible,
                                      inWidgetDefs[i].descriptor,
                                      inWidgetDefs[i].isRoot,
                                      (0 <= inWidgetDefs[i].containerIndex && inWidgetDefs[i].containerIndex < i) ? ioWidgets[inWidgetDefs[i].containerIndex] :
                                      inWidgetDefs[i].containerIndex == PARAM_PARENT ? inParamParent :
                                      NULL,        // this includes the case of NO_PARENT==-1
                                      inWidgetDefs[i].widgetClass);
    }
}

// turns relative coordinates in the widget structure into global ones
// relative means:
// (left|top) is relative to parent with positive top going down, negative numbers relative from right|bottom
// (right|bottom) is meant to be widht|height or relative to parent
bool TFURelative2GlobalWidgetDefs (TFWidgetCreate_t inWidgetDefs[],
                                   int              inCount)
{
    bool bResult = true;
    
    // verify that first entry has NO_PARENT || PARAM_PARENT
    if (inWidgetDefs[0].containerIndex != NO_PARENT &&
        inWidgetDefs[0].containerIndex != PARAM_PARENT)
        return false;
    
    for (int i = 0; i < inCount; i++ ) {
        // left|top relative to parent widget
        if (inWidgetDefs[i].containerIndex >= 0) {                  // is there a parent widget?
            if (inWidgetDefs[i].containerIndex < i) {               // and is it valid (have we processed it already)?
                const XPWidgetCreate_t& parent = inWidgetDefs[inWidgetDefs[i].containerIndex];
                
                // left|top relative to parent window left|top if positive, or
                //          relative to parent window right|bottom if negative
                if (inWidgetDefs[i].left >= 0)
                    inWidgetDefs[i].left += parent.left;
                else
                    inWidgetDefs[i].left += parent.right;
                
                if (inWidgetDefs[i].top >= 0)
                    inWidgetDefs[i].top  = parent.top - inWidgetDefs[i].top;
                else
                    inWidgetDefs[i].top  = parent.bottom - inWidgetDefs[i].top;
                
                // right|bottom relative to parent window if negative, or
                //              interpret as width|height if positive
                if (inWidgetDefs[i].right <= 0)
                    inWidgetDefs[i].right += parent.right;
                else
                    inWidgetDefs[i].right += inWidgetDefs[i].left;
                
                if (inWidgetDefs[i].bottom <= 0)
                    inWidgetDefs[i].bottom = parent.bottom - inWidgetDefs[i].bottom;
                else
                    inWidgetDefs[i].bottom = inWidgetDefs[i].top - inWidgetDefs[i].bottom;
            }
            else
                bResult = false;                                    // ran into a problem
        }
        // no parent window
        else
        {
            // interpret right|bottom as width|height
            inWidgetDefs[i].right  += inWidgetDefs[i].left;
            inWidgetDefs[i].bottom = inWidgetDefs[i].top - inWidgetDefs[i].bottom;
        }
    }
    
    // return success?
    return bResult;
}

// combines both above calls, adds widget properties
bool TFUCreateWidgetsEx(const TFWidgetCreate_t  inWidgetDefs[],
                        int                  inCount,
                        XPWidgetID           inParamParent,
                        XPWidgetID *         ioWidgets)
{
    // copy widget definitions and turn relative coordinates into global ones
    TFWidgetCreate_t *def = new TFWidgetCreate_t[inCount];
    memmove (def, inWidgetDefs, inCount * sizeof(TFWidgetCreate_t));
    if (!TFURelative2GlobalWidgetDefs(def,inCount))
        return false;
    
    // now create the actual widgets (similar to XPU/TFUCreateWidgets)
    // we know already that containerIndexes are OK, otherwise TFURelative2GlobalWidgetDefs would have failed
    bool bResult = true;
    for (int i = 0; i < inCount; i++) {
        // create the widget
        ioWidgets[i] = XPCreateWidget(def[i].left,
                                      def[i].top,
                                      def[i].right,
                                      def[i].bottom,
                                      def[i].visible,
                                      def[i].descriptor,
                                      def[i].isRoot,
                                      (0 <= def[i].containerIndex && def[i].containerIndex < i) ? ioWidgets[def[i].containerIndex] :
                                      def[i].containerIndex == PARAM_PARENT ? inParamParent :
                                      NULL,        // this includes the case of NO_PARENT==-1
                                      def[i].widgetClass);
        // successfully created?
        if (ioWidgets[i]) {
            // if available also add properties
            for (TFWidgetCreate_t::TFProp_t prop: def[i].props) {
                if (prop.propId)
                    XPSetWidgetProperty(ioWidgets[i], prop.propId, prop.propVal);
            }
        }
        else
            bResult = false;        // failure, return such (but try creating the remaining widgets)
    }

    // remove our own copy
    delete[] def;

    return bResult;
}

// get widget descriptor in a safe way and return as a std::string
std::string TFGetWidgetDescriptor (XPWidgetID me)
{
    // get length of descriptor first, then only the actual descriptor
    int len = XPGetWidgetDescriptor(me, NULL, 0);
    if (len <= 0)
        return std::string();

    std::string ret(len, '\0');
    XPGetWidgetDescriptor (me, ret.data(), len);
    return ret;
}

// returns the index of the widget under its parent
// (kind of reverse to XPGetNthChildWIdget)
int TFGetWidgetChildIndex (XPWidgetID me)
{
    XPWidgetID parent = XPGetParentWidget(me);
    if (!parent)
        return 0;
    
    // loop over all children of my parent (my siblings) and find myself
    int i = 0;
    for (XPWidgetID sibl = XPGetNthChildWidget(parent, i);
         sibl;
         sibl = XPGetNthChildWidget(parent, ++i))
    {
        // found myself? return my index
        if (sibl == me)
            return i;
    }
    
    // didn't find myself!!! (should not happen)
    return 0;
}

//
//MARK: TFWidget
//
TFWidget::TFWidget (XPWidgetID _me) :
me(NULL)
{
    setId(_me);
}

TFWidget::~TFWidget()
{
    if (me) {
        // remove actual widget from XPlane
        XPDestroyWidget(me, 1);
    }
}

// Besides storing the widget id in 'me', this also ensures that
// the XP widget has a pointer to the underlying C++ object, so that
// DispatchMessage can find the object when dispatching messages
void TFWidget::setId(XPWidgetID _me)
{
    // shortcut: no change
    if (me == _me) return;
    
    // if we were attached to some other widget remove us there
    if (me)
        SetProperty(xpProperty_Object, NULL);
    
    // set own value
    me = _me;
    
    // set widgets c++ object attribute and add the generic message handler
    if (me) {
        SetProperty(xpProperty_Object, (intptr_t)this);
        XPAddWidgetCallback(me, DispatchMessages);
    }
}

void TFWidget::Show (bool bShow)
{
    if (bShow)
        XPShowWidget(me);
    else
        XPHideWidget(me);
}

bool TFWidget::isVisible() const
{
    return XPIsWidgetVisible(me) != 0;
}

void TFWidget::MoveTo(int toLeft, int toTop)
{
    // get current geometry
    int left = 0, top = 0;
    GetGeometry(&left, &top, NULL, NULL);
    
    // move to target
    MoveBy ( toLeft - left, toTop - top );
}

void TFWidget::MoveBy(int x, int y)
{
    XPUMoveWidgetBy(me, x, y);
}

void TFWidget::Center()
{
    // Get the screen size
    int left=0, top=0, right=0, bottom=0;
#if defined(XPLM300)
    // Note that we're not guaranteed that the main monitor's lower left is at (0, 0)...
    // We'll need to query for the global desktop bounds!
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
#else
    XPLMGetScreenSize(&right,&top);
#endif
    
    // calc center coordinates
    left = (left+right)/2;
    top = (top+bottom)/2;
    
    // move left/up by half the widget dimension
    left -= GetWidth()/2;
    top += GetHeight()/2;
    
    // move the widget there
    MoveTo(left, top);
}

int TFWidget::GetWidth () const
{
    int left=0, right=0;
    GetGeometry(&left, NULL, &right, NULL);
    return right - left;
}

int TFWidget::GetHeight () const
{
    int top=0, bottom=0;
    GetGeometry(NULL, &top, NULL, &bottom );
    return top - bottom;
}

std::string TFWidget::GetDescriptor () const
{
    return TFGetWidgetDescriptor(me);
}

// format a number as the descriptor
void TFWidget::SetDescriptor (double d, int decimals)
{
    char buf[50];
    snprintf(buf,sizeof(buf), "%.*f", decimals, d);
    XPSetWidgetDescriptor(me, buf);
}

// static function: finds the correct widget object in the map and
// forwards the message there
// (This is an entry function into the LiveTraffic plugin, called by XP)
int TFWidget::DispatchMessages (XPWidgetMessage    inMessage,
                                XPWidgetID         inWidget,
                                intptr_t           inParam1,
                                intptr_t           inParam2)
{
    // get pointer to LTWidget from widget properties
    TFWidget* p = reinterpret_cast<TFWidget*>(XPGetWidgetProperty(inWidget,xpProperty_Object,NULL));
    if (p) {
        // LiveTraffic Top Level Exception handling:
        // catch all, swallow is best I can do
        try {
            return p->HandleMessage(inMessage, inParam1, inParam2);
        } catch (...) {
            // can't do much about it...just ignore it then
        }
    }

    return 0;
}

intptr_t TFWidget::GetProperty (XPWidgetPropertyID prop) const
{
    return XPGetWidgetProperty (me, prop, NULL);
}

bool TFWidget::GetBoolProperty (XPWidgetPropertyID prop) const
{
    return XPGetWidgetProperty (me, prop, NULL) != 0;
}

bool TFWidget::ExistsProperty (XPWidgetPropertyID prop) const
{
    int bExists = 0;
    XPGetWidgetProperty (me, prop, &bExists);
    return bExists != 0;
}

void TFWidget::SetProperty (XPWidgetPropertyID prop, intptr_t val)
{
    XPSetWidgetProperty (me, prop, val);
}

//
// MARK: TFWidget Message Handling
//

// dispatches messages to all the virtual message handling functions
bool TFWidget::HandleMessage (XPWidgetMessage    inMessage,
                              intptr_t           inParam1,
                              intptr_t           inParam2)
{
    // catch most messages and dispatch to virtual functions handling them
    switch (inMessage) {
        case xpMsg_Create:      return MsgCreate((int)inParam1 != 0);
        case xpMsg_Destroy:     return MsgDestroy((int)inParam1 != 0);
        case xpMsg_KeyTakeFocus:return MsgKeyTakeFocus((int)inParam1 != 0);
        case xpMsg_KeyLoseFocus:return MsgKeyLoseFocus((int)inParam1 != 0);
        case xpMsg_MouseDown:   return MsgMouseDown(*reinterpret_cast<XPMouseState_t*>(inParam1));
        case xpMsg_MouseDrag:   return MsgMouseDrag(*reinterpret_cast<XPMouseState_t*>(inParam1));
        case xpMsg_MouseUp:     return MsgMouseUp(*reinterpret_cast<XPMouseState_t*>(inParam1));
        case xpMsg_MouseWheel:  return MsgMouseWheel(*reinterpret_cast<XPMouseState_t*>(inParam1));
        case xpMsg_Reshape:
            return MsgReshape ( reinterpret_cast<XPWidgetID>(inParam1),
                               *reinterpret_cast<XPWidgetGeometryChange_t*>(inParam2) );
        case xpMsg_AcceptChild: return MsgAcceptChild(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_LoseChild:   return MsgLoseChild(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_AcceptParent:return MsgAcceptParent(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_Shown:       return MsgShown(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_Hidden:      return MsgHidden(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_DescriptorChanged:  return MsgDescriptorChanged();
        case xpMsg_PropertyChanged:
            return MsgPropertyChanged (XPWidgetPropertyID(inParam1), inParam2);
        case xpMsg_CursorAdjust:
            return MsgCursorAdjust (*reinterpret_cast<XPMouseState_t*>(inParam1),
                                    *reinterpret_cast<XPLMCursorStatus*>(inParam2));
            // key pressed
        case xpMsg_KeyPress:
            return MsgKeyPress(*reinterpret_cast<XPKeyState_t*>(inParam1));

        // button messages passed up the widget hierarchy
        case xpMsg_PushButtonPressed:
            return MsgPushButtonPressed(reinterpret_cast<XPWidgetID>(inParam1));
        case xpMsg_ButtonStateChanged:
            return MsgButtonStateChanged(reinterpret_cast<XPWidgetID>(inParam1),
                                         inParam2 != 0);
            
        // text field messages
        case xpMsg_TextFieldChanged:
            // as a courtesy we deliver the new text with the message
            return MsgTextFieldChanged(reinterpret_cast<XPWidgetID>(inParam1),
                                       TFGetWidgetDescriptor(reinterpret_cast<XPWidgetID>(inParam1)));
            
        // scroll bar messages
        case xpMsg_ScrollBarSliderPositionChanged:
            // as a courtesy we deliver the new slider position with the message
            return MsgScrollBarSliderPositionChanged(reinterpret_cast<XPWidgetID>(inParam1),
                                                     int(XPGetWidgetProperty(reinterpret_cast<XPWidgetID>(inParam1),
                                                                             xpProperty_ScrollBarSliderPosition,
                                                                             NULL)));
            
        // private messages
        case TFW_MSG_MAIN_SHOWHIDE:
            return TfwMsgMainShowHide(reinterpret_cast<XPWidgetID>(inParam1),
                                      inParam2!= 0);
        case TFW_MSG_MAIN_1S_TIMER:
            return TfwMsgMain1sTime();
            
        // otherwise not handled
        default:
            return false;
    }
}

// standard messages: don't do anything, return 'not handled'
bool TFWidget::MsgCreate  (bool /*bAddedAsSubclass*/) { return false; }
bool TFWidget::MsgKeyTakeFocus (bool /*bChildGaveUp*/) { return false; }
bool TFWidget::MsgKeyLoseFocus (bool /*bTakenByOtherWidget*/) { return false; }
bool TFWidget::MsgMouseDown (const XPMouseState_t& /*mouse*/) { return false; }
bool TFWidget::MsgMouseDrag (const XPMouseState_t& /*mouse*/) { return false; }
bool TFWidget::MsgMouseUp (const XPMouseState_t& /*mouse*/) { return false; }
bool TFWidget::MsgReshape (XPWidgetID /*originId*/, const XPWidgetGeometryChange_t& /*geoChange*/) { return false; }
bool TFWidget::MsgAcceptChild (XPWidgetID /*childId*/) { return false; }
bool TFWidget::MsgLoseChild (XPWidgetID /*childId*/) { return false; }
bool TFWidget::MsgAcceptParent (XPWidgetID /*parentId*/) { return false; }
bool TFWidget::MsgShown (XPWidgetID /*shownWidget*/) { return false; }
bool TFWidget::MsgHidden (XPWidgetID /*hiddenWidget*/) { return false; }
bool TFWidget::MsgDescriptorChanged () { return false; }
bool TFWidget::MsgPropertyChanged (XPWidgetPropertyID /*propId*/, intptr_t /*val*/) { return false; }
bool TFWidget::MsgMouseWheel (const XPMouseState_t& /*mouse*/) { return false; }
bool TFWidget::MsgCursorAdjust (const XPMouseState_t& /*mouse*/, XPLMCursorStatus& /*crsrStatus*/) { return false; }
bool TFWidget::MsgKeyPress (XPKeyState_t& /*key*/) { return false; }
bool TFWidget::MsgPushButtonPressed (XPWidgetID) { return false; }
bool TFWidget::MsgButtonStateChanged (XPWidgetID, bool) { return false; }
bool TFWidget::MsgTextFieldChanged (XPWidgetID, std::string) { return false; }
bool TFWidget::MsgScrollBarSliderPositionChanged (XPWidgetID, int) { return false; }
bool TFWidget::TfwMsgMainShowHide (XPWidgetID, bool /*bShow*/) { return false; }
bool TFWidget::TfwMsgMain1sTime () { return false; }

bool TFWidget::MsgDestroy (bool /*bRecursive*/)
{
    // widget is destroyed, invalidate our knowledge of it
    me = 0;
    return true;
}

//
// MARK: TFButtonWidget
//

bool TFButtonWidget::SetChecked (bool bCheck)
{
    bool bBefore = IsChecked();
    SetProperty(xpProperty_ButtonState, bCheck);
    return bBefore;
}

bool TFButtonWidget::IsChecked () const
{
    return GetBoolProperty(xpProperty_ButtonState);
}

//
// MARK: TFButtonDataRef
//
TFButtonDataRef::TFButtonDataRef (XPWidgetID _me,
                                  const char* dataRefName) :
TFButtonWidget(_me), TFDataRefLink(dataRefName)
{
    Synch();
}

// (deferred) initialization
void TFButtonDataRef::setId (XPWidgetID _me, const char* dataRefName)
{
    TFButtonWidget::setId(_me);             // hook into message loop
    TFDataRefLink::setDataRef(dataRefName); // link to dataRef
    Synch();                     // read current value
}

// reads current data ref value and sets button state accordingly
void TFButtonDataRef::Synch()
{
    if (getId() && isValid())
        SetChecked(GetInt() != 0);
}

// changes data ref in response to button state
bool TFButtonDataRef::MsgButtonStateChanged (XPWidgetID buttonWidget,
                                             bool bNowChecked)
{
    // handle message also in class hierarchy
    TFButtonWidget::MsgButtonStateChanged(buttonWidget, bNowChecked);

    // set dataRef accordingly, message is handled
    Set(bNowChecked ? 1 : 0);
    return true;
}

// every second sync the button's status with dataRef's reality
bool TFButtonDataRef::TfwMsgMain1sTime ()
{
    TFButtonWidget::TfwMsgMain1sTime();     // call base class, too
    Synch();                     // synch button state
    return true;                            // msg processed
}

//
// MARK: TFTextFieldWidget
//

// select text
void TFTextFieldWidget::SetSelection (int startPos, int endPos)
{
    SetProperty(xpProperty_EditFieldSelStart, startPos);
    SetProperty(xpProperty_EditFieldSelEnd, endPos);
}

// handle <Return>/<tab> specifically: end of entry, send "text changed", loose keyboard focus
// also handle <Esc> by reverting to the original descriptor
bool TFTextFieldWidget::MsgKeyPress (XPKeyState_t& key)
{
    // handle and eat [Return]/[Tab]
    if ((key.flags & xplm_DownFlag) &&      // 'key down' flag
        (key.key == XPLM_KEY_RETURN))       // key is 'return'
    {
        // in case of [Return] lose focus, MsgKeyLoseFocus handles notification
        LoseKeyboardFocus();
        return true;
    }
         
    // handle and eat [Esc]: Revert entry to previous value
    if ((key.flags & xplm_DownFlag) &&      // 'key down' flag
        (key.key == XPLM_KEY_ESCAPE))       // key is 'Esc'
    {
        // revert descriptor to original value and lose focus
        SetDescriptor(oldDescriptor);
        LoseKeyboardFocus();
        return true;
    }

    // *** Format validation ***
    
    // keys with virtual key codes before '0' are usualy navigation keys
    // we just let them pass no matter what formatting says
    if (key.vkey < XPLM_VK_0)
        return false;
        
    // in case we shall force upper case we do so
    if (tfFormat == TFF_UPPER_CASE || tfFormat == TFF_HEX)
    {
        // is the to-upper-converted key different?
        if (toupper(key.key) != key.key) {
            key.key = (char)toupper(key.key);     // we change the message
            key.flags |= xplm_ShiftFlag;    // and simulate upper case
        }
    }
    
    // hex characters only?
    if (tfFormat == TFF_HEX)
    {
        // if key is not one of the hex characters
        // we ignore the pressed key by eating the message without processing
        if ((key.key < '0' || key.key > '9') &&
            (key.key < 'A' || key.key > 'F'))
            return true;
    }
    
    // digits only?
    if (tfFormat == TFF_DIGITS)
    {
        // if key is not one of the digits
        // we ignore the pressed key by eating the message without processing
        if (key.key < '0' || key.key > '9')
            return true;
    }
    
    // we don't eat the message but leave it to others for processing
    return false;
}

// we lose focus: save what the user entered
bool TFTextFieldWidget::MsgKeyLoseFocus (bool bTakenByOtherWidget)
{
    // give class hierarchy a chance
    bool ret = TFWidget::MsgKeyLoseFocus(bTakenByOtherWidget);
    
    // did the text change?
    if (oldDescriptor != GetDescriptor()) {
        // inform window about change
        XPSendMessageToWidget(*this, xpMsg_TextFieldChanged, xpMode_UpChain,
                              intptr_t(getId()), NULL);
        return true;
    }
    
    return ret;
}


bool TFTextFieldWidget::MsgKeyTakeFocus (bool bChildGaveUp)
{
    // user is about to change the value...we save it in case of [Esc]
    oldDescriptor = GetDescriptor();
    // give class hierarchie a chance
    TFWidget::MsgKeyTakeFocus(bChildGaveUp);
    // we are receiving focus -> select the entire text to ease entering a new one
    SetSelection(0,int(oldDescriptor.size()));
    return true;
}

//
// MARK: TFIntFieldDataRef
//
TFIntFieldDataRef::TFIntFieldDataRef (XPWidgetID _me,
                                      const char* dataRefName,
                                      TFTextFieldFormatTy format) :
TFTextFieldWidget(_me), TFDataRefLink(dataRefName)
{
    assert(format == TFF_DIGITS || format == TFF_HEX);      // only integer values allowed
    tfFormat = format;
    Synch();
}

// (deferred) initialization
void TFIntFieldDataRef::setId (XPWidgetID _me,
                               const char* dataRefName,
                               TFTextFieldFormatTy format)
{
    assert(format == TFF_DIGITS || format == TFF_HEX);      // only integer values allowed
    tfFormat = format;
    TFTextFieldWidget::setId(_me);          // hook into message loop
    TFDataRefLink::setDataRef(dataRefName); // link to dataRef
    Synch();                                // read current value
}

// set the current value by first setting the dataRef and then synching
void TFIntFieldDataRef::Set (int val)
{
    TFDataRefLink::Set(val);
    Synch();
}

// reads current data ref value and sets field value accordingly
void TFIntFieldDataRef::Synch()
{
    if (getId() && isValid()) {
        if (tfFormat == TFF_DIGITS)
            // decimal
            SetDescriptor(std::to_string(GetInt()));
        else {
            // hex representation, filled with 0 as to text fields size
            char s[25];
            int l = (int)XPGetWidgetProperty(*this, xpProperty_MaxCharacters, NULL);
            snprintf (s, sizeof(s), "%0*X", l, GetInt());
            SetDescriptor(s);
        }
    }
}

// changes data ref in response to field's change
bool TFIntFieldDataRef::MsgTextFieldChanged (XPWidgetID textWidget,
                                             std::string text)
{
    // handle message also in class hierarchy
    TFTextFieldWidget::MsgTextFieldChanged(textWidget, text);
    
    // set dataRef accordingly, message is handled
    try {
        Set(tfFormat == TFF_DIGITS ? std::stoi(text) :
            std::stoi(text, nullptr, 16));
    }
    catch (const std::invalid_argument&) {
        // just ignore conversion exceptions, Synch() will handle it
    }
    catch (const std::out_of_range&) {
        // just ignore conversion exceptions, Synch() will handle it
    }

    Synch();                        // setting data ref causes validation after which value might have change, so: re-synch
    return true;
}

// every second sync the field's value with dataRef's reality
bool TFIntFieldDataRef::TfwMsgMain1sTime ()
{
    TFTextFieldWidget::TfwMsgMain1sTime();
    if (!HaveKeyboardFocus())               // don't overwrite while user is editing
        Synch();
    return true;
}


//
// MARK: TFButtonGroup
//

TFButtonGroup::TFButtonGroup (std::initializer_list<XPWidgetID> _group)
{
    Add (_group);
}

void TFButtonGroup::Add (std::initializer_list<XPWidgetID> _group)
{
    XPWidgetID checkId = GetChecked();  // which widget is checked/active? (only one allowed)
    
    // add widgets and make sure we have each widget just once
    for (XPWidgetID id: _group) {
        if (std::find(group.cbegin(), group.cend(), id) == group.cend())
            group.push_back(id);
    }
    
    // make sure at max one of the buttons is checked
    if (!checkId)
        checkId = GetChecked();
    SetChecked(checkId);
}

// verifies group members
bool TFButtonGroup::isInGroup (XPWidgetID id) const
{
    return std::find(group.cbegin(), group.cend(), id) != group.cend();
}

// return the first checked button's widget id
XPWidgetID TFButtonGroup::GetChecked() const
{
    for (XPWidgetID id: group) {
        if (XPGetWidgetProperty(id, xpProperty_ButtonState, NULL) != 0)
            return id;
    }
    // no checked button found
    return NULL;
}

// return the index of the first checked button
int TFButtonGroup::GetCheckedIndex() const
{
    int i=0;
    for (XPWidgetID id: group) {
        if (XPGetWidgetProperty(id, xpProperty_ButtonState, NULL) != 0)
            return i;
        i++;
    }
    // no checked button found
    return -1;
}

// return the checked button's descriptor
std::string TFButtonGroup::GetDescriptor () const
{
    XPWidgetID id = GetChecked();
    if (id)
        return TFGetWidgetDescriptor(id);
    else
        return std::string();
}

// check one button, uncheck the others, send messages for changes
void TFButtonGroup::SetChecked (XPWidgetID idChecked)
{
    for (XPWidgetID id: group) {
        bool bIsNowChecked = XPGetWidgetProperty(id, xpProperty_ButtonState, NULL) != 0;
        if (id == idChecked) {
            if (!bIsNowChecked) {
                // is not checked but shall be: set it checked and inform widgets about state change
                // (this is not executed at all if SetChecked is called from MsgButtonStateChanged,
                //  as in that case the new button is checked already)
                XPSetWidgetProperty(id, xpProperty_ButtonState, 1);
                XPSendMessageToWidget(id, xpMsg_ButtonStateChanged, xpMode_UpChain, intptr_t(id), intptr_t(1));
            }
        } else {
            if (bIsNowChecked) {
                // is checked but shall no longer be: set it unchecked and inform widgets about state change
                XPSetWidgetProperty(id, xpProperty_ButtonState, 0);
                XPSendMessageToWidget(id, xpMsg_ButtonStateChanged, xpMode_UpChain, intptr_t(id), NULL);
            }
        }
    } // for each widget
}

// check the given button, uncheck the others, send messages for changes
void TFButtonGroup::SetCheckedIndex (int i)
{
    // sanity checks
    if (0 <= i && i < group.size())
        SetChecked(group[i]);
}

// 'handles' the message about a now checked button IF AND ONLY IF the widget is part of the group
bool TFButtonGroup::MsgButtonStateChanged (XPWidgetID buttonWidget, bool bNowChecked)
{
    if (bNowChecked && isInGroup(buttonWidget)) {
        SetChecked(buttonWidget);
        return true;
    }
    // not found in group -> not handled
    return false;
}

//
// MARK: TFMainWindowWidget
//

TFMainWindowWidget::TFMainWindowWidget (XPWidgetID _me) :
TFWidget(_me), bTimerRunning(false)
{
    // register the callback for the 1s timer, but don't activate yet
    XPLMRegisterFlightLoopCallback(CB1sTimer, 0, this);
}

TFMainWindowWidget::~TFMainWindowWidget ()
{
    // unregister 1s timer callback
    XPLMUnregisterFlightLoopCallback(CB1sTimer, this);
}

// dispatch main windows-specific messages, pass others up the hierarchy
bool TFMainWindowWidget::HandleMessage (XPWidgetMessage    inMessage,
                                       intptr_t           inParam1,
                                       intptr_t           inParam2)
{
    // catch some messages specifically
    switch (inMessage) {
        // window's close button
        case xpMessage_CloseButtonPushed:
            return MessageCloseButtonPushed();

        // otherwise call base class
        default:
            return TFWidget::HandleMessage(inMessage, inParam1, inParam2);
    }
}

bool TFMainWindowWidget::MsgShown (XPWidgetID shownWidget)
{
    // if it is not me just do what the class hierarchy does (problably nothing)
    if (shownWidget != *this)
        return TFWidget::MsgShown(shownWidget);
    
    // tell all children we got shown
    XPSendMessageToWidget(shownWidget, TFW_MSG_MAIN_SHOWHIDE, xpMode_Recursive,
                          intptr_t(shownWidget), intptr_t(true));
    
    // start the 1s trigger messages
    StartStopTimerMessages(true);
    
    return true;
}

bool TFMainWindowWidget::MsgHidden (XPWidgetID hiddenWidget)
{
    // if it is not me just do what the class hierarchy does (problably nothing)
    if (hiddenWidget != *this)
        return TFWidget::MsgHidden(hiddenWidget);
    
    // stop the 1s trigger messages
    StartStopTimerMessages(false);
    
    // tell all children we got shown
    XPSendMessageToWidget(hiddenWidget, TFW_MSG_MAIN_SHOWHIDE, xpMode_Recursive,
                          intptr_t(hiddenWidget), intptr_t(false));
    return true;
}


// reshape, specifically: move, if I am being moved also move all my children
// NOTE: The standard XP implementation of a MainWindow is buggy as it
//       moves only direct children but no lower levels. This implementation does.
bool TFMainWindowWidget::MsgReshape (XPWidgetID originId, const XPWidgetGeometryChange_t& geoChange)
{
    // not me who got moved? -> call base class
    if (originId != *this)
        return TFWidget::MsgReshape (originId, geoChange);
    
    // I got moved! Move my children
    std::forward_list<XPWidgetID> kids;
    // add direct children to the list of kids as a start
    int c = XPCountChildWidgets(*this);
    for (int i = 0; i < c; i++)
        kids.push_front(XPGetNthChildWidget(*this, i));
    
    // work on the list
    while (!kids.empty()) {
        // move the widget
        XPWidgetID id = kids.front();
        XPUMoveWidgetBy(id, geoChange.dx, geoChange.dy);
        kids.pop_front();
        
        // add the widget's children to the list to work on them next
        c = XPCountChildWidgets(id);
        for (int i = 0; i < c; i++)
            kids.push_front(XPGetNthChildWidget(id, i));
    }
    
    // message handled (specifically also: don't let XP standard implementation deal with it any longer)
    return true;
}

// handle the [Tab] key: move keybard focus to the first/next editable widget
bool TFMainWindowWidget::MsgKeyPress (XPKeyState_t& key)
{
    // handle and eat [Tab]
    if ((key.flags & xplm_DownFlag) &&      // 'key down' flag
         key.key == XPLM_KEY_TAB)           // key is 'tab'
    {
        // who has the keyboard focus right now?
        XPWidgetID oldFocus = XPGetWidgetWithFocus();
        if (!oldFocus ||                    // X-Plane has focus?
            XPFindRootWidget(oldFocus) != *this)    // other main window has focus? (how did I get this message then???)
            return false;

        // one of our children has the focus
        // now find the next one in line within the subwindow
        XPWidgetID parent = XPGetParentWidget(oldFocus);
        int oldIdx = TFGetWidgetChildIndex(oldFocus);
        int numSibl = XPCountChildWidgets(parent);
        
        // try siblings after oldFocus, wrap around at end of list of children of subwindow
        int i = (key.flags & xplm_ShiftFlag) ?
        (oldIdx <= 0 ? numSibl-1 : oldIdx-1) :      // in case of [Shift+Tab] we search backward
        (oldIdx+1 >= numSibl ? 0 : oldIdx+1);       // else forward
        
        for (XPWidgetID id = XPGetNthChildWidget(parent, i);
             i != oldIdx;
             id = XPGetNthChildWidget(parent, i))
        {
            // we just set the focus and see if the widget accepts it
            // if not we try the next sibling
            XPWidgetID retId = XPSetKeyboardFocus(id);
            if (retId == id)                // widget accepted focus!
                return true;                // eat [Tab]
            // try next index (wrap around at end of children list)
            if (key.flags & xplm_ShiftFlag) {   // backward search?
                if (--i < 0)
                    i = numSibl-1;
            } else {
                if (++i >= numSibl)
                    i = 0;
            }
        }
        
        // found no one to accept the focus, little hack here:
        // we briefly take away the focus and set it again
        // to trigger the field's take/lose focus functionality
        LoseKeyboardFocus();
        XPSetKeyboardFocus(oldFocus);
    }
    
    // message not handled
    return false;
}

// hide window on close button
bool TFMainWindowWidget::MessageCloseButtonPushed ()
{
    Show(false);
    return true;
}

// handle button groups (for radio-button-group-like behaviours)
bool TFMainWindowWidget::MsgButtonStateChanged (XPWidgetID btnId, bool bNowChecked)
{
    // loop over known button groups and see if any is able to handle the state change
    for (TFButtonGroup* pBtnGrp: lstBtnGrp) {
        if (pBtnGrp->MsgButtonStateChanged (btnId, bNowChecked))
            return true;
    }
    
    // no handling group found, message not processed
    return false;
}

// add a button group definition
void TFMainWindowWidget::HookButtonGroup (TFButtonGroup& btnGrp)
{
    if (std::find(lstBtnGrp.cbegin(), lstBtnGrp.cend(), &btnGrp) == lstBtnGrp.cend())
        lstBtnGrp.push_front(&btnGrp);
}

// removes a group definition
void TFMainWindowWidget::UnhookButtonGroup (TFButtonGroup& btnGrp)
{
    lstBtnGrp.remove(&btnGrp);
}

// start or stop the 1s trigger messages
void TFMainWindowWidget::StartStopTimerMessages (bool bStart)
{
    // short-cut: no change
    if (bStart == bTimerRunning)
        return;
    
    // start or stop the timer
    XPLMSetFlightLoopCallbackInterval(CB1sTimer,
                                      float(bStart ? TFW_TIMER_INTVL : 0),
                                      true,
                                      this);
    bTimerRunning ^= true;          // toggle running-flag
}

// callback for flight loop: called every second, sends TFW_MSG_MAIN_1S_TIMER message to all widgets
float TFMainWindowWidget::CB1sTimer (float, float, int, void* refcon)
{
    TFMainWindowWidget* pMainWnd = reinterpret_cast<TFMainWindowWidget*>(refcon);
    if (pMainWnd)
        XPSendMessageToWidget(*pMainWnd,
                              TFW_MSG_MAIN_1S_TIMER,
                              xpMode_Recursive,
                              NULL, NULL);
    return TFW_TIMER_INTVL;
}

//
// MARK: TFDataRefLink
//

TFDataRefLink::TFDataRefLink (const char* dataRefName) :
ref(NULL),
dataType(DT_UNKNOWN)
{
    if (dataRefName)
        setDataRef(dataRefName);
}

// link to data ref, determine supported data type, inform initial value
bool TFDataRefLink::setDataRef (const char* dataRefName)
{
    ref = XPLMFindDataRef(dataRefName);
    if (isValid()) {
        XPLMDataTypeID dtId = XPLMGetDataRefTypes(ref);
        // we decide for the most detailed data type
        if      (dtId & xplmType_Double)    { dataType = DT_DOUBLE; }
        else if (dtId & xplmType_Float)     { dataType = DT_FLOAT;  }
        else if (dtId & xplmType_Int)       { dataType = DT_INT;    }
    }
    
    return isValid();
}

