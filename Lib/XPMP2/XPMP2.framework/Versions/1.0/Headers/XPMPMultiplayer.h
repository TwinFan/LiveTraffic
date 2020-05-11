/// @file       XPMPMultiplayer.h
/// @brief      Initialization and general control functions for XPMP2
///
/// @note       This file bases on and should be largely compile-compatible to
///             the header provided with the original libxplanemp.
/// @note       Some functions are marked deprecated, reason is given with
///             each function. They are only provided for backward
///             compatibility.
///
/// @details    This is one of two main header files for using XPMP2.
///             (The other is `XPMPAircraft.h`).
///             XPMP2 is a library allowing an X-Plane plugin to have
///             planes rendered in X-Plane's 3D world based on OBJ8
///             CSL models, which need to be installed separatela.
///             The plugin shall subclass XPMP2::Aircraft:: and override
///             the abstract virtual function XPMP2::Aircraft::UpdatePosition()
///             to provide updated position and attitude information.
///             XPMP2 takes care of reading and initializaing CSL models,
///             instanciating and updating the aircraft objects in X-Plane,
///             display in a map layer, provisioning information via X-Plane's
///             AI/multiplayer (and more) dataRefs.
///
/// @see        For more developer's information see
///             https://twinfan.github.io/XPMP2/
///
/// @see        For a definition of ICAO aircraft type designators see
///             https://www.icao.int/publications/DOC8643/Pages/Search.aspx
///
/// @see        For a list of ICAO airline/operator codes see
///             https://en.wikipedia.org/wiki/List_of_airline_codes
///
/// @author     Ben Supnik and Chris Serio
/// @copyright  Copyright (c) 2004, Ben Supnik and Chris Serio.
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

#ifndef _XPLMMultiplayer_h_
#define _XPLMMultiplayer_h_

#include <string>
#include "XPLMDefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************************
 * MARK: PLANE DATA TYPES
 ************************************************************************************/

/// XPMPPosition_t
///
/// @brief This data structure contains the basic position info for an aircraft.
/// @note  This structure is only used with deprecated concepts
///        (class XPCAircraft and direct use of callback functions.)
/// @note  There is no notion of aircraft velocity or acceleration; you will be queried for
///        your position every rendering frame.  Higher level APIs can use velocity and acceleration.
struct XPMPPlanePosition_t {
    long    size            = sizeof(XPMPPlanePosition_t);  ///< size of structure
    double  lat             = 0.0;                      ///< current position of aircraft (latitude)
    double  lon             = 0.0;                      ///< current position of aircraft (longitude)
    double  elevation       = 0.0;                      ///< current altitude of aircraft [ft above MSL]
    float   pitch           = 0.0f;                     ///< pitch [degrees, psitive up]
    float   roll            = 0.0f;                     ///< roll [degrees, positive right]
    float   heading         = 0.0f;                     ///< heading [degrees]
    char    label[32]       = "";                       ///< label to show with the aircraft
    float   offsetScale     = 1.0f;                     ///< how much of the surface contact correction offset should be applied [0..1]
    bool    clampToGround   = false;                    ///< enables ground-clamping for this aircraft (can be expensive, see Aircraft::bClampToGround)
    int     aiPrio          = 1;                        ///< Priority for AI/TCAS consideration, the lower the earlier
    float   label_color[4]  = {1.0f,1.0f,0.0f,1.0f};    ///< label base color (RGB), defaults to yellow
    int     multiIdx        = 0;                        ///< OUT: set by XPMP2 to inform application about multiplayer index in use, `< 1` means 'none'
};


/// @brief Light flash patterns
/// @note Unused in XPMP2
/// @note Not changed to proper enum type as it is used in bitfields in xpmp_LightStatus,
///       which causes misleading, non-suppressable warnings in gcc:\n
///       https://gcc.gnu.org/bugzilla/show_bug.cgi?id=51242
enum {
    xpmp_Lights_Pattern_Default     = 0,    ///< Jets: one strobe flash, short beacon (-*---*---*---)
    xpmp_Lights_Pattern_EADS        = 1,    ///< Airbus+EADS: strobe flashes twice (-*-*-----*-*--), short beacon
    xpmp_Lights_Pattern_GA          = 2     ///< GA: one strobe flash, long beacon (-*--------*---)
};
typedef unsigned int XPMPLightsPattern;     ///< Light flash pattern (unused in XPMP2)


#if LIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // because we don't want to change the anonymous structure following here as that would require code change in legacy plugins
#elif IBM
#pragma warning(push)
#pragma warning(disable: 4202 4201)
#endif
/// @brief Defines, which lights of the aircraft are on
/// @note  This structure is only used with deprecated concepts
///        (class XPCAircraft and direct use of callback functions.)
union xpmp_LightStatus {
    unsigned int lightFlags = 0x150000;     ///< this defaults to taxi | beacon | nav lights on
    struct {
        unsigned int timeOffset : 16;       ///< time offset to avoid lights across planes blink in sync (unused in XPMP2)
        
        unsigned int taxiLights : 1;        ///< taxi lights on?
        unsigned int landLights : 1;        ///< landing lights on?
        unsigned int bcnLights  : 1;        ///< beacon on?
        unsigned int strbLights : 1;        ///< strobe lights on?
        unsigned int navLights  : 1;        ///< navigation lights on?
        
        /// light pattern (unused in XPMP2)
        XPMPLightsPattern flashPattern   : 4;
    };
};
#if LIN
#pragma GCC diagnostic pop
#elif IBM
#pragma warning(pop)
#endif


/// @brief External physical configuration of the plane
/// @note  This structure is only used with deprecated concepts
///        (class XPCAircraft and direct use of callback functions.)
/// @details    This data structure will contain information about the external physical configuration of the plane,
///             things you would notice if you are seeing it from outside.  This includes flap position, gear position,
///             etc.
struct XPMPPlaneSurfaces_t {
    long                  size = sizeof(XPMPPlaneSurfaces_t);   ///< structure size
    float                 gearPosition      = 0.0f;             ///< gear position [0..1]
    float                 flapRatio         = 0.0f;             ///< flap extension ratio [0..1]
    float                 spoilerRatio      = 0.0f;             ///< spoiler extension ratio [0..1]
    float                 speedBrakeRatio   = 0.0f;             ///< speed brake extension ratio [0..1]
    float                 slatRatio         = 0.0f;             ///< slats extension ratio [0..1]
    float                 wingSweep         = 0.0f;             ///< wing sweep ratio [0..1]
    float                 thrust            = 0.0f;             ///< thrust ratio [0..1]
    float                 yokePitch         = 0.0f;             ///< yoke pitch ratio [0..1]
    float                 yokeHeading       = 0.0f;             ///< yoke heading ratio [0..1]
    float                 yokeRoll          = 0.0f;             ///< yoke roll ratio [0..1]
    
    xpmp_LightStatus      lights;                               ///< status of lights
    
    float                 tireDeflect       = 0.0f;             ///< tire deflection (meters)
    float                 tireRotDegree     = 0.0f;             ///< tire rotation angle (degrees 0..360)
    float                 tireRotRpm        = 0.0f;             ///< tire rotation speed (rpm)
    
    float                 engRotDegree      = 0.0f;             ///< engine rotation angle (degrees 0..360)
    float                 engRotRpm         = 0.0f;             ///< engine rotation speed (rpm)
    float                 propRotDegree     = 0.0f;             ///< prop rotation angle (degrees 0..360)
    float                 propRotRpm        = 0.0f;             ///< prop rotation speed (rpm)
    float                 reversRatio       = 0.0f;             ///< thrust reversers ratio
    
    bool                  touchDown = false;                    ///< just now is moment of touch down?
};


/// @brief These enumerations define the way the transponder of a given plane is operating.
/// @note Only information used by XPMP2 is `"mode != xpmpTransponderMode_Standby"`,
///       in which case the plane is considered for TCAS display.
enum XPMPTransponderMode {
    xpmpTransponderMode_Standby,        ///< transponder is in standby, not currently sending -> aircraft not visible on TCAS
    xpmpTransponderMode_Mode3A,         ///< transponder is on
    xpmpTransponderMode_ModeC,          ///< transponder is on
    xpmpTransponderMode_ModeC_Low,      ///< transponder is on
    xpmpTransponderMode_ModeC_Ident     ///< transponder is on
};


/// @brief defines information about an aircraft visible to radar.
/// @note Only information used by XPMP2 is `"mode != xpmpTransponderMode_Standby"`,
///       in which case the plane is considered for TCAS display.
struct XPMPPlaneRadar_t {
    long                    size = sizeof(XPMPPlaneRadar_t);    ///< structure size
    long                    code = 0;                           ///< current radar code
    XPMPTransponderMode     mode = xpmpTransponderMode_ModeC;   ///< current radar mode
};


/// @brief   Textual information of planes to be passed on
///          via shared dataRefs to other plugins.
/// @details The texts are not used within XPMP2 in any way, just passed on.
struct XPMPInfoTexts_t {
    long size = sizeof(XPMPInfoTexts_t);
    char tailNum[10]       = {0};       ///< registration, tail number
    char icaoAcType[5]     = {0};       ///< ICAO aircraft type designator, 3-4 chars
    char manufacturer[40]  = {0};       ///< a/c manufacturer, human readable
    char model[40]         = {0};       ///< a/c model, human readable
    char icaoAirline[4]    = {0};       ///< ICAO airline code
    char airline[40]       = {0};       ///< airline, human readable
    char flightNum [10]    = {0};       ///< flight number
    char aptFrom [5]       = {0};       ///< Origin airport (ICAO)
    char aptTo [5]         = {0};       ///< Destination airport (ICAO)
};


/// @brief This enum defines the different categories of aircraft information we can query about.
/// @note While these enums are defined in a way that they could be combined together,
///       there is no place, which makes use of this possibility. Each value will be used
///       individually only. Because of this, the definition has been changed in XPMP2
///       to an actual enum type for clarity.
/// @note This enum is only used with deprecated concepts
///       (class XPCAircraft and direct use of callback functions.)
enum XPMPPlaneDataType {
    xpmpDataType_Position   = 1L << 1,  ///< position data in XPMPPlanePosition_t
    xpmpDataType_Surfaces   = 1L << 2,  ///< physical appearance in XPMPPlaneSurfaces_t
    xpmpDataType_Radar      = 1L << 3,  ///< radar information in XPMPPlaneRadar_t
    xpmpDataType_InfoTexts  = 1L << 4,  ///< informational texts in XPMPInfoTexts_t
};


/// @brief Definfes the different responses to asking for information.
/// @note This enum is only used with deprecated concepts
///       (class XPCAircraft and direct use of callback functions.)
enum XPMPPlaneCallbackResult {
    xpmpData_Unavailable = 0,   /* The information has never been specified. */
    xpmpData_Unchanged = 1,     /* The information from the last time the plug-in was asked. */
    xpmpData_NewData = 2        /* The information has changed this sim cycle. */
};


/// @brief Unique ID for an aircraft created by a plugin.
/// @note In XPMP2 this value is no longer a pointer to an internal memory address,
///       but will directly be used as `modeS_id` in the new
///       [TCAS override](https://developer.x-plane.com/article/overriding-tcas-and-providing-traffic-information/)
///       approach.
typedef unsigned XPMPPlaneID;

/// Minimum allowed XPMPPlaneID / mode S id
constexpr XPMPPlaneID MIN_MODE_S_ID = 0x00000001;
/// Maximum allowed XPMPPlaneID / mode S id
constexpr XPMPPlaneID MAX_MODE_S_ID = 0x00FFFFFF;


/************************************************************************************
* MARK: INITIALIZATION
************************************************************************************/

// Config section is defined for legacy reasons only
#define XPMP_CFG_SEC_PLANES          "planes"               ///< Config section "planes"
#define XPMP_CFG_SEC_DEBUG           "debug"                ///< Config section "debug"

// Config key definitions
#define XPMP_CFG_ITM_CLAMPALL        "clamp_all_to_ground"  ///< Config key: Ensure no plane sinks below ground, no matter of Aircraft::bClampToGround
#define XPMP_CFG_ITM_LOGLEVEL        "log_level"            ///< Config key: General level of logging into Log.txt (0 = Debug, 1 = Info, 2 = Warning, 3 = Error, 4 = Fatal)
#define XPMP_CFG_ITM_MODELMATCHING   "model_matching"       ///< Config key: Write information on model matching into Log.txt

/// @brief Definition for the type of configuration callback function
/// @details The plugin using XPMP2 can provide such a callback function via XPMPMultiplayerInitLegacyData().
///          It will be called max. every 2s to fetch each of the following configuraton values:\n
///          \n
/// `section | key                 | type | default | description`\n
/// `------- | ------------------- | ---- | ------- | -------------------------------------------------------------------------`\n
/// `planes  | clamp_all_to_ground | int  |    1    | Ensure no plane sinks below ground, no matter of Aircraft::bClampToGround`\n
/// `debug   | log_level           | int  |    2    | General level of logging into Log.txt (0 = Debug, 1 = Info, 2 = Warning, 3 = Error, 4 = Fatal)`\n
/// `debug   | model_matching      | int  |    0    | Write information on model matching into Log.txt`\n
/// @note There is no immediate requirement to check the value of `_section` in your implementation.
///       `_key` by itself is unique. Compare it with any of the `XPMP_CFG_ITM_*` values and return your value.
/// @param _section Configuration section, ie. group of values, any of the `XPMP_CFG_SEC_*` values
/// @param _key Any of the `XPMP_CFG_ITM_*` values to indicate which config value is to be returned.
/// @param _default A default provided by XPMP2. Have your callback return '_default' if you don't want to explicitely set a value or don't know the `_key`.
/// @return Your callback shall return your config value for config item `_key`
typedef int (*XPMPIntPrefsFuncTy)(const char* _section, const char* _key, int _default);

/// @brief Deprecated legacy initialization of XPMP2.
/// @note Parameters changed compared to libxplanemp!
/// @details Effectively calls, in this order,
///          XPMPMultiplayerInit() and XPMPLoadCSLPackage().
/// @see XPMPMultiplayerInit() and XPMPLoadCSLPackage() for details on the parameters.
[[deprecated("Use XPMPMultiplayerInit and XPMPLoadCSLPackages")]]
const char *    XPMPMultiplayerInitLegacyData(const char* inCSLFolder,
                                              const char* inPluginName,
                                              const char* resourceDir,
                                              XPMPIntPrefsFuncTy inIntPrefsFunc   = nullptr,
                                              const char* inDefaultICAO           = nullptr,
                                              const char* inPluginLogAcronym      = nullptr);

/// @brief Initializes the XPMP2 library. This shall be your first call to the library.
/// @note Parameters changed compared to libxplanemp!
/// @param inPluginName Your plugin's name, used as map layer name, and as folder name under `Aircraft`
/// @param resourceDir The directory where XPMP2 finds all required supplemental files (`Doc8643.txt`, `MapIcons.png`, `related.txt`)
/// @param inIntPrefsFunc (optional) A pointer to a callback function providing integer config values. See ::XPMPIntPrefsFuncTy for details.
/// @param inDefaultICAO (optional) A fallback aircraft type if no type can be deduced otherwise for an aircraft.
/// @param inPluginLogAcronym (optional) A short text to be used in log output. If not given then `inPluginName` is used also for this purpse.
/// @return Empty string in case of success, otherwise a human-readable error message.
const char *    XPMPMultiplayerInit(const char* inPluginName,
                                    const char* resourceDir,
                                    XPMPIntPrefsFuncTy inIntPrefsFunc   = nullptr,
                                    const char* inDefaultICAO           = nullptr,
                                    const char* inPluginLogAcronym      = nullptr);

/// @brief Overrides the plugin's name to be used in Log output
/// @details The same as providing a plugin name with XPMPMultiplayerInit().
///          If no name is provided, it defaults to the plugin's name as set in XPluginStart().
/// @note    Replaces the compile-time macro `XPMP_CLIENT_LONGNAME` needed in `libxplanemp`.
/// @param inPluginName Your plugin's name, used as map layer name, and as folder name under `Aircraft`
/// @param inPluginLogAcronym (optional) A short text to be used in log output. If not given then `inPluginName` is used also for this purpse.
void XPMPSetPluginName (const char* inPluginName,
                        const char* inPluginLogAcronym      = nullptr);


/// @brief Clean up the multiplayer library
/// @details This shall be your last call to XPMP2.
///          Call this latest from XPluginStop for proper and controlled
///          cleanup of resources used by XPMP2.
void XPMPMultiplayerCleanup();


/// @brief Used to set the light textures for old OBJ7 models.
/// @note  Unsupported with XPMP2, will always return "OBJ7 format is no longer supported"
[[deprecated("Unsupported feature, will alsways return 'OBJ7 format is no longer supported'")]]
const char * XPMPMultiplayerOBJ7SupportEnable(const char * inTexturePath);


/************************************************************************************
* MARK: AI / Multiplayer plane control
************************************************************************************/


/// @brief Tries to grab control of AI/multiplayer planes from X-Plane
/// @details Only one plugin can have AI/multiplayer control at any one time.
///          So be prepared to properly handle an non-empty response
///          that indicates that you did not get control now.\n
///          If successful, XPMP2's aircraft will appear on TCAS and are
///          available to 3rd-party plugins relying on multiplayer dataRefs.\n
///          Typically, a plugin calls this function early in the initialization,
///          just before the first aircraft are created. But it could also
///          be bound to a menu item, for example.
/// @return  Empty string on success, human-readable error message otherwise
const char *    XPMPMultiplayerEnable();


/// @brief Release AI/multiplayer control.
/// @details Afterwards, XPMP2's aircraft will no longer appear on TCAS or
///          on 3rd-party plugins relying on multiplayer dataRefs.\n
///          Is called during XPMPMultiplayerCleanup() automatically.
void XPMPMultiplayerDisable();


/// Has XPMP2 control of AI/multiplayer aircraft? (See XPMPMultiplayerEnable())
bool XPMPHasControlOfAIAircraft();

/************************************************************************************
* MARK: CSL Package Handling
************************************************************************************/

/// @brief Loads CSL packages from the given folder, searching up to 5 folder levels deep.
/// @details This function mainly searches the given folder for packages.
///          It traverses all folders in search for `xsb_aircraft.txt` files.
///          It will find all `xsb_aircraft.txt` files situated at or below
///          the given root path, up to 5 levels deep.\n
///          As soon as such a file is found it is read and processed.
///          Depth search then ends. (But searching parallel folders may
///          still continue.)\n
///          The `xsb_aircraft.txt` is loaded and processed. Duplicate models
///          (by CSL model name, tag `OBJ8_AIRCRAFT`) are ignored.
///          For others the existence of the `.obj` file is validated,
///          but not the existence of files in turn needed by the `.obj`
///          file, like textures. If validated successfully the model is added to an
///          internal catalogue.\n
///          Actual loading of objects is done later and asynchronously only
///          when needed during aircraft creation.
/// @param inCSLFolder Root folder to start the search.
const char *    XPMPLoadCSLPackage(const char * inCSLFolder);


/// @brief Legacy function only provided for backwards compatibility. Does not actually do anything.
[[deprecated("No longer needed, does not do anything.")]]
void            XPMPLoadPlanesIfNecessary();


/// @brief Returns the number of loaded CSL models
int XPMPGetNumberOfInstalledModels();

/*
 * XPMPGetModelInfo
 *
 * Call this routine with an index to get all available info for this model. Valid
 * index is between 0 and XPMPGetNumberOfInstalledModels(). If you pass an index
 * out of this range, the out parameters are unchanged.
 * Make sure the size of all char arrays is big enough.
 *
 */

/// @brief Fetch information about a CSL model identified by an index
/// @note Index numbers may change if more models are loaded, don't rely on them.
///       Model name is unique.
/// @deprecated Instead, use XPMPGetModelInfo2().\n
///       This legacy function is defined with pointers to text arrays as
///       return type. This is rather unsafe. XPMP2's internal maps and buffers
///       may change if more models are loaded, hence pointers may become invalid.
///       Don't rely on these pointers staying valid over an extended period of time.
/// @param inIndex Number between `0` and `XPMPGetNumberOfInstalledModels()-1
/// @param[out] outModelName Receives pointer to model name (id). Optional, can be nullptr.
/// @param[out] outIcao Receives pointer to ICAO aircraft type designator. Optional, can be `nullptr`.
/// @param[out] outAirline Receives pointer to ICAO airline code. Optional, can be `nullptr`.
/// @param[out] outLivery Receives pointer to special livery string. Optional, can be `nullptr`.
[[deprecated("Unsafe, use XPMPGetModelInfo2() instead.")]]
void XPMPGetModelInfo(int inIndex, const char **outModelName, const char **outIcao, const char **outAirline, const char **outLivery);

/// @brief Fetch information about a CSL model identified by an index
/// @note Index numbers may change if more models are loaded, don't rely on them.
///       Model name is unique.
/// @param inIndex Number between `0` and `XPMPGetNumberOfInstalledModels()-1
/// @param[out] outModelName Receives model name (id)
/// @param[out] outIcao Receives ICAO aircraft designator
/// @param[out] outAirline Receives ICAO airline code
/// @param[out] outLivery Receives special livery string
void XPMPGetModelInfo2(int inIndex, std::string& outModelName,  std::string& outIcao, std::string& outAirline, std::string& outLivery);


/// @brief Tests model match quality based on the given parameters.
/// @param inICAO ICAO aircraft type designator, optional, can be `nullptr`
/// @param inAirline ICAO airline code, optional, can be `nullptr`
/// @param inLivery Special livery text, optional, can be `nullptr`
/// @return Match quality, the lower the better
int         XPMPModelMatchQuality(const char *              inICAO,
                                  const char *              inAirline,
                                  const char *              inLivery);


/// @brief Is `inICAO` a valid ICAO aircraft type designator?
bool            XPMPIsICAOValid(const char *                inICAO);


/************************************************************************************
 * MARK: PLANE CREATION API
 ************************************************************************************/

// TODO: Provide versions which return Aircraft*


/// @brief Callback function your plugin provides to return updates plane data
/// @note  This type is only used with deprecated concepts
///        (direct use of callback functions.)
/// @details This functions is called by XPMP2 once per cycle per plane per data type.
///          Your implementation returns the requested data,
///          so that XPMP2 can move and update the associated aircraft instance.
/// @param inPlane ID of the plane, for which data is requested
/// @param inDataType The type of data that is requested, see ::XPMPPlaneDataType
/// @param[out] ioData A pointer to a structure XPMP2 provides, for you to fill the data into.
///                    For its type see ::XPMPPlaneDataType.
/// @param inRefcon The refcon value you provided when creating the plane
typedef XPMPPlaneCallbackResult (* XPMPPlaneData_f)(XPMPPlaneID         inPlane,
                                                    XPMPPlaneDataType   inDataType,
                                                    void *              ioData,
                                                    void *              inRefcon);


/// @brief Creates a new plane
/// @deprecated Subclass XPMP2::Aircraft instead
/// @details Effectively calls XPMPCreatePlaneWithModelName() with
///          `inModelName = nullptr`
/// @param inICAOCode ICAO aircraft type designator, like 'A320', 'B738', 'C172'
/// @param inAirline ICAO airline code, like 'BAW', 'DLH', can be an empty string
/// @param inLivery Special livery designator, can be an empty string
/// @param inDataFunc Callback function called by XPMP2 to fetch updated data
/// @param inRefcon A refcon value passed back to you in all calls to the `inDataFunc`
/// @param inModeS_id (optional) Unique identification of the plane [0x01..0xFFFFFF], e.g. the 24bit mode S transponder code. XPMP2 assigns an arbitrary unique number of not given
[[deprecated("Subclass XPMP2::Aircraft instead")]]
XPMPPlaneID XPMPCreatePlane(const char *            inICAOCode,
                            const char *            inAirline,
                            const char *            inLivery,
                            XPMPPlaneData_f         inDataFunc,
                            void *                  inRefcon,
                            XPMPPlaneID             inModeS_id = 0);

/// @brief Creates a new plane, providing a specific CSL model name
/// @deprecated Subclass XPMP2::Aircraft instead
/// @param inModelName CSL Model name (id) to use, or `nullptr` if normal matching using the next 3 parameters shall be applied
/// @param inICAOCode ICAO aircraft type designator, like 'A320', 'B738', 'C172'
/// @param inAirline ICAO airline code, like 'BAW', 'DLH', can be an empty string
/// @param inLivery Special livery designator, can be an empty string
/// @param inDataFunc Callback function called by XPMP2 to fetch updated data
/// @param inRefcon A refcon value passed back to you in all calls to the `inDataFunc`
/// @param inModeS_id (optional) Unique identification of the plane [0x01..0xFFFFFF], e.g. the 24bit mode S transponder code. XPMP2 assigns an arbitrary unique number of not given
[[deprecated("Subclass XPMP2::Aircraft instead")]]
XPMPPlaneID XPMPCreatePlaneWithModelName(const char *           inModelName,
                                         const char *           inICAOCode,
                                         const char *           inAirline,
                                         const char *           inLivery,
                                         XPMPPlaneData_f        inDataFunc,
                                         void *                 inRefcon,
                                         XPMPPlaneID            inModeS_id = 0);

/// @brief [Deprecated] Removes a plane previously created with XPMPCreatePlane()
/// @deprecated Delete subclassed XPMP2::Aircraft object instead.
[[deprecated("Delete subclassed XPMP2::Aircraft object instead")]]
void            XPMPDestroyPlane(XPMPPlaneID);


/// @brief Show/Hide the aircraft temporarily without destroying the object
void            XPMPSetPlaneVisibility(XPMPPlaneID _id, bool _bVisible);


/// @brief  Perform model matching again and change the CSL model to the resulting match
/// @note   Effectively calls XPMP2::Aircraft::ChangeModel(),
///         so if you have the aircraft object, prefer call that function directly.
/// @param inPlaneID Which plane to change?
/// @param inICAOCode ICAO aircraft type designator, like 'A320', 'B738', 'C172'
/// @param inAirline ICAO airline code, like 'BAW', 'DLH', can be an empty string
/// @param inLivery Special livery designator, can be an empty string
/// @return Match quality, the lower the better
int     XPMPChangePlaneModel(XPMPPlaneID            inPlaneID,
                             const char *           inICAOCode,
                             const char *           inAirline,
                             const char *           inLivery);

/*
 * XPMPGetPlaneModelName
 *
 * Return the name of the model in use
 * Returns required buf size, i.e. length of description.
 * Negative values indicate failure (wrong PlaneID).
 *
 */
int     XPMPGetPlaneModelName(
                              XPMPPlaneID             inPlaneID,
                              char *                  outTxtBuf,
                              int                     outTxtBufSize);


/*
 * XPMPGetPlaneICAOAndLivery
 *
 * Given a plane, this function returns optionally its ICAO code or livery.  Pass string buffers
 * or NULL if you do not want the information.
 *
 */
void            XPMPGetPlaneICAOAndLivery(
                                          XPMPPlaneID               inPlane,
                                          char *                    outICAOCode,    // Can be NULL
                                          char *                    outLivery);     // Can be NULL

/*
 * XPMPGetPlaneData
 *
 * This function fetches specific data about a plane in the sim.  Pass in a plane ID, a data type
 * and a pointer to a struct for the data.  The struct's size field must be filled in!  The data
 * will be returned if possible, as well as an enum code indicating whether we are returning new
 * data, old data, or we have no data at all.
 *
 */
/// @note   Not supported in XPMP2!
///         In the legacy library this is actually more like an internal function,
///         calling all callback functions for querying aircraft data
///         (much like XPCAircraft::UpdatePosition() is now in XPMP2).
///         I doubt that it was intended to be called from the outside in the first place.
XPMPPlaneCallbackResult XPMPGetPlaneData(
                                         XPMPPlaneID                    inPlane,
                                         XPMPPlaneDataType          inDataType,
                                         void *                     outData);

/*
 * XPMPGetPlaneModelQuality
 *
 * This function returns the quality level for the nominated plane's
 * current model.
 */
int         XPMPGetPlaneModelQuality(
                                     XPMPPlaneID                inPlane);

/*
 * XPMPCountPlanes
 *
 * This function returns the number of planes in existence.
 *
 */
long            XPMPCountPlanes(void);

/*
 * XPMPGetNthPlane
 *
 * This function returns the plane ID of the Nth plane.
 *
 */
XPMPPlaneID XPMPGetNthPlane(
                            long                    index);


/*
 * XPMPSetDefaultPlaneICAO
 *
 * This routine controls what ICAO is used as a backup search criteria for a not-found plane.
 *
 */
void    XPMPSetDefaultPlaneICAO(
                                const char *            inICAO);

/************************************************************************************
 * MARK: PLANE OBSERVATION API
 ************************************************************************************/

// TODO: Provide versions which return Aircraft*

/*
 * XPMPPlaneNotification
 *
 * These are the various notifications you receive when you register a notifier
 * function.
 *
 */
enum XPMPPlaneNotification {
    xpmp_PlaneNotification_Created          = 1,
    xpmp_PlaneNotification_ModelChanged     = 2,
    xpmp_PlaneNotification_Destroyed        = 3
};

/*
 * XPMPPlaneNotifier_f
 *
 * You can pass a notifier to find out when a plane is created or destroyed or other
 * data changes.
 *
 */
typedef void (* XPMPPlaneNotifier_f)(
                                     XPMPPlaneID                inPlaneID,
                                     XPMPPlaneNotification  inNotification,
                                     void *                 inRefcon);

/*
 * XPMPRegisterPlaneCreateDestroyFunc
 *
 * This function registers a notifier functionfor obeserving planes being created and destroyed.
 *
 */
void            XPMPRegisterPlaneNotifierFunc(
                                              XPMPPlaneNotifier_f       inFunc,
                                              void *                    inRefcon);

/*
 * XPMPUnregisterPlaneCreateDestroyFunc
 *
 * This function canceles a registration for a notifier functionfor obeserving
 * planes being created and destroyed.
 */
void            XPMPUnregisterPlaneNotifierFunc(
                                                XPMPPlaneNotifier_f     inFunc,
                                                void *                  inRefcon);

/************************************************************************************
 * MARK: PLANE RENDERING API (unsued in XPMP2)
 ************************************************************************************/

/*
 * XPMPRenderPlanes_f
 *
 * You can register a callback to draw planes yourself.  If you do this, the XPMP will not
 * draw multiplayer planes out the cockpit window; do it yourself!  Use the data access API
 * to get plane info and then draw them.  You are responsible for all planes.
 *
 */
typedef void (* XPMPRenderPlanes_f)(
                                    int                         inIsBlend,
                                    void *                      inRef);

/*
 * XPMPSetPlaneRenderer
 *
 * This function setse the plane renderer.  You can pass NULL for the function to restore
 * the default renderer.
 *
 */
/// @warning Unsupported in XPMP2. The function is available to stay compile-time compatible, but it does nothing.
[[deprecated("Unsupported")]]
void        XPMPSetPlaneRenderer(
                                 XPMPRenderPlanes_f         inRenderer,
                                 void *                         inRef);

/*
 * XPMPDumpOneCycle
 *
 * This causes the plane renderer implementation to dump debug info to the error.out for one
 * cycle after it is called - useful for figuring out why your models don't look right.
 *
 */
void        XPMPDumpOneCycle(void);

/*
 * XPMPEnableAircraftLabels
 * XPMPDisableAircraftLabels
 *
 * These functions enable and disable the drawing of aircraft labels above the aircraft
 *
 */
void XPMPEnableAircraftLabels (bool _enable = true);

void XPMPDisableAircraftLabels();

bool XPMPDrawingAircraftLabels();

//
// MARK: MAP
//       Enable or disable the drawing of icons on maps
//

/// @brief Enable or disable the drawing of aircraft icons on X-Plane's map
/// @param _bEnable Enable or disable entire map functionality
/// @param _bLabels If map is enabled, shall also labels be drawn?
/// @details XPMP2 creates a separate Map Layer named after the plugin for this purposes.
///          By default, the map functionality is enabled including label writing.
void XPMPEnableMap (bool _bEnable, bool _bLabels = true);

#ifdef __cplusplus
}
#endif


#endif
