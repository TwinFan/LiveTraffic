/// @file       LTImgWindow.cpp
/// @brief      LiveTraffic-specific enhancements to ImGui / ImgWindow
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

#include "LiveTraffic.h"

#define FATAL_COULD_NOT_LOAD_FONT "Could not load GUI font %s"
#define WARN_NOT_LOADED_ICON_FONT "Could not load icon font, icons will not be displayed properly"

//
// MARK: LiveTraffic Configuration dataRef access
//

/// dataRef handles for the configuration dataRefs, will be filled lazily when needed
static XPLMDataRef gDR[CNT_DATAREFS_LT];

/// Lazily fetches the dataRef handle, then returns its current value
int cfgGet (dataRefsLT idx)
{
    LOG_ASSERT(0 <= idx && idx < CNT_DATAREFS_LT);
    if (!gDR[idx])
        gDR[idx] = XPLMFindDataRef(DATA_REFS_LT[idx]);
    return gDR[idx] ? XPLMGetDatai(gDR[idx]) : 0;
}

/// Lazily fetches the dataRef handle, then sets integer value
void cfgSet (dataRefsLT idx, int v)
{
    LOG_ASSERT(0 <= idx && idx < CNT_DATAREFS_LT);
    if (!gDR[idx])
        gDR[idx] = XPLMFindDataRef(DATA_REFS_LT[idx]);
    if (gDR[idx]) XPLMSetDatai(gDR[idx], v);
}

//
// MARK: ImGui extensions
//

/// Text color of button symbols
constexpr ImU32 LTIM_BTN_COL = IM_COL32(0xB0, 0xB0, 0xB0, 0xFF);    ///< medium gray for regular icon buttons

namespace ImGui {

/// width of a single-icon button
static float gWidthIconBtn = NAN;

// Get width of an icon button (calculate on first use)
IMGUI_API float GetWidthIconBtn (bool _bWithSpacing)
{
    if (std::isnan(gWidthIconBtn))
        gWidthIconBtn = CalcTextSize(ICON_FA_WINDOW_MAXIMIZE).x;
    if (_bWithSpacing)
        return gWidthIconBtn + ImGui::GetStyle().ItemSpacing.x;
    else
        return gWidthIconBtn;
}

// Helper for creating unique IDs
IMGUI_API void PushID_formatted(const char* format, ...)
{
    // format the variable string
    va_list args;
    char sz[500];
    va_start (args, format);
    vsnprintf(sz, sizeof(sz), format, args);
    va_end (args);
    // Call the actual push function
    PushID(sz);
}

// Outputs aligned text
IMGUI_API void TextAligned (AlignTy _align, const std::string& s)
{
    // Change cursor position so that text becomes aligned
    if (_align != IM_ALIGN_LEFT) {
        ImVec2 avail = GetContentRegionAvail();
        ImVec2 pos = GetCursorPos();
        const float txtWidth = CalcTextSize(s.c_str()).x;
        if (_align == IM_ALIGN_CENTER)
            pos.x += avail.x/2.0f - txtWidth/2.0f;
        else
            pos.x += avail.x - txtWidth - ImGui::GetStyle().ItemSpacing.x;
        SetCursorPos(pos);
    }
    // Output the text
    TextUnformatted(s.c_str());
}

// Small Button with on-hover popup helper text
IMGUI_API bool SmallButtonTooltip(const char* label,
                                  const char* tip,
                                  ImU32 colFg,
                                  ImU32 colBg)
{
    // Setup colors
    if (colFg != IM_COL32(1,1,1,0))
        PushStyleColor(ImGuiCol_Text, colFg);
    if (colBg != IM_COL32(1,1,1,0))
        PushStyleColor(ImGuiCol_Button, colBg);

    // do the button (and we _really_ using also x frame padding = 0)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f,0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f,0.0f));
    bool b = SmallButton(label);
    ImGui::PopStyleVar(2);
    
    // restore previous colors
    if (colBg != IM_COL32(1,1,1,0))
        PopStyleColor();
    if (colFg != IM_COL32(1,1,1,0))
        PopStyleColor();

    // do the tooltip
    if (tip && IsItemHovered())
        SetTooltip("%s", tip);
    
    // return if button pressed
    return b;
}

// Button with on-hover popup helper text
IMGUI_API bool ButtonTooltip(const char* label,
                             const char* tip,
                             ImU32 colFg,
                             ImU32 colBg,
                             const ImVec2& size)
{
    // Setup colors
    if (colFg != IM_COL32(1,1,1,0))
        PushStyleColor(ImGuiCol_Text, colFg);
    if (colBg != IM_COL32(1,1,1,0))
        PushStyleColor(ImGuiCol_Button, colBg);

    // do the button
    bool b = Button(label, size);
    
    // restore previous colors
    if (colBg != IM_COL32(1,1,1,0))
        PopStyleColor();
    if (colFg != IM_COL32(1,1,1,0))
        PopStyleColor();

    // do the tooltip
    if (tip && IsItemHovered())
        SetTooltip("%s", tip);
    
    // return if button pressed
    return b;
}

IMGUI_API bool ButtonIcon(const char* label, const char* tooltip, bool rightAligned)
{
    // Setup colors for window sizing buttons
    PushStyleColor(ImGuiCol_Text, LTIM_BTN_COL);                                         // very light gray
    PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);                               // transparent
    PushStyleColor(ImGuiCol_ButtonHovered, GetColorU32(ImGuiCol_ScrollbarGrab));  // gray

    if (rightAligned)
        SetCursorPosX(GetWindowContentRegionMax().x - GetWidthIconBtn() - ImGui::GetStyle().ItemSpacing.x);

    bool b = ButtonTooltip(label, tooltip);

    // Restore colors
    PopStyleColor(3);
    
    return b;
}

// Button which opens the given URL
IMGUI_API bool ButtonURL(const char* label,
                         const char* url,
                         const char* tip,
                         bool bSmallBtn,
                         ImU32 colFg,
                         ImU32 colBg,
                         const ImVec2& size)
{
    if ((!bSmallBtn && ButtonTooltip(label, tip ? tip : url, colFg, colBg, size)) ||
        ( bSmallBtn && SmallButtonTooltip(label, tip ? tip : url, colFg, colBg))) {
        LTOpenURL(url);
        return true;
    } else
        return false;
}

// Selectable, which turns green when selected
IMGUI_API bool SelectableTooltip(const char* label,
                                 bool* p_selected,
                                 bool bEnabled,
                                 const char* tip,
                                 ImGuiSelectableFlags flags,
                                 const ImVec2& size)
{
    // Turn background green when selected
    // do the selectable and remember the return value
    if (!bEnabled)
        flags |= ImGuiSelectableFlags_Disabled;
    const bool bRet = ImGui::Selectable(label, p_selected,flags, size);
    // if given do a tooltip
    if (tip && IsItemHovered())
        SetTooltip("%s", tip);
    // return if it was just clicked
    return bRet;
}

// Selectable, which turns green when selected
IMGUI_API bool SelectableTooltip(const char* label,
                                 bool bSelected,
                                 bool bEnabled,
                                 const char* tip,
                                 ImGuiSelectableFlags flags,
                                 const ImVec2& size)
{
    return SelectableTooltip(label, &bSelected, bEnabled, tip, flags, size);
}

IMGUI_API bool CheckboxDr(const char* label, dataRefsLT idx, const char* tooltip)
{
    // Show the checkbox
    bool bV = idx >= 0 ? cfgGet(idx) : false;
    const bool bRet = Checkbox(label, &bV);
    
    // do the tooltip
    if (tooltip && IsItemHovered())
        SetTooltip("%s", tooltip);
    
    // Process a changed value
    if (bRet && idx >= 0) {
        cfgSet(idx, bV);                // set dataRef value
        return true;
    } else
        return false;
    

}

// Same as SliderFloat, but display is in percent, so values are expected to be around 1.0 to be displayed as 100%
IMGUI_API bool SliderPercent(const char* label, float* v, float v_min, float v_max, const char* format, float power)
{
    float f = *v * 100.0f;              // "convert" to percent
    bool bRet = SliderFloat(label, &f, v_min*100.0f, v_max*100.0f, format, power);
    *v = f/100.0f;
    return bRet;
}

// Integer Slider, which reads from/writes to a defined dataRef
IMGUI_API bool SliderDr(const char* label, dataRefsLT idx,
                        int v_min, int v_max, int v_step,
                        const char* format)
{
    int iV = cfgGet(idx);
    SetNextItemWidth(GetContentRegionAvail().x);            // Slider is otherwise calculated too large, so we help here a bit
    if (SliderInt(label, &iV, v_min, v_max, format)) {      // if slider changed value
        // rounding to full steps
        if (v_step > 1)
            iV = (iV+(v_step/2))/v_step * v_step;
        // When entering manually [Ctrl+Click], values aren't clamped, so we take care of it
        cfgSet(idx, std::clamp<int>(iV, v_min, v_max));     // set dataRef value
        return true;
    }
    else
        return false;
}


// Same as DragFloat, but display is in percent, so values are expected to be around 1.0 to be displayed as 100%
IMGUI_API bool DragPercent(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, float power)
{
    float f = *v * 100.0f;              // "convert" to percent
    bool bRet = DragFloat(label, &f, v_speed*100.0f, v_min*100.0f, v_max*100.0f, format, power);
    *v = f/100.0f;
    return bRet;
}

// MARK: Complex functions across table cells

// Draws a tree node, a checkbox + an URL button, and a help icon button
IMGUI_API bool TreeNodeCbxLinkHelp(const char* label, int nCol,
                                   dataRefsLT idxCbx, const char* cbxPopup,
                                   const char* linkLabel, const char* linkURL, const char* linkPopup,
                                   const char* helpURL, const char* helpPopup,
                                   const char* filter, int nOpCl,
                                   ImGuiTreeNodeFlags flags)
{
    if (filter && *filter) return true;                     // is a filter defined? Then we don't show tree nodes
    
    if (TableGetColumnIndex() > 0)                          // make sure we start on a row's beginning
        TableNextRow();

    // Set special background color for tree nodes
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImGui::GetColorU32(ImGuiCol_Button, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg));

    // makes sure that all buttons here are recongized as different
    PushID(label);
    
    // Tree node
    if (nOpCl)                                              // if requested...
        SetNextItemOpen(nOpCl > 0);                         // ...force open/close
    const bool b = TreeNodeEx(label, flags);                // draw tree node
    
    // add a checkbox
    TableNextCell();
    if (idxCbx < CNT_DATAREFS_LT) {
        CheckboxDr("##CheckboxDr", idxCbx, cbxPopup);
        SameLine();
    }
    
    // add a button with a link action to the 2nd cell
    if (linkLabel && linkURL) {
        ButtonURL(linkLabel, linkURL, linkPopup);
        SameLine();
    }
    // Not a button, but mayby just plain text?
    else if (linkLabel && !linkURL) {
        TextUnformatted(linkLabel);
        if (linkPopup && IsItemHovered())
            SetTooltip("%s", linkPopup);
        SameLine();
    }
    
    // add a help icon button
    if (helpURL) {
        if (TableGetColumnIndex() < nCol-1)                 // move to last column
            TableSetColumnIndex(nCol-1);
        if (ButtonIcon(ICON_FA_QUESTION_CIRCLE, helpPopup, true))
            LTOpenHelp(helpURL);                            // Help button handling
    }
    
    PopID();
    TableNextRow();                                         // move to next row
    ImGui::PopStyleColor(2);                                // Restore row color

    return b;
}


// Show this label only if text matches filter string
IMGUI_API bool FilteredLabel(const char* label, const char* filter,
                             bool bEnabled)
{
    if (filter && *filter)  {           // any filter defined?
        std::string labelUpper(label);
        if (str_toupper(labelUpper).find(filter) == std::string::npos)
            return false;
    }
    
    // Draw the label
    if (bEnabled)
        TextUnformatted(label);
    else
        TextDisabled("%s", label);
    TableNextCell();
    return true;
}

// Filter label plus text input box
IMGUI_API bool FilteredInputText(const char* label, const char* filter,
                                 std::string& s, float width,
                                 const char* hint,
                                 ImGuiInputTextFlags flags)
{
    // Draw label first
    if (!FilteredLabel(label, filter))
        return false;

    // Next cell: Draw the checkbox with a value linked to the dataRef
    PushID(label);
    if (between(width, -0.1f, 0.1f))    // with == 0.0f
        width = GetContentRegionAvail().x - GetWidthIconBtn();
    SetNextItemWidth(width);
    if (hint)
        InputTextWithHint("", hint, &s, flags);
    else
        InputText("", &s, flags);
    bool bRet = IsItemDeactivatedAfterEdit();
    // Add a clear button
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TIMES)) {
        s.clear();
        bRet = true;
    }
    PopID();
    TableNextCell();
    return bRet;
}


// Filter label plus checkbox linked to boolean(integer) dataRef
IMGUI_API bool FilteredCfgCheckbox(const char* label, const char* filter, dataRefsLT idx,
                                   const char* tooltip)
{
    // Draw label first
    if (!FilteredLabel(label, filter))
        return false;

    // Next cell: Draw the checkbox with a value linked to the dataRef
    PushID(label);
    const bool bRet = CheckboxDr("", idx, tooltip);
    PopID();
    TableNextCell();
    return bRet;
}

// Filter label plus checkbox for a bit
IMGUI_API bool FilteredCheckboxFlags(const char* label, const char* filter,
                                     unsigned int* flags, unsigned int flags_value,
                                     const char* tooltip)
{
    // Draw label first
    if (!FilteredLabel(label, filter))
        return false;

    // Next cell: Draw the flag checkbox
    PushID(label);
    const bool bRet = CheckboxFlags("", flags, flags_value);
    if (tooltip && IsItemHovered())
        SetTooltip("%s", tooltip);
    PopID();
    TableNextCell();
    return bRet;
}

// Filter label plus checkbox linked to boolean(integer) dataRef
IMGUI_API bool FilteredRadioButton(const char* label, const char* filter,
                                   int* v, int v_button,
                                   const char* tooltip)
{
    // Draw label first
    if (!FilteredLabel(label, filter))
        return false;

    // Next cell: Draw the flag checkbox
    PushID(label);
    const bool bRet = RadioButton("", v, v_button);
    if (tooltip && IsItemHovered())
        SetTooltip("%s", tooltip);
    PopID();
    TableNextCell();
    return bRet;
}

// Filter label plus integer slider linked to dataRef
IMGUI_API bool FilteredCfgNumber(const char* label, const char* filter, dataRefsLT idx,
                                 int v_min, int v_max, int v_step, const char* format)
{
    // Draw label first
    if (!FilteredLabel(label, filter))
        return false;

    // Next cell: Draw the checkbox with a value linked to the dataRef
    PushID(label);
    const bool bRet = SliderDr("", idx, v_min, v_max, v_step, format);
    PopID();
    TableNextCell();
    return bRet;
}

// @brief Folder selection popup, returns true when done and confirmed
IMGUI_API bool SelectPath (const char* popupId, std::string& path)
{
    static std::string _p ("<uninit>");
    static std::vector<std::string> _l;

    bool ret = false;
    if (BeginPopup(popupId))
    {
        // Replace an empty path with X-Plane's root
        if (path.empty())
            path = dataRefs.GetXPSystemPath();
        // If a path starts with system path, strip the system path
        else {
#if IBM
            // Replace any backslashes with forward slashes as XP seems to do that internally, too
            for (char& c : path)
                if (c == '\\') c = '/';
            // except for the one after the drive letter
            if (path.length() >= 2 && path[1] == ':') {
                // Windows drive letter -> upper case
                path[0] = (char)toupper(path[0]);
                // that slash afterwards needs to be a backslash
                if (path.length() >= 3 && path[2] == '/')
                    path[2] = '\\';
            }
#endif
            LTRemoveXPSystemPath(path);
        }
        
        // make sure path doesn't end on slash
        // (but keep a single slash for "root"...and as the one after a Windows drive letter is kept as backslash it won't be removed either
        if (path.size() > 1 && path.back() == '/')
            path.pop_back();
        
        // make sure we have the proper list of subdirectories
        if (_p != path) {
            _p = path;
            _l = GetDirContents(path,true);
        }
        
        // Can we offer a "dir up" entry?
#if IBM
        if (path.size() > 3 && path[1] == ':')
#else
        if (path != "/")
#endif
        {
            if (Selectable(ICON_FA_LEVEL_UP_ALT)) {
                size_t lastSlash = path.find_last_of(PATH_DELIMS);
                if (lastSlash != std::string::npos)
                    path.erase(std::max(lastSlash, size_t(1)));// one level up, but keep a single slash for root
                else
                    path.clear();                   // nothing left
                ret = true;                         // something selected
            }
        }
        
        // Subdirs of that path, as a Listbox
        for (const std::string& s: _l) {
            if (Selectable(s.c_str())) {
                if (path != "/")
                    path += '/';                // always use forward slash, also on Windows
                path += s;                      // one level down
                ret = true;                     // something selected
            }
        }
        
        EndPopup();
    }
    
    return ret;
}

// Right-aligned indicator for OK/Error
IMGUI_API void Indicator (bool bOK, const char* okText, const char* nokText)
{
    // right-aligned
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::GetWidthIconBtn());
    // for the active line decide based on temporary check
    ImGui::TextUnformatted(bOK ? ICON_FA_CHECK_CIRCLE : ICON_FA_EXCLAMATION_TRIANGLE);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", bOK ? okText : nokText);
}


// Go back to previous cell (same row only)
IMGUI_API bool TablePrevCell()
{
    int col = TableGetColumnIndex();
    if (col < 1)
        return false;
    else
        return TableSetColumnIndex(col-1);
}

}   // namespace ImGui

//
// MARK: LTImgWindow implementation
//

// Constructor
LTImgWindow::LTImgWindow (WndMode _mode, WndStyle _style, WndRect _initPos) :
ImgWindow (_initPos.left(), _initPos.top(),
           _initPos.right(), _initPos.bottom(),
           toDeco(_style), toLayer(_style)),
wndStyle(_style),
rectFloat(_initPos)
{
    // Disable reading/writing of "imgui.ini"
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = IMGUI_INI_PATH;

    // Create a flight loop id
    XPLMCreateFlightLoop_t flDef = {
        sizeof(flDef),                              // structSize
        xplm_FlightLoop_Phase_BeforeFlightModel,    // phase
        cbChangeWndMode,                            // callbackFunc
        (void*)this,                                // refcon
    };
    flChangeWndMode = XPLMCreateFlightLoop(&flDef);
    LOG_ASSERT(flChangeWndMode);

    // For HUD-like windows set a transparent background
    if (wndStyle == WND_STYLE_HUD) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImColor(0, 0, 0, 0x30);
        // Set a drag area (for moving the window by mouse) at the top
        SetWindowDragArea(0, 5, INT_MAX, 5 + 2*WND_FONT_SIZE);
    }
    
    // Set the positioning mode:
    
    // Safeguard position a bit against "out of view" positions
    if (_initPos.left() > 0 || _initPos.bottom()) {
        WndRect screen;
        XPLMGetScreenBoundsGlobal (&screen.left(), &screen.top(),
                                   &screen.right(), &screen.bottom());
        if (!screen.contains(_initPos.tl) ||
            !screen.contains(_initPos.tl.shiftedBy(50, -20)))
        {
            // top left corner is not (sufficiently) inside screen bounds
            // -> make left/botom = 0 so that wnd will be centered
            //    while retaining width/height
            _initPos.shift(- _initPos.left(), - _initPos.bottom());
        }
    }
    
    // If _initPos defines left/bottom already then we don't do centered
    if (_initPos.left() > 0 || _initPos.bottom() > 0) {
        // Don't center the window as we have a proper position
        if (_mode == WND_MODE_FLOAT_CENTERED)
            _mode = WND_MODE_FLOAT;
        else if (_mode == WND_MODE_FLOAT_CNT_VR)
            _mode = WND_MODE_FLOAT_OR_VR;
    }
    SetMode(_mode);
    
    // Show myself and monitor that we stay visible
    SetVisible(true);
    XPLMScheduleFlightLoop(flChangeWndMode, 1.0, 1);
}

// Desctrucotr
LTImgWindow::~LTImgWindow()
{
    if (flChangeWndMode)
        XPLMDestroyFlightLoop(flChangeWndMode);
    flChangeWndMode = nullptr;
}



/// Set the window mode, move the window if needed
void LTImgWindow::SetMode (WndMode _mode)
{
    // auto-set VR mode if requested
    if (_mode == WND_MODE_FLOAT_OR_VR)
        _mode = dataRefs.IsVREnabled() ? WND_MODE_VR : WND_MODE_FLOAT;
    else if (_mode == WND_MODE_FLOAT_CNT_VR)
        _mode = dataRefs.IsVREnabled() ? WND_MODE_VR : WND_MODE_FLOAT_CENTERED;

    // Floating: Save current geometry to have a chance to get back there
    if (GetMode() == WND_MODE_FLOAT && _mode != WND_MODE_FLOAT)
        rectFloat = GetCurrentWindowGeometry();
    
    // Do set the XP window positioning mode
    SetWindowPositioningMode(toPosMode(_mode));
    
    // reset a wish to re-position
    nextWinMode = WND_MODE_NONE;

    // If we pop in, then we need to explicitely set a position for the window to appear
    if (_mode == WND_MODE_FLOAT && !rectFloat.empty()) {
        SetCurrentWindowGeometry(rectFloat);
        rectFloat.clear();
    }
    // if we set any of the "centered" modes
    // we shall set it back to floating a few flight loops later
    else if (_mode == WND_MODE_FLOAT_CENTERED)
    {
        nextWinMode = WND_MODE_FLOAT;           // to floating
        rectFloat.clear();                      // but don't move the window!
        XPLMScheduleFlightLoop(flChangeWndMode, -5.0, 1);  // in 5 flight loops time
    }
}

/// Get current window mode
WndMode LTImgWindow::GetMode () const
{
    if (IsInVR())       return WND_MODE_VR;
    if (IsPoppedOut())  return WND_MODE_POPOUT;
    return WND_MODE_FLOAT;
}


// Get current window geometry as an WndRect structure
WndRect LTImgWindow::GetCurrentWindowGeometry () const
{
    WndRect r;
    ImgWindow::GetCurrentWindowGeometry(r.left(), r.top(), r.right(), r.bottom());
    return r;
}

// Set window geometry
void LTImgWindow::SetCurrentWindowGeometry (const WndRect& r)
{
    switch (GetMode()) {
        case WND_MODE_POPOUT:
            SetWindowGeometryOS(r.left(), r.top(), r.right(), r.bottom());
            break;
        case WND_MODE_VR:
            SetWindowGeometryVR(r.width(), r.height());
            break;
        default:
            SetWindowGeometry(r.left(), r.top(), r.right(), r.bottom());
    }
}

// Loose keyboard foucs, ie. return focus to X-Plane proper, if I have it now
bool LTImgWindow::ReturnKeyboardFocus ()
{
    if (XPLMHasKeyboardFocus(GetWindowId())) {
        XPLMTakeKeyboardFocus(0);
        return true;
    }
    return false;
}

//
// MARK: ImGui painting
//

/// Paints title, decorative lines, and window buttons
void LTImgWindow::buildTitleBar (const std::string& _title,
                                 bool bCloseBtn,
                                 bool bWndBtns)
{
    const float btnWidth = ImGui::GetWidthIconBtn(true);
    const ImGuiStyle& style = ImGui::GetStyle();
    
    // Close button
    if (bCloseBtn) {
        buildCloseButton();

        // Continue on the same line by painting 3 lines
        ImGui::SameLine();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos_start = ImGui::GetCursorPos();
        pos_start.y += 3;
        float x_end = pos_start.x + btnWidth;   // length of lines
        for (int i = 0; i < 3; i++) {
            draw_list->AddLine(pos_start, {x_end, pos_start.y},
                               LTIM_BTN_COL, 1.0f);
            pos_start.y += 5;
        }
        
        // title goes on the same line
        ImGui::SameLine();
        ImGui::SetCursorPosX(x_end + style.ItemSpacing.x);
    }
    
    // Title text
    ImGui::TextUnformatted(_title.c_str());
    
    // Continue on the same line by painting 3 lines
    ImGui::SameLine();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos_start = ImGui::GetCursorPos();
    pos_start.y += 3;
    float x_end = ImGui::GetWindowContentRegionWidth();
    if (bWndBtns)
        x_end -= 2 * btnWidth;              // space for window buttons
    if (x_end > pos_start.x)                // any (positive) line to draw?
        for (int i = 0; i < 3; i++) {       // draw 3 lines
            draw_list->AddLine(pos_start, {x_end, pos_start.y},
                               LTIM_BTN_COL, 1.0f);
            pos_start.y += 5;
        }
    
    // Window buttons
    if (bWndBtns)
        buildWndButtons();
}

// Paints close button
void LTImgWindow::buildCloseButton ()
{
    if (ImGui::ButtonIcon(ICON_FA_WINDOW_CLOSE, "Close the window", false)) {
        nextWinMode = WND_MODE_CLOSE;
        // Window mode should be set outside drawing calls to avoid crashes
        ScheduleWndModeChange();
    }
}

/// Paints resizing buttons as needed as per current window status
void LTImgWindow::buildWndButtons ()
{
    // Button with fixed width 30 and standard height
    // to pop out the window in an OS window
    const float btnWidth = ImGui::GetWidthIconBtn(true);
    const bool bBtnHelp = szHelpURL != nullptr;
    const bool bBtnPopOut = (wndStyle == WND_STYLE_HUD) && !IsPoppedOut();
//#if APL
//    // WORKAROUND: With metal, popping back in often crashes, so disable (does work in OpenGL mode, though)
//    const bool bBtnPopIn  = !dataRefs.UsingModernDriver() && (IsPoppedOut() || IsInVR());
//#else
    const bool bBtnPopIn  = IsPoppedOut() || IsInVR();
//#endif
    const bool bBtnVR     = dataRefs.IsVREnabled() && !IsInVR();
    int numBtn = bBtnHelp + bBtnPopOut + bBtnPopIn + bBtnVR;
    if (numBtn > 0) {
        // Setup colors for window sizing buttons
        ImGui::PushStyleColor(ImGuiCol_Text, LTIM_BTN_COL);                                         // very light gray
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);                               // transparent
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ScrollbarGrab));  // gray

        if (bBtnHelp) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_QUESTION_CIRCLE, "Open Help in Browser"))
                LTOpenHelp(szHelpURL);
            --numBtn;
        }
        if (bBtnVR) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_EXTERNAL_LINK_SQUARE_ALT, "Move into VR"))
                nextWinMode = WND_MODE_VR;
            --numBtn;
        }
        if (bBtnPopIn) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_WINDOW_MAXIMIZE, "Move back into X-Plane"))
                nextWinMode = WND_MODE_FLOAT;
            --numBtn;
        }
        if (bBtnPopOut) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_WINDOW_RESTORE, "Pop out into separate window"))
                nextWinMode = WND_MODE_POPOUT;
            --numBtn;
        }

        // Restore colors
        ImGui::PopStyleColor(3);

        // Window mode should be set outside drawing calls to avoid crashes
        if (nextWinMode > WND_MODE_NONE)
            ScheduleWndModeChange();
    } else {
        ImGui::NewLine();
    }

}


//
// MARK: Static and Global functions
//

// Include font 'fa-solid-900' into the binary
#include "fa-solid-900.inc"

// flight loop callback for stuff we cannot do during drawing callback
// Outside all rendering we can change things like window mode
float LTImgWindow::cbChangeWndMode(float, float, int, void* inRefcon)
{
    // refcon is pointer to ImguiWidget
    LTImgWindow& wnd = *reinterpret_cast<LTImgWindow*>(inRefcon);
    
    // Has user requested to close the window?
    // or are we already no longer visible?
    if (wnd.nextWinMode == WND_MODE_CLOSE ||
        !wnd.GetVisible())
        delete &wnd;                // -> delete the window

    // Has user requested a change in window mode?
    else if (wnd.nextWinMode > WND_MODE_NONE)
        wnd.SetMode(wnd.nextWinMode);
    
    // If we happen to be in float mode save the current position
    // (we might need it when returning from popped out state)
    if (wnd.nextWinMode != WND_MODE_FLOAT_CENTERED && wnd.IsInsideSim())
        wnd.rectFloat = wnd.GetCurrentWindowGeometry();
    
    // regularly check for window's visibility
    return 1.0f;
}

// One-time initializations for all ImGui windows
bool LTImgWindowInit ()
{
    // Create one (and exactly one) font atlas, so all fonts are textured just once
    if (!ImgWindow::sFontAtlas)
        ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
    
    // Load one standard font (comes with XP)
    if (!ImgWindow::sFontAtlas->AddFontFromFileTTF((dataRefs.GetXPSystemPath() + WND_STANDARD_FONT).c_str(),
                                                   WND_FONT_SIZE))
    {
        LOG_MSG(logFATAL, FATAL_COULD_NOT_LOAD_FONT, WND_STANDARD_FONT);
        return false;
    }
    
    // Now we merge some icons from the OpenFontsIcons font into the above font
    // (see `imgui/docs/FONTS.txt`)
    ImFontConfig config;
    config.MergeMode = true;
    
    // We only read very selectively the individual glyphs we are actually using
    // to safe on texture space
    static ImVector<ImWchar> icon_ranges;
    ImFontGlyphRangesBuilder builder;
    // Add all icons that are actually used (they concatenate into one string)
    builder.AddText(ICON_FA_ANGLE_DOUBLE_DOWN
                    ICON_FA_ANGLE_DOUBLE_UP
                    ICON_FA_CAMERA
                    ICON_FA_CHECK
                    ICON_FA_CHECK_CIRCLE
                    ICON_FA_CHEVRON_DOWN
                    ICON_FA_CHEVRON_UP
                    ICON_FA_CLIPBOARD_LIST
                    ICON_FA_EXCLAMATION_TRIANGLE
                    ICON_FA_EYE
                    ICON_FA_EXTERNAL_LINK_SQUARE_ALT
                    ICON_FA_FOLDER_OPEN
                    ICON_FA_INFO_CIRCLE
                    ICON_FA_LEVEL_UP_ALT
                    ICON_FA_PLANE
                    ICON_FA_QUESTION_CIRCLE
                    ICON_FA_SAVE
                    ICON_FA_SEARCH
                    ICON_FA_SLIDERS_H
                    ICON_FA_SORT
                    ICON_FA_SPINNER
                    ICON_FA_TIMES
                    ICON_FA_TRASH_ALT
                    ICON_FA_UNDO
                    ICON_FA_UPLOAD
                    ICON_FA_WINDOW_CLOSE
                    ICON_FA_WINDOW_MAXIMIZE
                    ICON_FA_WINDOW_RESTORE);
    builder.BuildRanges(&icon_ranges);

    // Merge the icon font with the text font
    if (!ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data,
                                                               fa_solid_900_compressed_size,
                                                               WND_FONT_SIZE,
                                                               &config,
                                                               icon_ranges.Data))
    {
        LOG_MSG(logWARN, "%s", WARN_NOT_LOADED_ICON_FONT);
    }
    
    return true;
}

// Cleanup of any resources
void LTImgWindowCleanup ()
{
    // Destroy the font atlas
    ImgWindow::sFontAtlas.reset();
}
