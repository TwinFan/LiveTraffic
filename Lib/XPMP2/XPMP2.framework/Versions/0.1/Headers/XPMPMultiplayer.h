/// @file       XPMPMultiplayer.h
/// @brief      Initialization and general control functions for XPMP2
/// @note       This file bases on and should be compile-compatible to the header
///             provided with the original libxplanemp.
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

#include "XPLMDefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 Multiplayer - THEORY OF OPERATION
 
 The multiplayer API allows plug-ins to control aircraft visible to other plug-ins and
 the user via x-plane.  It effectively provides glue between a series of observers
 that wish to render or in other ways act upon those planes.
 
 A plug-in can control zero or more planes, and zero or more plug-ins can control planes.
 However, each plane is controlled by exactly one plug-in.  A plug-in thus dynamically
 allocates planes to control.  A plug-in registers a callback which is used to pull
 information.  The plug-in may decide to not return information or state that that
 information is unchanged.
 
 A plug-in can also read the current aircrafts or any of their data.  Aircraft data is
 cached to guarantee minimum computing of data.
 
 Each 'kind' of data has an enumeration and corresponding structure.*/


/************************************************************************************
 * MARK: PLANE DATA TYPES
 ************************************************************************************/

/// XPMPPosition_t
///
/// @brief This data structure contains the basic position info for an aircraft.
/// @details    Lat and lon are the position of the aircraft in the world.  They are double-precision to
///             provide reasonably precise positioning anywhere.\n
///             Elevation is in feet above mean sea level.\n
///             Pitch, roll, and heading define the aircraft's orientation.  Heading is in degrees, positive
///             is clockwise from north.  Pitch is the number of degrees, positive is nose up, and roll
///             is positive equals roll right.\n
///             Offset scale should be between 0 & 1 and indicates how much of the surface
///             contact correction offset should be applied.  1 is fully corrected, 0 is no
///             correction.  This is so XSB can blend the correction out as the aircraft
///             leaves circling altitude.\n
///             clampToGround enables the ground-clamping inside of libxplanemp.  If false,
///             libxplanemp will not clamp this particular aircraft.\n
/// @note       There is no notion of aircraft velocity or acceleration; you will be queried for
///             your position every rendering frame.  Higher level APIs can use velocity and acceleration.
struct XPMPPlanePosition_t {
    long    size            = sizeof(XPMPPlanePosition_t);  ///< size of structure
    double  lat             = 0.0;                      ///< current position of aircraft
    double  lon             = 0.0;                      ///< current position of aircraft
    double  elevation       = 0.0;                      ///< current altitude of aircraft [ft above MSL]
    float   pitch           = 0.0f;                     ///< pitch [degrees, psitive up]
    float   roll            = 0.0f;                     ///< roll [degrees, positive right]
    float   heading         = 0.0f;                     ///< heading [degrees]
    char    label[32]       = "";                       ///< label to show with the aircraft
    float   offsetScale     = 1.0f;                     ///< how much of the surface contact correction offset should be applied [0..1]
    bool    clampToGround   = true;                     ///< enables ground-clamping for this aircraft
    int     aiPrio          = 1;                        ///< Priority for AI/TCAS consideration, the lower the earlier
    float   label_color[4]  = {1.0f,1.0f,0.0f,1.0f};    ///< label base color (RGB)
    int     multiIdx        = 0;                        ///< OUT: set by xplanemp to inform application about multiplay index in use
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
typedef unsigned int XPMPLightsPattern;

/*
 * XPMPLightStatus
 *
 * This enum defines the settings for the lights bitfield in XPMPPlaneSurfaces_t
 *
 * The upper 16 bit of the light code (timeOffset) should be initialized only once
 * with a random number by the application. This number will be used to have strobes
 * flashing at different times.
 */
#if LIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // because we don't want to change the anonymous structure following here as that would require code change in legacy plugins
#elif IBM
#pragma warning(push)
#pragma warning(disable: 4202 4201)
#endif
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


/*
 * XPMPTransponderMode
 *
 * These enumerations define the way the transponder of a given plane is operating.
 *
 */
/// @note Only information used by XPMP2 is `"mode != xpmpTransponderMode_Standby"`,
///       in which case the plane is considered for TCAS display.
enum XPMPTransponderMode {
    xpmpTransponderMode_Standby,        ///< transponder is in standby, not currently sending ->plane  not visible on TCAS
    xpmpTransponderMode_Mode3A,         ///< transponder is on
    xpmpTransponderMode_ModeC,          ///< transponder is on
    xpmpTransponderMode_ModeC_Low,      ///< transponder is on
    xpmpTransponderMode_ModeC_Ident     ///< transponder is on
};

/*
 * XPMPPlaneRadar_t
 *
 * This structure defines information about an aircraft visible to radar.  Eventually it can include
 * information about radar profiles, stealth technology, radar jamming, etc.
 *
 */
/// @note Only information used by XPMP2 is `"mode != xpmpTransponderMode_Standby"`,
///       in which case the plane is considered for TCAS display.
struct XPMPPlaneRadar_t {
    long                    size = sizeof(XPMPPlaneRadar_t);    ///< structure size
    long                    code = 0;                           ///< current radar code
    XPMPTransponderMode     mode = xpmpTransponderMode_ModeC;   ///< current radar mode
};

/*
 * XPMPTexts_t;
 *
 * This structure define textual information of planes to be passed on
 * via shared dataRefs to other plugins. It is not used within xplanemp
 * in any way, just passed on.
 *
 */
struct XPMPInfoTexts_t {
    long size = sizeof(XPMPInfoTexts_t);
    char tailNum[10]       = {0};       ///< registration, tail number
    char icaoAcType[5]     = {0};       ///< ICAO aircraft type, 3-4 chars
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
enum XPMPPlaneDataType {
    xpmpDataType_Position   = 1L << 1,  ///< position data in XPMPPlanePosition_t
    xpmpDataType_Surfaces   = 1L << 2,  ///< physical appearance in XPMPPlaneSurfaces_t
    xpmpDataType_Radar      = 1L << 3,  ///< radar information in XPMPPlaneRadar_t
    xpmpDataType_InfoTexts  = 1L << 4,  ///< informational texts in XPMPInfoTexts_t
};

/*
 * XPMPPlaneCallbackResult
 *
 * This definfes the different responses to asking for information.
 *
 */
enum XPMPPlaneCallbackResult {
    xpmpData_Unavailable = 0,   /* The information has never been specified. */
    xpmpData_Unchanged = 1,     /* The information from the last time the plug-in was asked. */
    xpmpData_NewData = 2        /* The information has changed this sim cycle. */
};


/// @brief Unique ID for an aircraft created by a plugin.
/// @note In XPMP2 this value is no longer a pointer to an internal memory address,
///       but just an ever increasing number. Don't use it as a pointer.
typedef void *      XPMPPlaneID;

/************************************************************************************
 * Some additional functional by den_rain
 ************************************************************************************/

void actualVertOffsetInfo(const char *inMtl, char *outType, double *outOffset);
void setUserVertOffset(const char *inMtlCode, double inOffset);
void removeUserVertOffset(const char *inMtlCode);


/************************************************************************************
* MARK: INITIALIZATION
************************************************************************************/

/*
 * XPMPMultiplayerInitLegacyData
 *
 * This routine initializes legacy portions of the multiplayer library.
 *
 * inPlaneList is a ptr to a fully qualified file name that is a text file of ICAO -> model
 * mappings.  The xsb_aircraft.txt file can be used as a template.  (Please note: other
 * XSB files are NOT necessary and are not necessarily available under open source license.)
 *
 * inDoc8643 is the path to the ICAO document 8643, available at
 * http://www.icao.int/anb/ais/TxtFiles/Doc8643.txt. This file lists all aircraft types
 * with their manufacturer, ICAO equipment code and aircraft category.
 *
 * The two prefs funcs each take an ini section and key and return a value and also take
 * a default.  The renderer uses them for configuration.  Currently the following keys are
 * needed:
 *
 * section  key                 type    default description
 * planes   full_distance       float   3.0
 * planes   max_full_count      int     50
 *
 * The return value is a string indicating any problem that may have gone wrong in a human-readable
 * form, or an empty string if initalizatoin was okay.
 *
 * Call this once from your XPluginStart routine.
 *
 * Depending on which plane packages are installed this can take between 30 seconds and 15 minutes.
 *
 * After transitioning to exclusively OBJ8-based packages, this function should no longer be required.
 * Instead, make separate calls to XPMPLoadCSLPackages and XPMPSetDefaultPlaneICAO.
 *
 */
const char *    XPMPMultiplayerInitLegacyData(const char * inCSLFolder      = nullptr,
                                              const char * inRelatedPath    = nullptr,
                                              const char * inTexturePath    = nullptr,
                                              const char * inDoc8643        = nullptr,
                                              const char * inDefaultICAO    = nullptr,
                                              int (* inIntPrefsFunc)(const char *, const char *, int)       = nullptr,
                                              float (* inFloatPrefsFunc)(const char *, const char *, float) = nullptr,
                                              const char * inMapIconFile    = nullptr);

/*
 * XPMPMultiplayerInit
 *
 * The two prefs funcs each take an ini section and key and return a value and also take
 * a default.  The renderer uses them for configuration.  Currently the following keys are
 * needed:
 *
 * section  key                 type    default description
 * planes   full_distance       float   3.0     (unused in XPMP2)
 * planes   max_full_count      int     100     (unused in XPMP2)
 * planes   clamp_all_to_ground int     0
 * planes   dr_libxplanemp      int     1
 * debug    model_matching      int     0
 * debug    log_level           int     2       (new in XPMP2, 2 = Warning, see Utilities.h::logLevelTy)
 *
 * Additionally takes a string path to the resource directory of the calling plugin for storing the
 * user vertical offset config file.
 *
 * The return value is a string indicating any problem that may have gone wrong in a human-readable
 * form, or an empty string if initalizatoin was okay.
 *
 * Call this once, typically from your XPluginStart routine.
 *
 */
const char *    XPMPMultiplayerInit(int (* inIntPrefsFunc)(const char *, const char *, int)         = nullptr,
                                    float (* inFloatPrefsFunc)(const char *, const char *, float)   = nullptr,
                                    const char * resourceDir = nullptr);

/// @brief Overrides the plugin's name to be used in Log output
/// @details The name in use defaults to the plugin's name as set in XPluginStart().
///          Replaces the compile-time macro `XPMP_CLIENT_LONGNAME` from earlier releases.
void XPMPSetPluginName (const char* inPluginName);

/*
 * XPMPMultiplayerCleanup
 *
 * Clean up the multiplayer library. Call this from XPluginStop to reverse the actions of
 * XPMPMultiplayerInit as much as possible.
 */
void XPMPMultiplayerCleanup();

/*
 * XPMPMultiplayerOBJ7SupportEnable
 *
 * Sets the light texture to use for old OBJ7 models and initializes the required OpenGL hooks
 * for OBJ7 rendering. An empty string is returned on success, or a human-readable error message
 * otherwise. Calling this function is required if you are going to use OBJ7 CSLs.
 *
 * UNSUPPORTED, will alsways return "OBJ7 format is no longer supported"
 */
const char * XPMPMultiplayerOBJ7SupportEnable(const char * inTexturePath);

/************************************************************************************
* MARK: AI / Multiplayer plane control
************************************************************************************/

/*
 * XPMPMultiplayerEnable
 *
 * Enable drawing of multiplayer planes.  Call this once from your XPluginEnable routine to
 * grab multiplayer; an empty string is returned on success, or a human-readable error message
 * otherwise.
 *
 */
const char *    XPMPMultiplayerEnable();

/*
 * XPMPMultiplayerDisable
 *
 * Disable drawing of multiplayer planes.  Call this from XPluginDisable to release multiplayer.
 * Reverses the actions on XPMPMultiplayerEnable.
 */
void XPMPMultiplayerDisable();

/*
 * XPMPHasControlOfAIAircraft
 *
 * Does XPMP control AI aircrafts (after a call to XPMPMultiplayerEnable)
 * and, hence, can fake TCAS display?
 */
bool XPMPHasControlOfAIAircraft();

/************************************************************************************
* MARK: CSL Package Handling
************************************************************************************/

/*
 * XPMPLoadCSLPackage
 *
 * Loads a collection of planes
 *
 * inPlaneList is a ptr to a fully qualified file name that is a text file of ICAO -> model
 * mappings.  The xsb_aircraft.txt file can be used as a template.  (Please note: other
 * XSB files are NOT necessary and are not necessarily available under open source license.)
 *
 * inDoc8643 is the path to the ICAO document 8643, available at
 * http://www.icao.int/anb/ais/TxtFiles/Doc8643.txt. This file lists all aircraft types
 * with their manufacturer, ICAO equipment code and aircraft category.
 *
 * This is fast if the planes are all OBJ8 (because the objects are loaded asynchronously,
 * on demand), otherwise it could take several seconds or minutes depending on the quantity
 * and fidelity of the planes.
 *
 */
const char *    XPMPLoadCSLPackage(const char * inCSLFolder,
                                   const char * inRelatedPath   = nullptr,
                                   const char * inDoc8643       = nullptr);

/*
 * XPMPLoadPlanesIfNecessary
 *
 * This routine checks what planes are loaded and loads any that we didn't get.
 * Call it after you oare enabled if it isn't the first time to set up models.
 *
 */
void            XPMPLoadPlanesIfNecessary();

/*
 * XPMPGetNumberOfInstalledModels
 *
 * This routine returns the number of found models.
 *
 */
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
void XPMPGetModelInfo(int inIndex, const char **outModelName, const char **outIcao, const char **outAirline, const char **outLivery);

/*
 * XPMPModelMatchQuality
 *
 * This functions searches through our model list and returns the pass
 * upon which a match was found, and -1 if one was not.
 *
 * This can be used for assessing if it's worth using a partial update
 * to update the model vs previous efforts.
 */
int         XPMPModelMatchQuality(
                                  const char *              inICAO,
                                  const char *              inAirline,
                                  const char *              inLivery);

/*
 * XPMPIsICAOValid
 *
 * This functions searches through our global vector of valid ICAO codes and returns true if there
 * was a match and false if there wasn't.
 *
 */
bool            XPMPIsICAOValid(const char *                inICAO);


/************************************************************************************
 * MARK: PLANE CREATION API
 ************************************************************************************/

// TODO: Provide versions which return Aircraft*


/*
 * XPMPPlaneData_f
 *
 * This is the aircraft data providing function.  It is called no more than once per sim
 * cycle per data type by the plug-in manager to get data about your plane.  The data passed
 * in is a pointer to one of the above structures.  The function specifies the datatype, and the
 * last data you provided is passed in.
 *
 */
typedef XPMPPlaneCallbackResult (* XPMPPlaneData_f)(
                                                    XPMPPlaneID         inPlane,
                                                    XPMPPlaneDataType   inDataType,
                                                    void *              ioData,
                                                    void *              inRefcon);

/*
 * XPMPCreatePlane
 *
 * This function creates a new plane for a plug-in and returns it.  Pass in an ICAO aircraft ID code,
 * a livery string and a data function for fetching dynamic information.
 *
 */
[[deprecated("Subclass XPMP2::Aircraft instead")]]
XPMPPlaneID XPMPCreatePlane(
                            const char *            inICAOCode,
                            const char *            inAirline,
                            const char *            inLivery,
                            XPMPPlaneData_f         inDataFunc,
                            void *                  inRefcon);

[[deprecated("Subclass XPMP2::Aircraft instead")]]
XPMPPlaneID XPMPCreatePlaneWithModelName(
                                         const char *           inModelName,
                                         const char *           inICAOCode,
                                         const char *           inAirline,
                                         const char *           inLivery,
                                         XPMPPlaneData_f            inDataFunc,
                                         void *                  inRefcon);

/// @brief [Deprecated] Deallocates a created aircraft.
/// @deprecated Delete subclassed XPMP2::Aircraft object instead.
[[deprecated("Delete subclassed XPMP2::Aircraft object instead")]]
void            XPMPDestroyPlane(XPMPPlaneID);


/// @brief Show/Hide the aircraft temporarily without destroying the object
void            XPMPSetPlaneVisibility(XPMPPlaneID _id, bool _bVisible);


/*
 * XPMPChangePlaneModel
 *
 * This routine lets you change an aircraft's model.  This can be useful if a remote
 * player changes planes or new information comes over the network asynchronously.
 *
 * this function returns an integer which, abstractly, represents the match quality.
 *
 * lower numbers are better, 2 or lower indicates an exact match on model.
 * negative values indicate failure to match at all.
 *
 */
int     XPMPChangePlaneModel(
                             XPMPPlaneID                inPlaneID,
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

/// Enable or disable the drawing of aircraft icons on X-Plane's map in a separate Layer names after the plugin
void XPMPEnableMap (bool _bEnable);

#ifdef __cplusplus
}
#endif


#endif
