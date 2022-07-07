/// @file       DataRefs.h
/// @brief      Access to global data like dataRefs, `doc8643` and `model_typecode` files.
/// @details    Defines classes Doc8643, DataRefs, and read access to `model_typecode` file.\n
///             There is exactly one instance of DataRefs, which is the global variable `dataRefs`,
///             in which all globally relevant values are stored, beyond just XP's dataRefs:\n
///             - LiveTraffic's configuration options including reading/writing of the config file\n
///             - readable callbacks for other plugins' access to LiveTraffic's data
///             - LTAPI interface
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

#ifndef DataRefs_h
#define DataRefs_h

#include "XPLMDataAccess.h"
#include "TextIO.h"
#include "CoordCalc.h"

class RealTrafficConnection;

//
// MARK: Defaults
//

const int DEF_MAX_NUM_AC        = 50;           ///< how many aircraft to create at most?
const int DEF_FD_STD_DISTANCE   = 25;           ///< nm: miles to look for a/c around myself
const int DEF_FD_SNAP_TAXI_DIST = 15;           ///< [m]: Snapping to taxi routes in a max distance of this many meter (0 -> off)
const int DEF_FD_REFRESH_INTVL  = 20;           ///< how often to fetch new flight data
const int DEF_FD_BUF_PERIOD     = 90;           ///< seconds to buffer before simulating aircraft
const int DEF_AC_OUTDATED_INTVL = 50;           ///< a/c considered outdated if latest flight data more older than this compare to 'now'
const int MIN_NETW_TIMEOUT      =  5;           ///< [s] minimum network request timeout
const int DEF_NETW_TIMEOUT      = 90;           ///< [s] of network request timeout


constexpr int DEF_UI_FONT_SCALE = 100;  ///< [%] Default font scaling
constexpr int DEF_UI_OPACITY    =  25;  ///< [%] Default background opacity

//
// MARK: Doc8643
//
class Doc8643 {
public:
    std::string manufacturer;
    std::string model;
    std::string typeDesignator;
    std::string classification;
    std::string wtc;
public:
    Doc8643 () {}
    Doc8643 (std::string&& _manufacturer,
             std::string&& _model,
             std::string&& _typeDesignator,
             std::string&& _classification,
             std::string&& _wtc);
    
    // copying and moving is all as per default
public:
    Doc8643 (const Doc8643& o) = default;
    Doc8643 (Doc8643&& o) = default;
    Doc8643& operator = (const Doc8643& o) = default;
    Doc8643& operator = (Doc8643&& o) = default;
    
    // 'model' is key, so let's base all comparison on it
public:
    bool operator == (const Doc8643& o) const { return model == o.model; }
    bool operator < (const Doc8643& o)  const { return model < o.model; }
    bool operator > (const Doc8643& o)  const { return model > o.model; }
    operator bool () const { return !model.empty(); }
    
    // return the string for FlightModel matching
    operator std::string() const;
    
    // Helicopter or Gyrocopter with big rotor?
    inline bool hasRotor () const
    { return classification.size() >= 1 ?
        (classification[0] == 'H' || classification[0] == 'G') : false;
    }
    
    // static functions for reading the doc8643.txt file
    // and returning information from it
public:
    static bool ReadDoc8643File ();
    static const Doc8643& get (const std::string& _type);
};

//
// MARK: ModelIcaoType
//

/// @brief Represents an entry in the `model_typecode.txt` file
/// @details The `model_typecode.txt` file matches
/// non-standardized human-readable `model` entries in
/// tracking data (especially in OpenSky's data) to an ICAO a/c type code.
/// The file has been created by forum user crbascott.
/// @see https://forums.x-plane.org/index.php?/forums/topic/188206-matching-lacks-icao-ac-type-code/
namespace ModelIcaoType
{
    /// Read the `model_typecode.txt` file
    bool ReadFile ();
    /// Lookup ICAO type designator for human-readable model text, empty if nothing found
    const std::string& getIcaoType (const std::string& _model);
}


//
// MARK: Screen coordinate helpers
//

/// 2D window position
struct WndPos {
    int x = 0;
    int y = 0;
    
    /// Shift both values
    void shift (int _dx, int _dy) { x += _dx; y += _dy; }
    
    /// Return a shifted copy
    WndPos shiftedBy (int _dx, int _dy) const
    { WndPos ret = *this; ret.shift(_dx,_dy); return ret; }
};

/// 2D rectagle
struct WndRect {
    WndPos tl;          ///< top left
    WndPos br;          ///< bottom right
    
    /// Default Constructor -> all zero
    WndRect () {}
    /// Constructor takes four ints as a convenience
    WndRect (int _l, int _t, int _r, int _b) :
    tl{_l, _t}, br{_r, _b} {}
    /// Constructor taking two positions
    WndRect (const WndPos& _tl, const WndPos& _br) :
    tl(_tl), br(_br) {}
    
    // Accessor to individual coordinates
    int     left () const   { return tl.x; }    ///< reading left
    int&    left ()         { return tl.x; }    ///< writing left
    int     top () const    { return tl.y; }    ///< reading top
    int&    top ()          { return tl.y; }    ///< writing top
    int     right () const  { return br.x; }    ///< reading right
    int&    right ()        { return br.x; }    ///< writing right
    int     bottom () const { return br.y; }    ///< reading bottom
    int&    bottom ()       { return br.y; }    ///< writing bottom
    
    int     width () const  { return right() - left(); }    ///< width
    int     height () const { return top() - bottom(); }    ///< height
    
    /// Does window contain the position?
    bool    contains (const WndPos& _p) const
    { return
        left()   <= _p.x && _p.x <= right() &&
        bottom() <= _p.y && _p.y <= top(); }
    
    /// Clear all to zero
    void    clear () { tl.x = tl.y = br.x = br.y = 0; }
    /// Is all zeroes?
    bool    empty () const { return !tl.x && !tl.y && !br.x && !br.y; }
    
    /// Shift position right/down
    WndRect& shift (int _dx, int _dy)
    { tl.shift(_dx,_dy); br.shift(_dx,_dy); return *this; }
    
    /// Set from config file string ("left,top,right.bottom")
    void    set (const std::string& _s);
};

/// Write WndRect into config file ("left,top,right.bottom")
std::ostream& operator<< (std::ostream& _stream, const WndRect& _r);


//
// MARK: DataRefs
//

// from LTAircraft.h
class LTAircraft;

enum pluginStateTy {
    STATE_STOPPED = 0,  // before init; after stop
    STATE_INIT,         // after init, before enabled; after disabled, before stop
    STATE_ENABLED,      // after enabled; before disabled
    STATE_SHOW_AC       // enabled + showing aircraft
};


// XP standard Datarefs being accessed
enum dataRefsXP {
    DR_MISC_NETW_TIME = 0,
    DR_LOCAL_TIME_SEC,
    DR_LOCAL_DATE_DAYS,
    DR_USE_SYSTEM_TIME,
    DR_ZULU_TIME_SEC,
    DR_REPLAY_MODE,                     ///< sim/operation/prefs/replay_mode    int    y    enum    Are we in replay mode?
    DR_VIEW_EXTERNAL,
    DR_VIEW_TYPE,
    DR_MODERN_DRIVER,                   // sim/graphics/view/using_modern_driver: boolean: Vulkan/Metal in use?

    DR_CAMERA_TCAS_IDX,                 ///< Shared data ref created by us: If LiveTraffic's camera is on, then on which aircraft? Here: TCAS index
    DR_CAMERA_AC_ID,                    ///< Shared data ref created by us: If LiveTraffic's camera is on, then on which aircraft? Here: aircraft id (usually ICAO hex code)

    DR_PLANE_LAT,                       // user's plane
    DR_PLANE_LON,
    DR_PLANE_ELEV,
    DR_PLANE_PITCH,
    DR_PLANE_ROLL,
    DR_PLANE_HEADING,
    DR_PLANE_MAG_HEADING,               ///< sim/flightmodel/position/mag_psi    float    n    degrees    The real magnetic heading of the aircraft
    DR_PLANE_TRACK,
    DR_PLANE_KIAS,                      ///< sim/flightmodel/position/indicated_airspeed    float    y    kias    Air speed indicated - this takes into account air density and wind direction
    DR_PLANE_TAS,                       ///< sim/flightmodel/position/true_airspeed    float    n    meters/sec    Air speed true - this does not take into account air density at altitude!
    DR_PLANE_GS,                        ///< sim/flightmodel/position/groundspeed    float    n    meters/sec    The ground speed of the aircraft
    DR_PLANE_VVI,                       ///< sim/flightmodel/position/vh_ind    float    n    meters/second    VVI (vertical velocity in meters per second)
    DR_PLANE_ONGRND,
    DR_PLANE_REG,                       ///< sim/aircraft/view/acf_tailnum    byte[40]    y    string    Tail number
    DR_PLANE_MODES_ID,                  ///< sim/aircraft/view/acf_modeS_id    int    y    integer    24bit (0-16777215 or 0-0xFFFFFF) unique ID of the airframe. This is also known as the ADS-B "hexcode".
    DR_PLANE_ICAO,                      ///< sim/aircraft/view/acf_ICAO    byte[40]    y    string    ICAO code for aircraft (a string) entered by author
    DR_WIND_DIR,                        ///< sim/weather/wind_direction_degt    float    n    [0-359)    The effective direction of the wind at the plane's location.
    DR_WIND_SPEED,                      ///< sim/weather/wind_speed_kt    float    n    msc    >= 0        The effective speed of the wind at the plane's location. WARNING: this dataref is in meters/second - the dataref NAME has a bug.
    DR_VR_ENABLED,                      // VR stuff
    CNT_DATAREFS_XP                     // always last, number of elements
};

enum cmdRefsXP {
    CR_NO_COMMAND = -1,                 // initialization placeholder
    CR_GENERAL_LEFT = 0,                // first 16 commands grouped together
    CR_GENERAL_RIGHT,                   // they move the spot on latitude (Z) and lonigtude (X)
    CR_GENERAL_LEFT_FAST,               // there actual movement twords Z and X depends on current heading
    CR_GENERAL_RIGHT_FAST,
    CR_GENERAL_FORWARD,
    CR_GENERAL_BACKWARD,
    CR_GENERAL_FORWARD_FAST,
    CR_GENERAL_BACKWARD_FAST,
    CR_GENERAL_HAT_SWITCH_LEFT,         // hat switch
    CR_GENERAL_HAT_SWITCH_RIGHT,
    CR_GENERAL_HAT_SWITCH_UP,
    CR_GENERAL_HAT_SWITCH_DOWN,
    CR_GENERAL_HAT_SWITCH_UP_LEFT,
    CR_GENERAL_HAT_SWITCH_UP_RIGHT,
    CR_GENERAL_HAT_SWITCH_DOWN_LEFT,
    CR_GENERAL_HAT_SWITCH_DOWN_RIGHT,
    CR_GENERAL_UP,                      // up/down -> change altitude
    CR_GENERAL_DOWN,
    CR_GENERAL_UP_FAST,
    CR_GENERAL_DOWN_FAST,
    CR_GENERAL_ROT_LEFT,                // rotate/turn -> change heading
    CR_GENERAL_ROT_RIGHT,
    CR_GENERAL_ROT_LEFT_FAST,
    CR_GENERAL_ROT_RIGHT_FAST,
    CR_GENERAL_ROT_UP,                  // rotate/tilt -> change pitch
    CR_GENERAL_ROT_DOWN,
    CR_GENERAL_ROT_UP_FAST,
    CR_GENERAL_ROT_DOWN_FAST,
    CR_GENERAL_ZOOM_IN,                 // zoom
    CR_GENERAL_ZOOM_OUT,
    CR_GENERAL_ZOOM_IN_FAST,
    CR_GENERAL_ZOOM_OUT_FAST,           // last command registered for camera movement
    
    CR_VIEW_FREE_CAM,               ///< sim/view/free_camera                               Free camera.
    CR_VIEW_FWD_2D,                 ///< sim/view/forward_with_2d_panel                     Forward with 2-D panel.
    CR_VIEW_FWD_HUD ,               ///< sim/view/forward_with_hud                          Forward with HUD.
    CR_VIEW_FWD_NODISP ,            ///< sim/view/forward_with_nothing                      Forward with nothing.
    CR_VIEW_EXT_LINEAR ,            ///< sim/view/linear_spot                               Linear spot.
    CR_VIEW_EXT_STILL ,             ///< sim/view/still_spot                                Still spot.
    CR_VIEW_EXT_RNWY ,              ///< sim/view/runway                                    Runway.
    CR_VIEW_EXT_CIRCLE ,            ///< sim/view/circle                                    Circle.
    CR_VIEW_EXT_TOWER ,             ///< sim/view/tower                                     Tower.
    CR_VIEW_EXT_RIDE ,              ///< sim/view/ridealong                                 Ride-along.
    CR_VIEW_EXT_WEAPON ,            ///< sim/view/track_weapon                              Track weapon.
    CR_VIEW_EXT_CHASE ,             ///< sim/view/chase                                     Chase.
    CR_VIEW_FWD_3D ,                ///< sim/view/3d_cockpit_cmnd_look                      3-D cockpit.
    
    CNT_CMDREFS_XP                      // always last, number of elements
};

enum XPViewTypes {
    VIEW_UNKNOWN    = 0,
    VIEW_FWD_2D     = 1000, ///< sim/view/forward_with_2d_panel                     Forward with 2-D panel.
    VIEW_EXT_TOWER  = 1014, ///< sim/view/tower                                     Tower.
    VIEW_EXT_RNWY   = 1015, ///< sim/view/runway                                    Runway.
    VIEW_EXT_CHASE  = 1017, ///< sim/view/chase                                     Chase.
    VIEW_EXT_CIRCLE = 1018, ///< sim/view/circle                                    Circle.
    VIEW_EXT_STILL  = 1020, ///< sim/view/still_spot                                Still spot.
    VIEW_EXT_LINEAR = 1021, ///< sim/view/linear_spot                               Linear spot.
    VIEW_FWD_HUD    = 1023, ///< sim/view/forward_with_hud                          Forward with HUD.
    VIEW_FWD_NODISP = 1024, ///< sim/view/forward_with_nothing                      Forward with nothing.
    VIEW_FWD_3D     = 1026, ///< sim/view/3d_cockpit_cmnd_look                      3-D cockpit.
    VIEW_FREE_CAM   = 1028, ///< sim/view/free_camera                               Free camera
    VIEW_EXT_RIDE   = 1031, ///< sim/view/ridealong                                 Ride-along.
};

// Datarefs offered by LiveTraffic
enum dataRefsLT {
    // a/c information
    DR_AC_KEY = 0,                      // a/c info read/write
    DR_AC_NUM,                          // int a/c info
    DR_AC_ON_GND,
    DR_AC_PHASE,
    DR_AC_LAT,                          // float a/c info
    DR_AC_LON,
    DR_AC_ALT,
    DR_AC_HEADING,
    DR_AC_ROLL,
    DR_AC_PITCH,
    DR_AC_SPEED,
    DR_AC_VSI,
    DR_AC_TERRAIN_ALT,
    DR_AC_HEIGHT,
    DR_AC_FLAPS,
    DR_AC_GEAR,
    DR_AC_LIGHTS_BEACON,
    DR_AC_LIGHTS_STROBE,
    DR_AC_LIGHTS_NAV,
    DR_AC_LIGHTS_LANDING,
    DR_AC_BEARING,
    DR_AC_DIST,                         // last of a/c info
    
    DR_AC_BULK_QUICK,               // bulk a/c primarily for communication with LTAPI
    DR_AC_BULK_EXPENSIVE,           // similar, but for expensive data, should be called less often
    
    DR_SIM_DATE,
    DR_SIM_TIME,
    
    DR_LT_VER,                      ///< LiveTraffic's version number, like 201 for v2.01
    DR_LT_VER_DATE,                 ///< LiveTraffic's version date, like 20200430 for 30-APR-2020
    
    // UI information
    DR_UI_OPACITY,
    DR_UI_FONT_SCALE,
    DR_UI_SETTINGS_TRANSP,
    DR_UI_ACI_COLLAPSED,
    
    // configuration options
    DR_CFG_AIRCRAFT_DISPLAYED,
    DR_CFG_AUTO_START,
    DR_CFG_AI_ON_REQUEST,
    DR_CFG_AI_UNDER_CONTROL,
    DR_CFG_AI_NOT_ON_GND,
    DR_CFG_LABELS,
    DR_CFG_LABEL_SHOWN,
    DR_CFG_LABEL_MAX_DIST,
    DR_CFG_LABEL_VISIBILITY_CUT_OFF,
    DR_CFG_LABEL_COL_DYN,
    DR_CFG_LABEL_COLOR,
    DR_CFG_LOG_LEVEL,
    DR_CFG_MSG_AREA_LEVEL,
    DR_CFG_LOG_LIST_LEN,
    DR_CFG_MAX_NUM_AC,
    DR_CFG_FD_STD_DISTANCE,
    DR_CFG_FD_SNAP_TAXI_DIST,
    DR_CFG_FD_REFRESH_INTVL,
    DR_CFG_FD_BUF_PERIOD,
    DR_CFG_AC_OUTDATED_INTVL,
    DR_CFG_NETW_TIMEOUT,
    DR_CFG_LND_LIGHTS_TAXI,
    DR_CFG_HIDE_BELOW_AGL,
    DR_CFG_HIDE_TAXIING,
    DR_CFG_HIDE_PARKING,
    DR_CFG_HIDE_NEARBY_GND,
    DR_CFG_HIDE_NEARBY_AIR,
    DR_CFG_HIDE_IN_REPLAY,
    DR_CFG_HIDE_STATIC_TWR,
    DR_CFG_COPY_OBJ_FILES,
    DR_CFG_REMOTE_SUPPORT,
    DR_CFG_EXTERNAL_CAMERA,
    DR_CFG_LAST_CHECK_NEW_VER,
    
    // debug options
    DR_DBG_AC_FILTER,
    DR_DBG_AC_POS,
    DR_DBG_LOG_RAW_FD,
    DR_DBG_MODEL_MATCHING,
    DR_DBG_EXPORT_FD,
    DR_DBG_EXPORT_USER_AC,
    DR_DBG_EXPORT_NORMALIZE_TS,
    DR_DBG_EXPORT_FORMAT,

    // channel configuration options
    DR_CFG_FSC_ENV,
    DR_CFG_OGN_USE_REQUREPL,
    DR_CFG_RT_LISTEN_PORT,
    DR_CFG_RT_TRAFFIC_PORT,
    DR_CFG_RT_WEATHER_PORT,
    DR_CFG_FF_SEND_PORT,
    DR_CFG_FF_SEND_USER_PLANE,
    DR_CFG_FF_SEND_TRAFFIC,
    DR_CFG_FF_SEND_TRAFFIC_INTVL,

    // channels, in ascending order of priority
    DR_CHANNEL_FUTUREDATACHN_ONLINE,    // placeholder, first channel
    DR_CHANNEL_FORE_FLIGHT_SENDER,
    DR_CHANNEL_FSCHARTER,
    DR_CHANNEL_OPEN_GLIDER_NET,
    DR_CHANNEL_ADSB_EXCHANGE_ONLINE,
    DR_CHANNEL_ADSB_EXCHANGE_HISTORIC,
    DR_CHANNEL_OPEN_SKY_ONLINE,
    DR_CHANNEL_OPEN_SKY_AC_MASTERDATA,
    DR_CHANNEL_REAL_TRAFFIC_ONLINE,     // currently highest-prio channel
    // always last, number of elements:
    CNT_DATAREFS_LT
};

enum cmdRefsLT {
    CR_INFO_LIST_WND = 0,
    CR_ACINFOWND_OPEN,
    CR_ACINFOWND_OPEN_POPPED_OUT,
    CR_ACINFOWND_HIDE_SHOW,
    CR_ACINFOWND_CLOSE_ALL,
    CR_AC_DISPLAYED,
    CR_AC_TCAS_CONTROLLED,
    CR_LABELS_TOGGLE,
    CR_SETTINGS_UI,
    CNT_CMDREFS_LT                      // always last, number of elements
};

/// Which format to use for exporting flight tracking data
enum exportFDFormat {
    EXP_FD_AITFC = 1,                   ///< use AITFC format, the older shorter format
    EXP_FD_RTTFC,                       ///< user RTTFC format, introduced with RealTraffic v9
};

// first/last channel; number of channels:
constexpr int DR_CHANNEL_FIRST  = DR_CHANNEL_FUTUREDATACHN_ONLINE;
constexpr int DR_CHANNEL_LAST   = CNT_DATAREFS_LT-1;
constexpr int CNT_DR_CHANNELS   = DR_CHANNEL_LAST+1 - DR_CHANNEL_FIRST;

class DataRefs
{
public:
    //MARK: dataRefDefinitionT
    struct dataRefDefinitionT {
    protected:
        std::string dataName;
        XPLMDataTypeID dataType     = xplmType_Unknown;
        XPLMGetDatai_f ifRead       = NULL;
        XPLMSetDatai_f ifWrite      = NULL;
        XPLMGetDataf_f ffRead       = NULL;
        XPLMSetDataf_f ffWrite      = NULL;
        XPLMGetDatab_f bfRead       = NULL;
        void* refCon                = NULL;
        bool bCfgFile               = false;
        bool bDebugLog              = false;    ///< log this setting in case of DEBUG logging?
        
    public:
        // constructor for xplmType_Int
        dataRefDefinitionT (const char* name,
                            XPLMGetDatai_f _ifRead, XPLMSetDatai_f _ifWrite = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false,
                            bool _bDebugLog = false) :
        dataName(name), dataType(xplmType_Int),
        ifRead(_ifRead), ifWrite(_ifWrite),
        refCon(_refCon), bCfgFile(_bCfg), bDebugLog(_bDebugLog) {}

        // constructor for xplmType_Float
        dataRefDefinitionT (const char* name,
                            XPLMGetDataf_f _ffRead, XPLMSetDataf_f _ffWrite = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false,
                            bool _bDebugLog = false) :
        dataName(name), dataType(xplmType_Float),
        ffRead(_ffRead), ffWrite(_ffWrite),
        refCon(_refCon), bCfgFile(_bCfg), bDebugLog(_bDebugLog) {}

        // constructor for xplmType_Data
        dataRefDefinitionT (const char* name,
                            XPLMGetDatab_f _bfRead, XPLMSetDataf_f /*_bfWrite*/ = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false,
                            bool _bDebugLog = false) :
        dataName(name), dataType(xplmType_Data),
        bfRead(_bfRead), 
        refCon(_refCon), bCfgFile(_bCfg), bDebugLog(_bDebugLog) {}
        
        // allows using the object in string context -> dataName
        inline const std::string getDataNameStr() const { return dataName; }
        inline const char* getDataName() const { return dataName.c_str(); }
        inline operator const char* () const { return getDataName(); }
        inline bool operator == (const dataRefDefinitionT& o) { return dataName == o.dataName; }
        
        inline bool isWriteable () const { return (dataType == xplmType_Int)   ? (ifWrite != NULL) :
                                                  (dataType == xplmType_Float) ? (ffWrite != NULL) : false; }
        inline XPLMDataTypeID getDataTpe () const { return dataType; }
        inline XPLMGetDatai_f getDatai_f () const { return ifRead; }
        inline XPLMSetDatai_f setDatai_f () const { return ifWrite; }
        inline XPLMGetDataf_f getDataf_f () const { return ffRead; }
        inline XPLMSetDataf_f setDataf_f () const { return ffWrite; }
        inline XPLMGetDatab_f getDatab_f () const { return bfRead; }

        inline XPLMDataTypeID getDataType() const { return dataType; }
        inline void* getRefCon() const { return refCon; }
        inline void setRefCon (void* _refCon) { refCon = _refCon; }
        inline bool isCfgFile() const { return bCfgFile; }
        bool isDebugLogging() const { return bDebugLog; }
        
        // get the actual current value (by calling the getData?_f function)
        int getDatai () const;
        float getDataf () const;
        
        // set the value
        void setData (int i);
        void setData (float f);
        void setData (const std::string& s);
        
        // returns the string to be stored in a config file
        std::string GetConfigString() const;
    };
    
    // which elements make up an a/c label?
    struct LabelCfgTy {
        unsigned
        // static info
        bIcaoType : 1,              // default
        bAnyAcId : 1,               // default
        bTranspCode : 1,
        bReg : 1,
        bIcaoOp : 1,
        bCallSign : 1,
        bFlightNo : 1,
        bRoute : 1,
        // dynamic info
        bPhase : 1,
        bHeading : 1,
        bAlt : 1,                   // default
        bHeightAGL : 1,
        bSpeed : 1,                 // default
        bVSI : 1;
        
        // this is a bit ugly but avoids a wrapper union with an int
        inline unsigned GetUInt() const { return *reinterpret_cast<const unsigned*>(this); }
        inline void SetUInt(unsigned i) { *reinterpret_cast<unsigned*>(this) = i; }
        inline bool operator != (const LabelCfgTy& o) const
        { return GetUInt() != o.GetUInt(); }
    };
    
    // when to show a/c labels?
    struct LabelShowCfgTy {
        unsigned
        bExternal : 1,              // external/outside views
        bInternal : 1,              // internal/cockpit views
        bVR       : 1,              // VR views
        bMap      : 1;              // Map icons

        // this is a bit ugly but avoids a wrapper union with an int
        inline unsigned GetUInt() const { return *reinterpret_cast<const unsigned*>(this); }
        inline void SetUInt(unsigned i) { *reinterpret_cast<unsigned*>(this) = i; }
        inline bool operator != (const LabelShowCfgTy& o) const
        { return GetUInt() != o.GetUInt(); }
    };
    
    /// represents a line in the [CSLPath] section of LiveTrafic.prg
    struct CSLPathCfgTy {
    public:
        bool        bEnabled = false;   ///< enabled for auto-load on startup
    protected:
        int         bPathExists = 0;    ///< 3-values: -1 no, 0 not tested, 1 yes
        std::string path;               ///< actual path, can be relative to X-Plane system path
        
    public:
        CSLPathCfgTy () {}
        CSLPathCfgTy (bool b, const std::string& p);
        bool empty() const   { return path.empty(); }
        bool enabled() const { return bEnabled && !empty(); }
        const std::string& getPath() const { return path; }
        bool existsSave();              ///< tests path for existence, saves test result
        bool exists() const;            ///< tests path for existence
        const std::string& operator= (const std::string& _p);  ///< assign new path
        bool operator== (const CSLPathCfgTy& o) const { return path == o.path; }
        bool operator== (const std::string& s) const { return path == s; }
    };
    typedef std::vector<CSLPathCfgTy> vecCSLPaths;
    
public:
    pluginStateTy pluginState = STATE_STOPPED;
#ifdef DEBUG
    bool bSimVREntered = false;                 // for me to simulate some aspects of VR
    double fdBufDebug  = 0.0;                   // Due to debugging, the buffering period might extend a lot...
#endif
    
//MARK: DataRefs
protected:
    XPLMDataRef adrXP[CNT_DATAREFS_XP];                 ///< array of XP data refs to read from and shared dataRefs to provide
    XPLMDataRef adrLT[CNT_DATAREFS_LT];                 // array of data refs LiveTraffic provides
public:
    XPLMCommandRef cmdXP[CNT_CMDREFS_XP];               // array of command refs
    XPLMCommandRef cmdLT[CNT_CMDREFS_LT];

//MARK: Provided Data, i.e. global variables
protected:
    std::thread::id xpThread;        ///< id of X-Plane's thread (when it is OK to use XP API calls)
    XPLMPluginID pluginID       = 0;
    logLevelTy iLogLevel        = logWARN;
    logLevelTy iMsgAreaLevel    = logINFO;
#ifdef DEBUG
    int logListLen              = 500;  ///< number of log message kept in storage to show in Info List Window
#else
    int logListLen              = 100;  ///< number of log message kept in storage to show in Info List Window
#endif
    int bShowingAircraft        = false;
    unsigned uDebugAcFilter     = 0;    // icao24 for a/c filter
    int bDebugAcPos             = false;// output debug info on position calc into log file?
    int bDebugLogRawFd          = false;// log raw flight data to LTRawFD.log
    exportFDFormat eDebugExportFdFormat = EXP_FD_AITFC; ///< Which format to use when exporting flight data?
    int bDebugExportFd          = false;// export flight data to LTExportFD.csv
    int bDebugExportUserAc      = false;///< export user's aircraft data to LTExportFD.csv
    float lastExportUserAc      = 0.0f; ///< last time user's aircraft data has been written to export file
    int bDebugExportNormTS      = true; ///< normalize the timestamp when writing LTExportFD.csv, starting at 0 by the time exporting starts
    int bDebugModelMatching     = false;// output debug info on model matching in xplanemp?
    std::string XPSystemPath;
    std::string LTPluginPath;           // path to plugin directory
    std::string DirSeparator;
    int bChannel[CNT_DR_CHANNELS];      // is channel enabled?
    double chTsOffset           = 0.0f; // offset of network time compared to system clock
    int chTsOffsetCnt           = 0;    // how many offset reports contributed to the calculated average offset?
    int iTodaysDayOfYear        = 0;
    time_t tStartThisYear = 0, tStartPrevYear = 0;
    int lastCheckNewVer         = 0;    // when did we last check for updates? (hours since the epoch)
    
    float lstLatRef = NAN;              ///< last lat_ref, ie. known local coordinate system's reference point
    float lstLonRef = NAN;              ///< last lon_ref, ie. known local coordinate system's reference point
    
    // generic config values
    int bAutoStart          = true;     ///< shall display a/c right after startup?
    int bAIonRequest        = false;    ///< acquire multiplayer control for TCAS on request only, not automatically?
    bool bAwaitingAIControl = false;    ///< have in vain tried acquiring AI control and are waiting for callback now?
    int bAINotOnGnd         = false;    ///< shall a/c on the ground be hidden from TCAS/AI?
    // which elements make up an a/c label?
    LabelCfgTy labelCfg = { 0,1,0,0,0,0,0,0, 0,0,0,0,0,0 };
    LabelShowCfgTy labelShown = { 1, 1, 1, 1 };     ///< when to show? (default: always)
    int labelMaxDist    = 3;            ///< [nm] max label distance
    bool bLabelVisibilityCUtOff = true; ///< cut off labels at reported visibility?
    bool bLabelColDynamic  = false;     // dynamic label color?
    int labelColor      = COLOR_YELLOW;             ///< label color, by default yellow
    int maxNumAc        = DEF_MAX_NUM_AC;           ///< how many aircraft to create at most?
    int fdStdDistance   = DEF_FD_STD_DISTANCE;      ///< nm: miles to look for a/c around myself
    int fdSnapTaxiDist  = DEF_FD_SNAP_TAXI_DIST;    ///< [m]: Snapping to taxi routes in a max distance of this many meter (0 -> off)
    int fdRefreshIntvl  = DEF_FD_REFRESH_INTVL;     ///< how often to fetch new flight data
    int fdBufPeriod     = DEF_FD_BUF_PERIOD;        ///< seconds to buffer before simulating aircraft
    int acOutdatedIntvl = DEF_AC_OUTDATED_INTVL;    ///< a/c considered outdated if latest flight data more older than this compare to 'now'
    int netwTimeout     = DEF_NETW_TIMEOUT;         ///< [s] of network request timeout
    int bLndLightsTaxi = false;         // keep landing lights on while taxiing? (to be able to see the a/c as there is no taxi light functionality)
    int hideBelowAGL    = 0;            // if positive: a/c visible only above this height AGL
    int hideTaxiing     = 0;            // hide a/c while taxiing?
    int hideParking     = 0;            ///< hide a/c parking at a startup-position (gate, ramp)?
    int hideNearbyGnd   = 0;            // [m] hide a/c if closer than this to user's aircraft on the ground
    int hideNearbyAir   = 0;            // [m] hide a/c if closer than this to user's aircraft in the air
    int hideInReplay    = false;        ///< Shall no planes been shown while in Replay mode (to avoid collisions)?
    int hideStaticTwr   = true;         ///< filter out TWR objects from the channels
    int cpyObjFiles     = 1;            ///< copy `.obj` files for replacing dataRefs and textures
    int remoteSupport   = 0;            ///< support XPMP2 Remote Client? (3-way: -1 off, 0 auto, 1 on)
    int bUseExternalCamera  = false;    ///< Do not activate LiveTraffic's camera view when hitting the camera button (intended for a 3rd party camera plugin to activate instead based on reading livetraffic/camera/... dataRefs or using LTAPI)

    // channel config options
    int fscEnv          = 0;            ///< FSCharter: Which environment to connect to?
    int ognUseRequRepl  = 0;            ///< OGN: Use Request/Reply instead of TCP receiver
    int rtListenPort    = 10747;        // port opened for RT to connect
    int rtTrafficPort   = 49003;        // UDP Port receiving traffic
    int rtWeatherPort   = 49004;        // UDP Port receiving weather info
    int ffSendPort      = 49002;        // UDP Port to send ForeFlight feeding data
    int bffUserPlane    = 1;            // bool Send User plane data?
    int bffTraffic      = 1;            // bool Send traffic data?
    int ffSendTrfcIntvl = 3;            // [s] interval to broadcast traffic info

    vecCSLPaths vCSLPaths;              // list of paths to search for CSL packages
    
    std::string sDefaultAcIcaoType  = CSL_DEFAULT_ICAO_TYPE;
    std::string sDefaultCarIcaoType = CSL_CAR_ICAO_TYPE;
    std::string sOpenSkyUser;           ///< OpenSky Network user
    std::string sOpenSkyPwd;            ///< OpenSky Network password
    std::string sADSBExAPIKey;          ///< ADS-B Exchange API key
    std::string sFSCUser;               ///< FSCharter login user
    std::string sFSCPwd;                ///< FSCharter login password
    
    // live values
    bool bReInitAll     = false;        // shall all a/c be re-initiaized (e.g. time jumped)?
    
    int cntAc           = 0;            // number of a/c being displayed
    std::string keyAc;                  // key (transpIcao) for a/c whose data is returned
    const LTAircraft* pAc = nullptr;    // ptr to that a/c
    
    // Weather
    double      altPressCorr_ft = 0.0;  ///< [ft] barometric correction for pressure altitude, in meter
    float       lastWeatherAttempt = 0.0f;  ///< last time we _tried_ to update the weather
    float       lastWeatherUpd = 0.0f;  ///< last time the weather was updated? (in XP's network time)
    float       lastWeatherHPA = HPA_STANDARD; ///< last barometric pressure received
    positionTy  lastWeatherPos;         ///< last position for which weather was retrieved
    std::string lastWeatherStationId;   ///< last weather station we got weather from
    std::string lastWeatherMETAR;       ///< last full METAR string
    
//MARK: Debug helpers (public)
public:
    std::string cslFixAcIcaoType;       // set of fixed values to use for...
    std::string cslFixOpIcao;           // ...newly created aircraft for...
    std::string cslFixLivery;           // ...CSL model package testing
    
// MARK: Public members
public:
    /// once per Flarm a/c type: matching it to one or more ICAO types
    std::array<std::vector<std::string>, 14> aFlarmToIcaoAcTy;

    long OpenSkyRRemain = LONG_MAX;     ///< OpenSky: Remaining number of requests per day
    std::string OpenSkyRetryAt;         ///< OpenSky: If limit is reached, when to retry? (local time as string)
    long ADSBExRLimit = 0;              // ADSBEx: Limit on RapidAPI
    long ADSBExRRemain = 0;             // ADSBEx: Remaining Requests on RapidAPI
    
    // UI information
    int UIopacity = DEF_UI_OPACITY;     ///< [%] UI opacity
    int UIFontScale = DEF_UI_FONT_SCALE; ///< [%] Font scale

    // Settings UI
    WndRect SUIrect;                    ///< Settings UI Window position
    int SUItransp = 0;                  ///< Settings UI: transaprent background?
    
    // A/C Info Window(s)
    WndRect ACIrect;                    ///< A/C Info Window position
    int ACIcollapsed = 0;               ///< A/C Info Wnd collapsed sections status

    // Info List Window (Status etc)
    WndRect ILWrect;                    ///< Info List Window position

//MARK: Constructor
public:
    DataRefs ( logLevelTy initLogLevel );               // Constructor doesn't do much
    bool Init();                                        // Init DataRefs, return "OK?"
    void InformDataRefEditors();                        ///< tell DRE and DRT our dataRefs
    void Stop();                                        // unregister what's needed
    
protected:
    // call XPLMRegisterDataAccessor
    bool RegisterDataAccessors();
    bool RegisterCommands();
    void* getVarAddr (dataRefsLT dr);

//MARK: DataRef access, partly cached for thread-safe access
protected:
    static positionTy lastCamPos;               ///< cached read camera position
    float       lastNetwTime    = 0.0f;         ///< cached network time
    double      lastSimTime     = NAN;          ///< cached simulated time
    bool        lastReplay      = true;         ///< cached: is replay mode?
    bool        lastVREnabled   = false;        ///< cached info: VR enabled?
    bool        bUsingModernDriver = false;     ///< modern driver in use?
    positionTy  lastUsersPlanePos;              ///< cached user's plane position
    double      lastUsersTrueAirspeed = 0.0;    ///< [m/s] cached user's plane's air speed
    double      lastUsersTrack        = 0.0;    ///< cacher user's plane's track
    vectorTy    lastWind;                       ///< wind at user's plane's location
public:
    void ThisThreadIsXP() { xpThread = std::this_thread::get_id();  }
    bool IsXPThread() const { return std::this_thread::get_id() == xpThread; }
    float GetMiscNetwTime() const;
    inline bool  IsViewExternal() const         { return XPLMGetDatai(adrXP[DR_VIEW_EXTERNAL]) != 0; }
    inline XPViewTypes GetViewType () const     { return (XPViewTypes)XPLMGetDatai(adrXP[DR_VIEW_TYPE]); }
    inline bool UsingModernDriver () const      { return bUsingModernDriver; }
    inline bool  IsVREnabled() const            { return lastVREnabled; }

    inline void SetLocalDateDays(int days)      { XPLMSetDatai(adrXP[DR_LOCAL_DATE_DAYS], days); }
    inline void SetUseSystemTime(bool bSys)     { XPLMSetDatai(adrXP[DR_USE_SYSTEM_TIME], (int)bSys); }
    inline void SetZuluTimeSec(float sec)       { XPLMSetDataf(adrXP[DR_ZULU_TIME_SEC], sec); }
    void SetViewType(XPViewTypes vt);
    positionTy GetUsersPlanePos(double& trueAirspeed_m, double& track) const;

//MARK: DataRef provision by LiveTraffic
    // Generic Get/Set callbacks
    static int   LTGetInt(void* p);
    static float LTGetFloat(void* p);
    static void  LTSetBool(void* p, int i);
    
    // Bulk data access to transfer a lot of a/c info to LTAPI
    static int LTGetBulkAc (void* inRefcon, void * outValue,
                            int inStartIdx, int inNumAc);

protected:
    /// Find dataRef definition based on the pointer to its member variable
    static const dataRefDefinitionT* FindDRDef (void* p);
    /// Save config(change) info to the log
    static void LogCfgSetting (void* p, int val);
    // a/c info
    bool FetchPAc ();

public:
    static void LTSetAcKey(void*p, int i);
    static int LTGetAcInfoI(void* p);
    static float LTGetAcInfoF(void* p);
    
    void SetCameraAc(const LTAircraft* pCamAc); ///< sets the data of the shared datarefs to point to `ac` as the current aircraft under the camera
    static void ClearCameraAc(void*);           ///< shared dataRef callback: Whenever someone else writes to the shared dataRef we clear our a/c camera information
    
    // seconds since epoch including fractionals
    double GetSimTime() const { return lastSimTime; }
    /// Current sim time as a human readable string, including 10th of seconds
    std::string GetSimTimeString() const;
    
    // livetraffic/sim/date and .../time
    static int LTGetSimDateTime(void* p);

    /// Are we in replay mode?
    bool IsReplayMode() const { return lastReplay; }
    
    // livetraffic/cfg/aircrafts_displayed: Aircraft Displayed
    static void LTSetAircraftDisplayed(void* p, int i);
    inline int AreAircraftDisplayed() const  { return bShowingAircraft; }
    void SetAircraftDisplayed ( int bEnable );
    int ToggleAircraftDisplayed ();        // returns new status (displayed?)
    
    inline XPLMPluginID GetMyPluginId() const { return pluginID; }
    
    // livetraffic/cfg/log_level: Log Level
    static void LTSetLogLevel(void* p, int i);
    void SetLogLevel ( int i );
    void SetMsgAreaLevel ( int i );
    inline logLevelTy GetLogLevel()             { return iLogLevel; }
    inline logLevelTy GetMsgAreaLevel()         { return iMsgAreaLevel; }
    
    /// Reinit data usage
    void ForceDataReload ();
    
    // general config values
    static void LTSetCfgValue(void* p, int val);
    bool SetCfgValue(void* p, int val);
    
    // generic config access (not as fast as specific access, but good for rare access)
    static bool  GetCfgBool  (dataRefsLT dr);
    static int   GetCfgInt   (dataRefsLT dr);
    static float GetCfgFloat (dataRefsLT dr);
                     
    // specific access
    inline bool GetAutoStart() const { return bAutoStart != 0; }
    inline bool IsAIonRequest() const { return bAIonRequest != 0; }
    bool IsAINotOnGnd() const { return bAINotOnGnd != 0; }
    static int HaveAIUnderControl(void* =NULL) { return XPMPHasControlOfAIAircraft(); }
    bool AwaitingAIControl() const { return bAwaitingAIControl; }
    void SetAwaitingAIControl (bool _b) { bAwaitingAIControl = _b; }
    inline LabelCfgTy GetLabelCfg() const { return labelCfg; }
    inline LabelShowCfgTy GetLabelShowCfg() const { return labelShown; }
    inline bool IsLabelColorDynamic() const { return bLabelColDynamic; }
    inline int GetLabelColor() const { return labelColor; }
    void GetLabelColor (float outColor[4]) const;
    inline int GetMaxNumAc() const { return maxNumAc; }
    void SetMaxNumAc(int n) { maxNumAc = n; }
    inline int GetFdStdDistance_nm() const { return fdStdDistance; }
    inline int GetFdStdDistance_m() const { return fdStdDistance * M_per_NM; }
    inline int GetFdStdDistance_km() const { return fdStdDistance * M_per_NM / M_per_KM; }
    inline int GetFdSnapTaxiDist_m() const { return fdSnapTaxiDist; }
    inline int GetFdRefreshIntvl() const { return fdRefreshIntvl; }
    inline int GetFdBufPeriod() const { return fdBufPeriod; }
    inline int GetAcOutdatedIntvl() const { return acOutdatedIntvl; }
    inline int GetNetwTimeout() const { return netwTimeout; }
    inline bool GetLndLightsTaxi() const { return bLndLightsTaxi != 0; }
    inline int GetHideBelowAGL() const { return hideBelowAGL; }
    inline bool GetHideTaxiing() const { return hideTaxiing != 0; }
    inline bool GetHideParking() const { return hideParking != 0; }
    inline int GetHideNearby(bool bGnd) const   ///< return "hide nearby" config
    { return bGnd ? hideNearbyGnd : hideNearbyAir; }
    inline bool GetHideInReplay() const { return hideInReplay; }
    inline bool GetHideStaticTwr () const { return hideStaticTwr; }
    inline bool IsAutoHidingActive() const  ///< any auto-hiding activated?
    { return hideBelowAGL > 0  || hideTaxiing != 0 || hideParking != 0 ||
             hideNearbyGnd > 0 || hideNearbyAir > 0 || hideInReplay; }
    bool ShallCpyObjFiles () const { return cpyObjFiles != 0; }
    int GetRemoteSupport () const { return remoteSupport; }
    bool ShallUseExternalCamera () const { return bUseExternalCamera; }

    bool NeedNewVerCheck () const;
    void SetLastCheckedNewVerNow ();

    const vecCSLPaths& GetCSLPaths() const { return vCSLPaths; }
    vecCSLPaths& GetCSLPaths()             { return vCSLPaths; }
    bool LoadCSLPackage(const std::string& _path);
    const std::string& GetDefaultAcIcaoType() const { return sDefaultAcIcaoType; }
    const std::string& GetDefaultCarIcaoType() const { return sDefaultCarIcaoType; }
    bool SetDefaultAcIcaoType(const std::string type);
    bool SetDefaultCarIcaoType(const std::string type);
    
    // livetraffic/channel/...
    void SetChannelEnabled (dataRefsLT ch, bool bEnable);
    inline bool IsChannelEnabled (dataRefsLT ch) const { return bChannel[ch - DR_CHANNEL_FIRST]; }
    int CntChannelEnabled () const;
    
    void GetOpenSkyCredentials (std::string& user, std::string& pwd)
    { user = sOpenSkyUser; pwd = sOpenSkyPwd; }
    void SetOpenSkyUser (const std::string& user) { sOpenSkyUser = user; OpenSkyRRemain = LONG_MAX; }
    void SetOpenSkyPwd (const std::string& pwd)   { sOpenSkyPwd = pwd;   OpenSkyRRemain = LONG_MAX; }

    std::string GetADSBExAPIKey () const { return sADSBExAPIKey; }
    void SetADSBExAPIKey (std::string apiKey) { sADSBExAPIKey = apiKey; }
    
    bool SetRTTrafficPort (int port) { return SetCfgValue(&rtTrafficPort, port); }
    
    size_t GetFSCEnv() const { return (size_t)fscEnv; }
    void GetFSCharterCredentials (std::string& user, std::string& pwd)
    { user = sFSCUser; pwd = sFSCPwd; }
    void SetFSCharterUser (const std::string& user) { sFSCUser = user; }
    void SetFSCharterPwd (const std::string& pwd)   { sFSCPwd = pwd; }
    
    // timestamp offset network vs. system clock
    inline void ChTsOffsetReset() { chTsOffset = 0.0f; chTsOffsetCnt = 0; }
    inline double GetChTsOffset () const { return chTsOffset; }
    bool ChTsAcceptMore () const { return cntAc == 0 && chTsOffsetCnt < CntChannelEnabled() * 2; }
    void ChTsOffsetAdd (double aNetTS);

    // livetraffic/dbg/ac_filter: Debug a/c filter (the integer is converted to hex as an transpIcao key)
    std::string GetDebugAcFilter() const;
    static void LTSetDebugAcFilter( void* inRefcon, int i );

    // returns a/c filter if set, otherwise a/c selected for a/c info
    inline std::string GetSelectedAcKey() const
        { return uDebugAcFilter ? GetDebugAcFilter() : keyAc; }

    // livetraffic/dbg/ac_pos: Debug Positions for given a/c?
    inline bool GetDebugAcPos(const std::string& key) const
        { return bDebugAcPos && key == GetSelectedAcKey(); }
    
    inline bool GetDebugLogRawFD() const        { return bDebugLogRawFd; }
    void SetDebugLogRawFD (bool bLog)           { bDebugLogRawFd = bLog; }
    
    exportFDFormat GetDebugExportFormat() const { return eDebugExportFdFormat; }
    void SetDebugExportFormat (exportFDFormat e) { eDebugExportFdFormat = e; }
    bool GetDebugExportFD() const               { return bDebugExportFd; }
    void SetDebugExportFD (bool bExport)        { bDebugExportFd = bExport; }
    bool GetDebugExportUserAc() const           { return bDebugExportUserAc; }
    void SetDebugExportUserAc (bool bExport)    { bDebugExportUserAc = bExport; }
    void ExportUserAcData ();                   ///< Write out an export record for the user aircraft
    bool ShallExportNormalizeTS () const        { return bDebugExportNormTS; }
    
    bool AnyExportData() const                  { return GetDebugExportFD() || GetDebugExportUserAc(); }
    void SetAllExportData (bool bExport)        { SetDebugExportFD(bExport); SetDebugLogRawFD(bExport); }

    // livetraffic/dbg/model_matching: Debug Model Matching (by XPMP2)
    inline bool GetDebugModelMatching() const   { return bDebugModelMatching; }
    
    // Number of aircraft
    inline int GetNumAc() const                 { return cntAc; }
    int IncNumAc();
    int DecNumAc();

    // Get XP System Path
    inline std::string GetXPSystemPath() const  { return XPSystemPath; }
    inline std::string GetLTPluginPath() const  { return LTPluginPath; }
    inline std::string GetDirSeparator() const  { return DirSeparator; }
    
    // Load/save config file (basically a subset of LT dataRefs)
    bool LoadConfigFile();
    bool SaveConfigFile();
    
    // Re-Init
    inline bool IsReInitAll() const { return bReInitAll; }
    inline void SetReInitAll (bool b) { bReInitAll = b; }

// MARK: Updating cached values for thread-safe access
    void UpdateCachedValues ();                 ///< performs all updates of cached values
    void UpdateSimTime();                       ///< calculate simulated time
protected:
    void UpdateUsersPlanePos ();                ///< fetches user's plane position
    static void UpdateViewPos();                ///< read and cache camera position
    void UpdateSimWind ();                      ///< Update local (in sim!) wind at user's plane

    
//MARK: Processed values
public:
    static positionTy GetViewPos();            // view position in World coordinates
    static double GetViewHeading();
    static inline boundingBoxTy GetBoundingBox(double dist) // bounding box around current view pos
    { return boundingBoxTy(GetViewPos(), dist); }
    bool ShallDrawLabels() const;
    bool ShallDrawMapLabels() const { return labelShown.bMap; }
    bool ToggleLabelDraw();                 // returns new value
    
    // Weather
    bool WeatherUpdate ();              ///< check if weather updated needed, then do
    /// @brief set/update current weather
    /// @details if lat/lon ar NAN, then location of provided station is taken if found, else current camera pos
    void SetWeather (float hPa, float lat, float lon, const std::string& stationId,
                     const std::string& METAR);
    /// Compute geometric altitude [ft] from pressure altitude and current weather in a very simplistic manner good enough for the first 3,000ft
    static double WeatherAltCorr_ft (double pressureAlt_ft, double hPa)
        { return pressureAlt_ft + ((hPa - HPA_STANDARD) * FT_per_HPA); }
    /// Compute geometric altitude [ft] from pressure altitude and current weather in a very simplistic manner good enough for the first 3,000ft
    double WeatherAltCorr_ft (double pressureAlt_ft) { return pressureAlt_ft + altPressCorr_ft; }
    /// Compute geometric altitude [m] from pressure altitude and current weather in a very simplistic manner good enough for the first 3,000ft
    double WeatherAltCorr_m (double pressureAlt_m) { return pressureAlt_m + altPressCorr_ft * M_per_FT; }
    /// Compute pressure altitude [ft] from geometric altitude and current weather in a very simplistic manner good enough for the first 3,000ft
    double WeatherPressureAlt_ft (double geoAlt_ft) { return geoAlt_ft - altPressCorr_ft; }
    /// Thread-safely gets current weather info
    void GetWeather (float& hPa, std::string& stationId, std::string& METAR);
    
    /// Local (in sim!) wind at user's plane
    const vectorTy& GetSimWind () const { return lastWind; }
};

extern DataRefs::dataRefDefinitionT DATA_REFS_LT[CNT_DATAREFS_LT];

#endif /* DataRefs_h */
