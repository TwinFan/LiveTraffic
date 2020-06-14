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

/// Standard window title (if yet empty)
#define ACI_WND_TITLE   "A/C Info"

//
// A/C Info Main Window
//
class ACIWnd : public LTImgWindow
{
protected:
    // What's currently valid?
    LTFlightData::FDKeyTy   acKey;  ///< key of a/c to be displayed
    bool bAuto = false;             ///< currently in AUTO mode?
    // Last static and dynamic data
    LTFlightData::FDStaticData stat;
    LTFlightData::FDDynamicData dyn;
    
    // Temporary user input
    std::string keyEntry;           ///< what user is currently entering
    
    /// When did we check for an update of the AUTO a/c last? (in XP network time)
    float lastAutoCheck = 0.0f;
    
    /// Nearest airport
    std::string nearestAirport;
    positionTy  nearestAirportPos;
    float       lastNearestAirportCheck = 0.0f;

public:
    /// Constructor shows a window for the given a/c key
    /// @param _acKey (optional) specifies a search text to find an a/c, if empty -> AUTO mode
    /// @param _mode (optional) window mode, defaults to "float or VR"
    ACIWnd(const std::string& _acKey = "",
           WndMode _mode = WND_MODE_FLOAT_OR_VR);
    /// Desctructor cleans up
    ~ACIWnd() override;
    
    /// Get current a/c key. This is the currently valid key (not any temporary user entry)
    const LTFlightData::FDKeyTy& GetAcKey () const { return acKey; }
    /// Set the a/c key - no validation, if invalid window will clear
    void SetAcKey (const LTFlightData::FDKeyTy& _key);
    /// Clear the a/c key, ie. display no data
    void ClearAcKey ();
    /// Is in AUTO mode?
    bool IsAuto () const { return bAuto; }
    /// Set AUTO mode
    void SetAuto (bool _b);
    /// Return the text to be used as window title
    std::string GetWndTitle () const;

protected:
    /// Taking user's temporary input `keyEntry` searches for a valid a/c, sets acKey on success
    bool SearchAndSetFlightData ();
    /// @brief using `acKey` returns the actual a/c data
    LTFlightData* GetFlightData () const;
    /// switch to another focus a/c?
    bool UpdateFocusAc ();
    /// periodically find the nearest airport and return a nice position string relative to it
    std::string RelativePositionText (const positionTy& pos);
    
    /// Some setup before UI building starts, here text size calculations
    ImGuiWindowFlags_ beforeBegin() override;
    /// Main function to render the window's interface
    void buildInterface() override;
    /// Add a label to the list of a/c info
    void buildRowLabel (const std::string& label);
    /// Add a label and a value to the list of a/c info
    void buildRow (const std::string& label,
                   const std::string& val,
                   bool bShowVal);
    /// Add a label and a value to the list of a/c info
    void buildRow (const std::string& label,
                   bool bShowVal,
                   const char* szFormat, ...) LT_FMTARGS(4);
    /// Add a label and a value to the list of a/c info
    void buildRow (const std::string& label,
                   int iVal, bool bShowVal,
                   const char* szFormat = "%d");
    /// Add a label and a value to the list of a/c info
    void buildRow (const std::string& label,
                   double fVal, bool bShowVal,
                   const char* szFormat = "%.1f");

    // A set of static functions to create/administer the windows
protected:
    /// Font scaling factor for ACI Windows
    static float fFontScale;
    /// Transparency level for ACI Windows
    static float fTransparency;
    /// are the ACI windows displayed or hidden?
    static bool bAreShown;
    /// list of all ACI windows currently displayed
    static std::list<ACIWnd*> listACIWnd;
public:
    /// @brief Create a new A/C Info window
    /// @param _acKey (optional) specifies a search text to find an a/c, if empty -> AUTO mode
    /// @param _mode (optional) window mode, defaults to "float(centered) or VR"
    /// @return pointer to the newly created window
    static ACIWnd* OpenNewWnd (const std::string& _acKey = "",
                               WndMode _mode = WND_MODE_FLOAT_CNT_VR);
    // move all windows into/out of VR
    static void MoveAllVR (bool bIntoVR);
    // hide/show all windows, returns new state
    static bool ToggleHideShowAll();
    static bool AreShown() { return bAreShown; }
    static void CloseAll();
    
};

#endif /* ACInfoWnd_h */
