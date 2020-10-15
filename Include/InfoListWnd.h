/// @file       InfoListWnd.h
/// @brief      Window listing aircraft, messages, and status information
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

#ifndef InfoListWnd_h
#define InfoListWnd_h

//
// Info List Window
//
class InfoListWnd : public LTImgWindow
{
protected:
    enum ILWTabTy { ILW_TAB_NONE=0, ILW_TAB_AC_LIST, ILW_TAB_MSG, ILW_TAB_STATUS, ILW_TAB_SETTINGS };
    ILWTabTy    activeTab = ILW_TAB_NONE;
    std::string wndTitle;           ///< current window title, contains opened tab

    // A/c list
    ACTable acList;                 ///< represents the aircraft list on the first tab
    char sAcFilter[50] = {0};       ///< aircraft search filter
    
    // Message list
    unsigned int msgLvlFilter = 63; ///< which log levels to show? (default: all)
    char sMsgFilter[50] = {0};      ///< messages search filter
    LogMsgIterListTy msgIterList;   ///< list of msg to show
    /// counter of last displayed message (to determine if new msg have been added)
    unsigned long msgCounterLastDisp = 0;
    /// counter of last message (to determine if msgs have been deleted, which invalidates iterators)
    unsigned long msgCounterEnd = 0;
    
    // Info/Status
    float lastStatusUpdate = 0.0f;  ///< when last updates periodic status info?
    std::string verText;            ///< version information
    std::string aiCtrlPlugin;       ///< name of plugin controlling AI planes
    float weatherHPA = HPA_STANDARD;///< Weather: QNH
    std::string weatherStationId;   ///< Weather: reporting station
    std::string weatherMETAR;       ///< Weather: full METAR

public:
    /// Constructor shows the window
    /// @param _mode (optional) window mode, defaults to "float or VR"
    InfoListWnd(WndMode _mode = WND_MODE_FLOAT_CNT_VR);
    /// Desctructor cleans up
    ~InfoListWnd() override;
    
    /// Redefine the window title based on the (now) active tab
    void TabActive (ILWTabTy _tab);
    /// Return the text to be used as window title
    std::string GetWndTitle () const { return wndTitle; }
    
protected:
    /// Some setup before UI building starts, here text size calculations
    ImGuiWindowFlags_ beforeBegin() override;
    /// Main function to render the window's interface
    void buildInterface() override;

    // A set of static functions to create/administer the windows
public:
    /// @brief Creates/opens/displays/hides/closes the info list window
    /// @param _force 0 - toggle, <0 force close, >0 force open
    /// @return Is displayed now?
    static bool ToggleDisplay (int _force = 0);
    /// Is the settings window currently displayed?
    static bool IsDisplayed ();
};

#endif /* InfoListWnd_h */
