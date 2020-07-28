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
    enum ILWTabTy { ILW_TAB_NONE=0, ILW_TAB_AC_LIST, ILW_TAB_MSG, ILW_TAB_STATUS };
    ILWTabTy    activeTab = ILW_TAB_NONE;
    std::string wndTitle;           ///< current window title, contains opened tab

public:
    /// Constructor shows the window
    /// @param _mode (optional) window mode, defaults to "float or VR"
    InfoListWnd(WndMode _mode = WND_MODE_FLOAT_OR_VR);
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
