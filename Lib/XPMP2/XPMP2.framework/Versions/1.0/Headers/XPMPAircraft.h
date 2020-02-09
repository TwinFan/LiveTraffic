/// @file       XPMPAircraft.h
/// @brief      XPMP2::Aircraft represent an aircraft as managed by XPMP2
/// @details    New implementations should derive directly from XPMP2::Aircraft.
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

#ifndef _XPMPAircraft_h_
#define _XPMPAircraft_h_

#include "XPMPMultiplayer.h"
#include "XPLMInstance.h"
#include "XPLMCamera.h"
#include "XPLMMap.h"

#include <string>
#include <chrono>
#include <array>
#include <list>

//
// MARK: XPMP2 New Definitions
//

namespace XPMP2 {

class CSLModel;

/// Convert revolutions-per-minute (RPM) to radians per second (rad/s) by multiplying with PI/30
constexpr float RPM_to_RADs = 0.10471975511966f;
/// Convert feet to meters, e.g. for altitude calculations
constexpr double M_per_FT   = 0.3048;   // meter per 1 foot
/// Convert nautical miles to meters
constexpr int M_per_NM      = 1852;     // meter per one nautical mile

/// The dataRefs provided by XPMP2 to the CSL models
enum DR_VALS {
    V_CONTROLS_GEAR_RATIO = 0,                  ///< libxplanemp/controls/gear_ratio
    V_CONTROLS_FLAP_RATIO,                      ///< libxplanemp/controls/flap_ratio
    V_CONTROLS_SPOILER_RATIO,                   ///< libxplanemp/controls/spoiler_ratio
    V_CONTROLS_SPEED_BRAKE_RATIO,               ///< libxplanemp/controls/speed_brake_ratio
    V_CONTROLS_SLAT_RATIO,                      ///< libxplanemp/controls/slat_ratio
    V_CONTROLS_WING_SWEEP_RATIO,                ///< libxplanemp/controls/wing_sweep_ratio
    V_CONTROLS_THRUST_RATIO,                    ///< libxplanemp/controls/thrust_ratio
    V_CONTROLS_YOKE_PITCH_RATIO,                ///< libxplanemp/controls/yoke_pitch_ratio
    V_CONTROLS_YOKE_HEADING_RATIO,              ///< libxplanemp/controls/yoke_heading_ratio
    V_CONTROLS_YOKE_ROLL_RATIO,                 ///< libxplanemp/controls/yoke_roll_ratio
    V_CONTROLS_THRUST_REVERS,                   ///< libxplanemp/controls/thrust_revers
    
    V_CONTROLS_TAXI_LITES_ON,                   ///< libxplanemp/controls/taxi_lites_on
    V_CONTROLS_LANDING_LITES_ON,                ///< libxplanemp/controls/landing_lites_on
    V_CONTROLS_BEACON_LITES_ON,                 ///< libxplanemp/controls/beacon_lites_on
    V_CONTROLS_STROBE_LITES_ON,                 ///< libxplanemp/controls/strobe_lites_on
    V_CONTROLS_NAV_LITES_ON,                    ///< libxplanemp/controls/nav_lites_on
    
    V_GEAR_TIRE_VERTICAL_DEFLECTION_MTR,        ///< libxplanemp/gear/tire_vertical_deflection_mtr
    V_GEAR_TIRE_ROTATION_ANGLE_DEG,             ///< libxplanemp/gear/tire_rotation_angle_deg
    V_GEAR_TIRE_ROTATION_SPEED_RPM,             ///< libxplanemp/gear/tire_rotation_speed_rpm
    V_GEAR_TIRE_ROTATION_SPEED_RAD_SEC,         ///< libxplanemp/gear/tire_rotation_speed_rad_sec
    
    V_ENGINES_ENGINE_ROTATION_ANGLE_DEG,        ///< libxplanemp/engines/engine_rotation_angle_deg
    V_ENGINES_ENGINE_ROTATION_SPEED_RPM,        ///< libxplanemp/engines/engine_rotation_speed_rpm
    V_ENGINES_ENGINE_ROTATION_SPEED_RAD_SEC,    ///< libxplanemp/engines/engine_rotation_speed_rad_sec
    V_ENGINES_PROP_ROTATION_ANGLE_DEG,          ///< libxplanemp/engines/prop_rotation_angle_deg
    V_ENGINES_PROP_ROTATION_SPEED_RPM,          ///< libxplanemp/engines/prop_rotation_speed_rpm
    V_ENGINES_PROP_ROTATION_SPEED_RAD_SEC,      ///< libxplanemp/engines/prop_rotation_speed_rad_sec
    V_ENGINES_THRUST_REVERSER_DEPLOY_RATIO,     ///< libxplanemp/engines/thrust_reverser_deploy_ratio
    
    V_MISC_TOUCH_DOWN,                          ///< libxplanemp/misc/touch_down
    
    V_COUNT                                     ///< always last, number of dataRefs supported
};

/// @brief Actual representation of all aircraft in XPMP2.
/// @note In modern implementation, this class shall be subclassed by your plugin's code.
class Aircraft {
    
protected:
    /// Legacy: The id of the represented plane (in XPMP2, this now is an arbitrary, ever increasing number)
    XPMPPlaneID         mPlane = 0;
    
public:
    std::string acIcaoType;             ///< ICAO aircraft type of this plane
    std::string acIcaoAirline;          ///< ICAO Airline code of this plane
    std::string acLivery;               ///< Livery code of this plane
    
    /// @brief Holds position (in local coordinates!) and orientation (pitch, heading roll) of the aircraft.
    /// @details This is where it will be placed in the next drawing cycle.
    ///          When filling `y` directly (instead of using SetLocation()) remember to add
    ///          GetVertOfs() for accurate placement on the ground
    XPLMDrawInfo_t drawInfo;
    
    /// @brief actual dataRef values to be provided to the CSL model
    /// @details Combined with the indexes (see `DR_VALS`) this should be the primary location
    ///          of maintaining current aircraft parameters to avoid copy operations per drawing frame
    std::array<float,V_COUNT> v;
    
    /// aircraft label shown in the 3D world next to the plane
    std::string label;
    float       colLabel[4]  = {1.0f,1.0f,0.0f,1.0f};    ///< label base color (RGB)
    
    /// How much of the vertical offset shall be applied? (This allows phasing out the vertical offset in higher altitudes.) [0..1]
    float       vertOfsRatio = 1.0f;
    
    /// Priority for display in one of the limited number of AI/multiplayer slots
    int         aiPrio      = 1;
    
    /// @brief Current radar status
    /// @note Only the condition `mode != Standby` is of interest to XPMP2 for considering the aircraft for TCAS display
    XPMPPlaneRadar_t acRadar;
    
    /// Informational texts passed on via multiplayer shared dataRefs
    XPMPInfoTexts_t acInfoTexts;
    
protected:
    bool bVisible               = true;     ///< Shall this plane be drawn at the moment?
    
    XPMP2::CSLModel*    pCSLMdl = nullptr;  ///< the CSL model in use
    int                 matchQuality = -1;  ///< quality of the match with the CSL model
    
    // this is data from about a second ago to calculate cartesian velocities
    double              prev_x = 0.0f, prev_y = 0.0f, prev_z = 0.0f;
    std::chrono::steady_clock::time_point prev_ts;
    
    /// X-Plane instance handles for all objects making up the model
    std::list<XPLMInstanceRef> listInst;
    /// Which sim/multiplayer/plane-index used last?
    int                 multiIdx = -1;

    /// Distance to camera in meters (updated internally with every flightloop callback)
    float               distCamera = 0.0f;
    
    // Data used for drawing icons in X-Plane's map
    int                 mapIconRow = 0;     ///< map icon coordinates, row
    int                 mapIconCol = 0;     ///< map icon coordinates, column
    float               mapX = 0.0f;        ///< temporary: map coordinates (NAN = not to be drawn)
    float               mapY = 0.0f;        ///< temporary: map coordinates (NAN = not to be drawn)
    
public:
    /// Constructor
    Aircraft (const std::string& _icaoType,
              const std::string& _icaoAirline,
              const std::string& _livery,
              const std::string& _modelName = "");
    /// Destructor cleans up all resources acquired
    virtual ~Aircraft();

    /// return the XPMP2 plane id
    XPMPPlaneID GetPlaneID () const { return mPlane; }
    /// return the last used sim/multiplayer/plane-index
    int         GetMultiIdx () const { return multiIdx; }
    /// Will this plane show up on TCAS / in multiplayer views? (It will if transponder is not switched off)
    bool        ShowAsAIPlane () const { return IsVisible() && acRadar.mode != xpmpTransponderMode_Standby; }
    
    /// (Potentially) change the plane's model after doing a new match attempt
    int ChangeModel (const std::string& _icaoType,
                     const std::string& _icaoAirline,
                     const std::string& _livery);
    /// Assigns the given model per name, returns if successful
    bool AssignModel (const std::string& _modelName);
    /// return the name of the CSL model in use
    std::string GetModelName () const;
    /// quality of the match with the CSL model
    int         GetMatchQuality () const { return matchQuality; }
    /// Vertical offset, ie. the value that needs to be added to drawInfo.y to make the aircraft appear on the ground
    float       GetVertOfs () const;
    
    /// @brief Called right before updating the aircraft's placement in the world
    /// @details Abstract virtual function. Override in derived classes and fill
    ///          `drawInfo`, the `v` array of dataRefs, `label`, and `infoTexts` with current values.
    virtual void UpdatePosition () = 0;
    /// Distance to camera [m]
    float GetDistToCamera () const { return distCamera; }
    
    /// @brief Converts world coordinates to local coordinates, writes to `drawInfo`
    /// @note Alternatively, the calling plugin can set local coordinates in `drawInfo`directly
    /// @param lat Latitude in degress -90..90
    /// @param lon Longitude in degrees -180..180
    /// @param alt_ft Altitude in feet above MSL
    void SetLocation (double lat, double lon, double alt_ft);
    
    /// @brief Converts aircraft's local coordinates to lat/lon values
    /// @warning This isn't exactly precice. If you need precise location keep it in your derived class yourself.
    void GetLocation (double& lat, double& lon, double& alt_ft) const;
    
    /// Make the plane (in)visible
    virtual void SetVisible (bool _bVisible);
    /// Is the plane visible?
    bool IsVisible () const { return bVisible; }
    
    // The following is implemented in Map.cpp:
    /// Determine which map icon to use for this aircraft
    void MapFindIcon ();
    /// Prepare map coordinates
    void MapPreparePos (XPLMMapProjectionID  projection,
                        const float boundsLTRB[4]);
    /// Actually draw the map icon
    void MapDrawIcon (XPLMMapLayerID inLayer, float acSize);
    /// Actually draw the map label
    void MapDrawLabel (XPLMMapLayerID inLayer, float yOfs);

protected:
    /// Internal: Flight loop callback function
    static float FlightLoopCB (float, float, int, void*);
    /// Internal: This puts the instance into XP's sky and makes it move
    void DoMove ();
    /// Internal: Update the plane's distance from the camera location
    void UpdateDistCamera (const XPLMCameraPosition_t& posCam);
    /// Create the instances, return if successful
    bool CreateInstances ();
    /// Destroy all instances
    void DestroyInstances ();

    // The following functions are implemented in AIMultiplayer.cpp:
    /// AI/Multiplayer handling: Find next AI slot
    int  AISlotReserve ();
    /// AI/Multiplayer handling: Clear AI slot
    void AISlotClear ();
    // These functions are called from AIMultiUpdate()
    friend void AIMultiUpdate ();
};

/// Find aircraft by its plane ID, can return nullptr
Aircraft* AcFindByID (XPMPPlaneID _id);

//
// MARK: XPMP2 Exception class
//

/// XPMP2 Exception class, e.g. thrown if there are no CSL models when creating an Aircraft
class XPMP2Error : public std::logic_error {
protected:
    std::string fileName;           ///< filename of the line of code where exception occurred
    int ln;                         ///< line number of the line of code where exception occurred
    std::string funcName;           ///< function of the line of code where exception occurred
    std::string msg;                ///< additional text message
public:
    /// Constructor puts together a formatted exception text
    XPMP2Error (const char* szFile, int ln, const char* szFunc, const char* szMsg, ...);
public:
    /// returns msg.c_str()
    virtual const char* what() const noexcept;
    
public:
    // copy/move constructor/assignment as per default
    XPMP2Error (const XPMP2Error& o) = default;
    XPMP2Error (XPMP2Error&& o) = default;
    XPMP2Error& operator = (const XPMP2Error& o) = default;
    XPMP2Error& operator = (XPMP2Error&& o) = default;
};



}   // namespace XPMP2

#endif
