/// @file       SettingsUI.h
/// @brief      Defines the Settings window
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

#ifndef SettingsUI_h
#define SettingsUI_h

//
// Settings UI Main window
//

/// Settings dialog
class LTSettingsUI : public LTImgWindow
{
protected:
    /// Search filter
    char sFilter[50] = {0};
    /// Is ADSBEx key displayed clear text?
    bool bADSBExKeyClearText = false;
    enum {
        ADSBX_KEY_NO_ACTION = 0,    ///< no key test currently happening
        ADSBX_KEY_TESTING,          ///< key test underway
        ADSBX_KEY_FAILED,           ///< key test ended with failure
        ADSBX_KEY_SUCCESS,          ///< key test succeeded
    } eADSBExKeyTest = ADSBX_KEY_NO_ACTION;
    std::string sADSBExKeyEntry;    ///< current ADSBEx key entry
    // Debug options
    std::string txtDebugFilter;     ///< filter for single aircraft
    std::string txtFixAcType;       ///< fixed aircraft type
    std::string txtFixOp;           ///< fixed operator
    std::string txtFixLivery;       ///< fixed livery
public:
    /// Constructor creates and displays the window
    LTSettingsUI();
    /// Destructor completely removes the window
    ~LTSettingsUI() override;

protected:
    /// Some setup before UI building starts, here text size calculations
    ImGuiWindowFlags_ beforeBegin() override;
    /// Main function to render the window's interface
    void buildInterface() override;

    /*
    /// Set CSL label configuration
    void LabelBtnSave();
    
    // Save CSL path / Load a CSL package
    void SaveCSLPath(int idx);*/
    
public:
    /// @brief Creates/opens/displays/hides/closes the settings window
    /// @param _force 0 - toggle, <0 force close, >0 force open
    /// @return Is displayed now?
    static bool ToggleDisplay (int _force = 0);
    /// Is the settings window currently displayed?
    static bool IsDisplayed ();
};

/// Shows the settings dialog

#endif /* SettingsUI_h */
