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

#include <stdexcept>
#include <string>
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
    V_CONTROLS_GEAR_RATIO = 0,                  ///< `libxplanemp/controls/gear_ratio` and \n`sim/cockpit2/tcas/targets/position/gear_deploy`
    V_CONTROLS_FLAP_RATIO,                      ///< `libxplanemp/controls/flap_ratio` and \n`sim/cockpit2/tcas/targets/position/flap_ratio` and `...flap_ratio2`
    V_CONTROLS_SPOILER_RATIO,                   ///< `libxplanemp/controls/spoiler_ratio`
    V_CONTROLS_SPEED_BRAKE_RATIO,               ///< `libxplanemp/controls/speed_brake_ratio` and \n`sim/cockpit2/tcas/targets/position/speedbrake_ratio`
    V_CONTROLS_SLAT_RATIO,                      ///< `libxplanemp/controls/slat_ratio` and \n`sim/cockpit2/tcas/targets/position/slat_ratio`
    V_CONTROLS_WING_SWEEP_RATIO,                ///< `libxplanemp/controls/wing_sweep_ratio` and \n`sim/cockpit2/tcas/targets/position/wing_sweep`
    V_CONTROLS_THRUST_RATIO,                    ///< `libxplanemp/controls/thrust_ratio` and \n`sim/cockpit2/tcas/targets/position/throttle`
    V_CONTROLS_YOKE_PITCH_RATIO,                ///< `libxplanemp/controls/yoke_pitch_ratio` and \n`sim/cockpit2/tcas/targets/position/yolk_pitch`
    V_CONTROLS_YOKE_HEADING_RATIO,              ///< `libxplanemp/controls/yoke_heading_ratio` and \n`sim/cockpit2/tcas/targets/position/yolk_yaw`
    V_CONTROLS_YOKE_ROLL_RATIO,                 ///< `libxplanemp/controls/yoke_roll_ratio` and \n`sim/cockpit2/tcas/targets/position/yolk_roll`
    V_CONTROLS_THRUST_REVERS,                   ///< `libxplanemp/controls/thrust_revers`
    
    V_CONTROLS_TAXI_LITES_ON,                   ///< `libxplanemp/controls/taxi_lites_on` and \n`sim/cockpit2/tcas/targets/position/lights`
    V_CONTROLS_LANDING_LITES_ON,                ///< `libxplanemp/controls/landing_lites_on` and \n`sim/cockpit2/tcas/targets/position/lights`
    V_CONTROLS_BEACON_LITES_ON,                 ///< `libxplanemp/controls/beacon_lites_on` and \n`sim/cockpit2/tcas/targets/position/lights`
    V_CONTROLS_STROBE_LITES_ON,                 ///< `libxplanemp/controls/strobe_lites_on` and \n`sim/cockpit2/tcas/targets/position/lights`
    V_CONTROLS_NAV_LITES_ON,                    ///< `libxplanemp/controls/nav_lites_on` and \n`sim/cockpit2/tcas/targets/position/lights`
    
    V_GEAR_TIRE_VERTICAL_DEFLECTION_MTR,        ///< `libxplanemp/gear/tire_vertical_deflection_mtr`
    V_GEAR_TIRE_ROTATION_ANGLE_DEG,             ///< `libxplanemp/gear/tire_rotation_angle_deg`
    V_GEAR_TIRE_ROTATION_SPEED_RPM,             ///< `libxplanemp/gear/tire_rotation_speed_rpm`
    V_GEAR_TIRE_ROTATION_SPEED_RAD_SEC,         ///< `libxplanemp/gear/tire_rotation_speed_rad_sec`
    
    V_ENGINES_ENGINE_ROTATION_ANGLE_DEG,        ///< `libxplanemp/engines/engine_rotation_angle_deg`
    V_ENGINES_ENGINE_ROTATION_SPEED_RPM,        ///< `libxplanemp/engines/engine_rotation_speed_rpm`
    V_ENGINES_ENGINE_ROTATION_SPEED_RAD_SEC,    ///< `libxplanemp/engines/engine_rotation_speed_rad_sec`
    V_ENGINES_PROP_ROTATION_ANGLE_DEG,          ///< `libxplanemp/engines/prop_rotation_angle_deg`
    V_ENGINES_PROP_ROTATION_SPEED_RPM,          ///< `libxplanemp/engines/prop_rotation_speed_rpm`
    V_ENGINES_PROP_ROTATION_SPEED_RAD_SEC,      ///< `libxplanemp/engines/prop_rotation_speed_rad_sec`
    V_ENGINES_THRUST_REVERSER_DEPLOY_RATIO,     ///< `libxplanemp/engines/thrust_reverser_deploy_ratio`
    
    V_MISC_TOUCH_DOWN,                          ///< `libxplanemp/misc/touch_down`
    
    V_COUNT                                     ///< always last, number of dataRefs supported
};

/// @brief Actual representation of all aircraft in XPMP2.
/// @note In modern implementations, this class shall be subclassed by your plugin's code.
class Aircraft {
    
protected:
    /// @brief A plane is uniquely identified by a 24bit number [0x01..0xFFFFFF]
    /// @details This number is directly used as `modeS_id` n the new
    ///          [TCAS override](https://developer.x-plane.com/article/overriding-tcas-and-providing-traffic-information/)
    ///          approach.
    XPMPPlaneID modeS_id = 0;
    
public:
    /// @brief ICAO aircraft type designator of this plane
    /// @see https://www.icao.int/publications/DOC8643/Pages/Search.aspx
    std::string acIcaoType;
    std::string acIcaoAirline;          ///< ICAO Airline code of this plane
    std::string acLivery;               ///< Livery code of this plane
    
    /// @brief Holds position (in local coordinates!) and orientation (pitch, heading roll) of the aircraft.
    /// @details This is where the plane will be placed in this drawing cycle.\n
    /// @note    When filling `y` directly (instead of using SetLocation()) remember to add
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
    
    /// @brief Shall this plane be clamped to ground (ie. never sink below ground)?
    /// @note This involves Y-Testing, which is a bit expensive, see [SDK](https://developer.x-plane.com/sdk/XPLMScenery).
    ///       If you know your plane is not close to the ground,
    ///       you may want to avoid clamping by setting this to `false`.
    /// @see configuration item `XPMP2_CFG_ITM_CLAMPALL`
    bool        bClampToGround = false;
    
    /// @brief Priority for display in one of the limited number of TCAS target slots
    /// @details The lower the earlier will a plane be considered for TCAS.
    ///          Increase this value if you want to make a plane less likely
    ///          to occupy one of the limited TCAS slots.
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
    float               prev_x = 0.0f, prev_y = 0.0f, prev_z = 0.0f;
    float               prev_ts = 0.0f;     ///< last update of `prev_x/y/z` in XP's network time
    
    /// X-Plane instance handles for all objects making up the model
    std::list<XPLMInstanceRef> listInst;
    /// Which `sim/cockpit2/tcas/targets`-index does this plane occupy? [1..63], `-1` if none
    int                 tcasTargetIdx = -1;

    /// Timestamp of last update of camera dist/bearing
    float               camTimLstUpd = 0.0f;
    /// Distance to camera in meters (updated internally regularly)
    float               camDist = 0.0f;
    /// Bearing from camera in degrees (updated internally regularly)
    float               camBearing = 0.0f;

    /// Y Probe for terrain testing, needed in ground clamping
    XPLMProbeRef        hProbe = nullptr;
    
    // Data used for drawing icons in X-Plane's map
    int                 mapIconRow = 0;     ///< map icon coordinates, row
    int                 mapIconCol = 0;     ///< map icon coordinates, column
    float               mapX = 0.0f;        ///< temporary: map coordinates (NAN = not to be drawn)
    float               mapY = 0.0f;        ///< temporary: map coordinates (NAN = not to be drawn)
    std::string         mapLabel;           ///< label for map drawing
    
public:
    /// Constructor creates a new aircraft object, which will be managed and displayed
    /// @exception XPMP2::XPMP2Error Mode S id invalid or duplicate, no model found during model matching
    /// @param _icaoType ICAO aircraft type designator, like 'A320', 'B738', 'C172'
    /// @param _icaoAirline ICAO airline code, like 'BAW', 'DLH', can be an empty string
    /// @param _livery Special livery designator, can be an empty string
    /// @param _modeS_id (optional) **Unique** identification of the plane [0x01..0xFFFFFF], e.g. the 24bit mode S transponder code. XPMP2 assigns an arbitrary unique number of not given
    /// @param _modelId (optional) specific model id to be used (no folder/package name, just the id as defined in the `OBJ8_AIRCRAFT` line)
    Aircraft (const std::string& _icaoType,
              const std::string& _icaoAirline,
              const std::string& _livery,
              XPMPPlaneID _modeS_id = 0,
              const std::string& _modelId = "");
    /// Destructor cleans up all resources acquired
    virtual ~Aircraft();

    /// return the XPMP2 plane id
    XPMPPlaneID GetModeS_ID () const { return modeS_id; }
    /// Is this object a ground vehicle?
    bool        IsGroundVehicle() const;
    /// @brief return the current TCAS target index (into `sim/cockpit2/tcas/targets`), 1-based, `-1` if not used
    int         GetTcasTargetIdx () const { return tcasTargetIdx; }
    /// Is this plane currently also being tracked as a TCAS target, ie. will appear on TCAS?
    bool        IsCurrentlyShownAsTcasTarget () const { return tcasTargetIdx >= 1; }
    /// Is this plane currently also being tracked by X-Plane's classic AI/multiplayer?
    bool        IsCurrentlyShownAsAI () const { return 1 <= tcasTargetIdx && tcasTargetIdx <= 20; }
    /// Is this plane to be drawn on TCAS? (It will if transponder is not switched off)
    bool        ShowAsAIPlane () const { return IsVisible() && acRadar.mode != xpmpTransponderMode_Standby; }
    /// Reset TCAS target slot index to `-1`
    void        ResetTcasTargetIdx () { tcasTargetIdx = -1; }

    /// @brief Return a value for dataRef .../tcas/target/flight_id
    /// @returns The first non-empty string out of: flight number, registration, departure/arrival airports
    virtual std::string GetFlightId() const;
    
    /// @brief (Potentially) changes the plane's model after doing a new match attempt
    /// @param _icaoType ICAO aircraft type designator, like 'A320', 'B738', 'C172'
    /// @param _icaoAirline ICAO airline code, like 'BAW', 'DLH', can be an empty string
    /// @param _livery Special livery designator, can be an empty string
    /// @return match quality, the lower the better
    int ChangeModel (const std::string& _icaoType,
                     const std::string& _icaoAirline,
                     const std::string& _livery);
    
    /// @brief Finds a match again, using the existing parameters, eg. after more models have been loaded
    /// @return match quality, the lower the better
    int ReMatchModel () { return ChangeModel(acIcaoType,acIcaoAirline,acLivery); }

    /// Assigns the given model per name, returns if successful
    bool AssignModel (const std::string& _modelName);
    /// return a pointer to the CSL model in use (Note: The CSLModel structure is not public.)
    XPMP2::CSLModel* GetModel () const { return pCSLMdl; }
    /// return the name of the CSL model in use
    const std::string& GetModelName () const;
    /// quality of the match with the CSL model
    int         GetMatchQuality () const { return matchQuality; }
    /// Vertical offset, ie. the value that needs to be added to `drawInfo.y` to make the aircraft appear on the ground
    float       GetVertOfs () const;
    
    /// Make the plane (in)visible
    virtual void SetVisible (bool _bVisible);
    /// Is the plane visible?
    bool IsVisible () const { return bVisible; }
    
    /// Distance to camera [m]
    float GetCameraDist () const { return camDist; }
    /// Bearing from camera [°]
    float GetCameraBearing () const { return camBearing; }

    /// @brief Called right before updating the aircraft's placement in the world
    /// @details Abstract virtual function. Override in derived classes and fill
    ///          `drawInfo`, the `v` array of dataRefs, `label`, and `infoTexts` with current values.
    /// @see See [XPLMFlightLoop_f](https://developer.x-plane.com/sdk/XPLMProcessing/#XPLMFlightLoop_f)
    ///      for background on the two passed-on parameters:
    /// @param _elapsedSinceLastCall The wall time since last call
    /// @param _flCounter A monotonically increasing counter, bumped once per flight loop dispatch from the sim.
    virtual void UpdatePosition (float _elapsedSinceLastCall, int _flCounter) = 0;
    
    // --- Getters and Setters for the values in `drawInfo` ---

    /// @brief Converts world coordinates to local coordinates, writes to Aircraft::drawInfo
    /// @note Alternatively, the calling plugin can set local coordinates in Aircraft::drawInfo directly
    /// @param lat Latitude in degress -90..90
    /// @param lon Longitude in degrees -180..180
    /// @param alt_ft Altitude in feet above MSL
    void SetLocation (double lat, double lon, double alt_ft);
    
    /// @brief Converts aircraft's local coordinates to lat/lon values
    /// @warning This isn't exactly precice. If you need precise location keep it in your derived class yourself.
    void GetLocation (double& lat, double& lon, double& alt_ft) const;
    
    /// Sets location in local world coordinates
    void SetLocalLoc (float _x, float _y, float _z) { drawInfo.x = _x; drawInfo.y = _y; drawInfo.z = _z; }
    /// Gets all location info (including local coordinates)
    const XPLMDrawInfo_t& GetLocation () const { return drawInfo; }

    float GetPitch () const              { return drawInfo.pitch; }                     ///< pitch [degree]
    void  SetPitch (float _deg)          { drawInfo.pitch = _deg; }                     ///< pitch [degree]
    float GetHeading () const            { return drawInfo.heading; }                   ///< heading [degree]
    void  SetHeading (float _deg)        { drawInfo.heading = _deg; }                   ///< heading [degree]
    float GetRoll () const               { return drawInfo.roll; }                      ///< roll [degree]
    void  SetRoll (float _deg)           { drawInfo.roll = _deg; }                      ///< roll [degree]

    // --- Getters and Setters for the values in the `v` array ---
    float GetGearRatio () const          { return v[V_CONTROLS_GEAR_RATIO]; }           ///< Gear deploy ratio
    void  SetGearRatio (float _f)        { v[V_CONTROLS_GEAR_RATIO] = _f;   }           ///< Gear deploy ratio
    float GetFlapRatio () const          { return v[V_CONTROLS_FLAP_RATIO]; }           ///< Flaps deploy ratio
    void  SetFlapRatio (float _f)        { v[V_CONTROLS_FLAP_RATIO] = _f;   }           ///< Flaps deploy ratio
    float GetSpoilerRatio () const       { return v[V_CONTROLS_SPOILER_RATIO]; }        ///< Spoilers deploy ratio
    void  SetSpoilerRatio (float _f)     { v[V_CONTROLS_SPOILER_RATIO] = _f;   }        ///< Spoilers deploy ratio
    float GetSpeedbrakeRatio () const    { return v[V_CONTROLS_SPEED_BRAKE_RATIO]; }    ///< Speedbrakes deploy ratio
    void  SetSpeedbrakeRatio (float _f)  { v[V_CONTROLS_SPEED_BRAKE_RATIO] = _f;   }    ///< Speedbrakes deploy ratio
    float GetSlatRatio () const          { return v[V_CONTROLS_SLAT_RATIO]; }           ///< Slats deploy ratio
    void  SetSlatRatio (float _f)        { v[V_CONTROLS_SLAT_RATIO] = _f;   }           ///< Slats deploy ratio
    float GetWingSweepRatio () const     { return v[V_CONTROLS_WING_SWEEP_RATIO]; }     ///< Wing sweep ratio
    void  SetWingSweepRatio (float _f)   { v[V_CONTROLS_WING_SWEEP_RATIO] = _f;   }     ///< Wing sweep ratio
    float GetThrustRatio () const        { return v[V_CONTROLS_THRUST_RATIO]; }         ///< Thrust ratio
    void  SetThrustRatio (float _f)      { v[V_CONTROLS_THRUST_RATIO] = _f;   }         ///< Thrust ratio
    float GetYokePitchRatio () const     { return v[V_CONTROLS_YOKE_PITCH_RATIO]; }     ///< Yoke pitch ratio
    void  SetYokePitchRatio (float _f)   { v[V_CONTROLS_YOKE_PITCH_RATIO] = _f;   }     ///< Yoke pitch ratio
    float GetYokeHeadingRatio () const   { return v[V_CONTROLS_YOKE_HEADING_RATIO]; }   ///< Yoke heading ratio
    void  SetYokeHeadingRatio (float _f) { v[V_CONTROLS_YOKE_HEADING_RATIO] = _f;   }   ///< Yoke heading ratio
    float GetYokeRollRatio () const      { return v[V_CONTROLS_YOKE_ROLL_RATIO]; }      ///< Yoke roll ratio
    void  SetYokeRollRatio (float _f)    { v[V_CONTROLS_YOKE_ROLL_RATIO] = _f;   }      ///< Yoke roll ratio
    float GetThrustReversRatio () const  { return v[V_CONTROLS_THRUST_REVERS]; }        ///< Thrust reversers ratio
    void  SetThrustReversRatio (float _f){ v[V_CONTROLS_THRUST_REVERS] = _f;   }        ///< Thrust reversers ratio

    bool  GetLightsTaxi () const         { return v[V_CONTROLS_TAXI_LITES_ON] > 0.5f; }     ///< Taxi lights
    void  SetLightsTaxi (bool _b)        { v[V_CONTROLS_TAXI_LITES_ON] = float(_b);   }     ///< Taxi lights
    bool  GetLightsLanding () const      { return v[V_CONTROLS_LANDING_LITES_ON] > 0.5f; }  ///< Landing lights
    void  SetLightsLanding (bool _b)     { v[V_CONTROLS_LANDING_LITES_ON] = float(_b);   }  ///< Landing lights
    bool  GetLightsBeacon () const       { return v[V_CONTROLS_BEACON_LITES_ON] > 0.5f; }   ///< Beacon lights
    void  SetLightsBeacon (bool _b)      { v[V_CONTROLS_BEACON_LITES_ON] = float(_b);   }   ///< Beacon lights
    bool  GetLightsStrobe () const       { return v[V_CONTROLS_STROBE_LITES_ON] > 0.5f; }   ///< Strobe lights
    void  SetLightsStrobe (bool _b)      { v[V_CONTROLS_STROBE_LITES_ON] = float(_b);   }   ///< Strobe lights
    bool  GetLightsNav () const          { return v[V_CONTROLS_NAV_LITES_ON] > 0.5f; }      ///< Navigation lights
    void  SetLightsNav (bool _b)         { v[V_CONTROLS_NAV_LITES_ON] = float(_b);   }      ///< Navigation lights

    float GetTireDeflection () const     { return v[V_GEAR_TIRE_VERTICAL_DEFLECTION_MTR]; } ///< Vertical tire deflection [meter]
    void  SetTireDeflection (float _mtr) { v[V_GEAR_TIRE_VERTICAL_DEFLECTION_MTR] = _mtr; } ///< Vertical tire deflection [meter]
    float GetTireRotAngle () const       { return v[V_GEAR_TIRE_ROTATION_ANGLE_DEG]; }      ///< Tire rotation angle [degree]
    void  SetTireRotAngle (float _deg)   { v[V_GEAR_TIRE_ROTATION_ANGLE_DEG] = _deg; }      ///< Tire rotation angle [degree]
    float GetTireRotRpm () const         { return v[V_GEAR_TIRE_ROTATION_SPEED_RPM]; }      ///< Tire rotation speed [rpm]
    void  SetTireRotRpm (float _rpm)     { v[V_GEAR_TIRE_ROTATION_SPEED_RPM] = _rpm;        ///< Tire rotation speed [rpm], also sets [rad/s]
                                           v[V_GEAR_TIRE_ROTATION_SPEED_RAD_SEC] = _rpm * RPM_to_RADs; }
    float GetTireRotRad () const         { return v[V_GEAR_TIRE_ROTATION_SPEED_RAD_SEC]; }  ///< Tire rotation speed [rad/s]
    void  SetTireRotRad (float _rad)     { v[V_GEAR_TIRE_ROTATION_SPEED_RAD_SEC] = _rad;    ///< Tire rotation speed [rad/s], also sets [rpm]
                                           v[V_GEAR_TIRE_ROTATION_SPEED_RPM] = _rad / RPM_to_RADs; }
    
    float GetEngineRotAngle () const     { return v[V_ENGINES_ENGINE_ROTATION_ANGLE_DEG]; }      ///< Engine rotation angle [degree]
    void  SetEngineRotAngle (float _deg) { v[V_ENGINES_ENGINE_ROTATION_ANGLE_DEG] = _deg; }      ///< Engine rotation angle [degree]
    float GetEngineRotRpm () const       { return v[V_ENGINES_ENGINE_ROTATION_SPEED_RPM]; }      ///< Engine rotation speed [rpm]
    void  SetEngineRotRpm (float _rpm)   { v[V_ENGINES_ENGINE_ROTATION_SPEED_RPM] = _rpm;        ///< Engine rotation speed [rpm], also sets [rad/s]
                                           v[V_ENGINES_ENGINE_ROTATION_SPEED_RAD_SEC] = _rpm * RPM_to_RADs; }
    float GetEngineRotRad () const       { return v[V_ENGINES_ENGINE_ROTATION_SPEED_RAD_SEC]; }  ///< Engine rotation speed [rad/s]
    void  SetEngineRotRad (float _rad)   { v[V_ENGINES_ENGINE_ROTATION_SPEED_RAD_SEC] = _rad;    ///< Engine rotation speed [rad/s], also sets [rpm]
                                           v[V_ENGINES_ENGINE_ROTATION_SPEED_RPM] = _rad / RPM_to_RADs; }

    float GetPropRotAngle () const       { return v[V_ENGINES_PROP_ROTATION_ANGLE_DEG]; }      ///< Propellor rotation angle [degree]
    void  SetPropRotAngle (float _deg)   { v[V_ENGINES_PROP_ROTATION_ANGLE_DEG] = _deg; }      ///< Propellor rotation angle [degree]
    float GetPropRotRpm () const         { return v[V_ENGINES_PROP_ROTATION_SPEED_RPM]; }      ///< Propellor rotation speed [rpm]
    void  SetPropRotRpm (float _rpm)     { v[V_ENGINES_PROP_ROTATION_SPEED_RPM] = _rpm;        ///< Propellor rotation speed [rpm], also sets [rad/s]
                                           v[V_ENGINES_PROP_ROTATION_SPEED_RAD_SEC] = _rpm * RPM_to_RADs; }
    float GetPropRotRad () const         { return v[V_ENGINES_PROP_ROTATION_SPEED_RAD_SEC]; }  ///< Propellor rotation speed [rad/s]
    void  SetPropRotRad (float _rad)     { v[V_ENGINES_PROP_ROTATION_SPEED_RAD_SEC] = _rad;    ///< Propellor rotation speed [rad/s], also sets [rpm]
                                           v[V_ENGINES_PROP_ROTATION_SPEED_RPM] = _rad / RPM_to_RADs; }

    float GetReversDeployRatio () const  { return v[V_ENGINES_THRUST_REVERSER_DEPLOY_RATIO]; }  ///< Thrust reversers deploy ratio
    void  SetReversDeployRatio (float _f){ v[V_ENGINES_THRUST_REVERSER_DEPLOY_RATIO] = _f;   }  ///< Thrust reversers deploy ratio

    bool  GetTouchDown () const          { return v[V_MISC_TOUCH_DOWN] > 0.5f; }                ///< Moment of touch down
    void  SetTouchDown (bool _b)         { v[V_MISC_TOUCH_DOWN] = float(_b);   }                ///< Moment of touch down

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
    /// Internal: Flight loop callback function controlling update and movement of all planes
    static float FlightLoopCB (float, float, int, void*);
    /// Internal: This puts the instance into XP's sky and makes it move
    void DoMove ();
    /// Internal: Update the plane's distance/bearing from the camera location
    void UpdateDistBearingCamera (const XPLMCameraPosition_t& posCam);
    /// Clamp to ground: Make sure the plane is not below ground, corrects Aircraft::drawInfo if needed.
    void ClampToGround ();
    /// Create the instances required to represent the plane, return if successful
    bool CreateInstances ();
    /// Destroy all instances
    void DestroyInstances ();
    
    /// @brief Put together the map label
    /// @details Called about once a second. Label depends on tcasTargetIdx
    virtual void ComputeMapLabel ();

    // The following functions are implemented in AIMultiplayer.cpp:
    /// Define the TCAS target index in use
    virtual void SetTcasTargetIdx (int _idx) { tcasTargetIdx = _idx; }
    // These functions are called from AIMultiUpdate()
    friend void AIMultiUpdate ();
};

/// Find aircraft by its plane ID, can return nullptr
Aircraft* AcFindByID (XPMPPlaneID _id);

//
// MARK: XPMP2 Exception class
//

/// XPMP2 Exception class, e.g. thrown if there are no CSL models or duplicate modeS_ids when creating an Aircraft
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
