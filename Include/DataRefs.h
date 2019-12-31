//
//  DataRefs.h
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef DataRefs_h
#define DataRefs_h

#include "XPLMDataAccess.h"
#include "TextIO.h"
#include "CoordCalc.h"

class RealTrafficConnection;

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
    DR_TOTAL_RUNNING_TIME_SEC = 0,
    DR_LOCAL_TIME_SEC,
    DR_LOCAL_DATE_DAYS,
    DR_USE_SYSTEM_TIME,
    DR_ZULU_TIME_SEC,
    DR_VIEW_EXTERNAL,
    DR_VIEW_TYPE,
    DR_WEATHER_BARO_SEA,                // XP's weather
    DR_WEATHER_USE_REAL,
    DR_PLANE_LAT,                       // user's plane
    DR_PLANE_LON,
    DR_PLANE_ELEV,
    DR_PLANE_PITCH,
    DR_PLANE_ROLL,
    DR_PLANE_HEADING,
    DR_PLANE_TRACK,
    DR_PLANE_TRUE_AIRSPEED,
    DR_PLANE_ONGRND,
    DR_VR_ENABLED,                      // VR stuff
    DR_PILOTS_HEAD_X,
    DR_PILOTS_HEAD_Y,
    DR_PILOTS_HEAD_Z,
    DR_PILOTS_HEAD_HEADING,
    DR_PILOTS_HEAD_PITCH,
    DR_PILOTS_HEAD_ROLL,
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
    CNT_CMDREFS_XP                      // always last, number of elements
};

enum XPViewTypes {
    VIEW_UNKNOWN    = 0,
    VIEW_FWD_2D     = 1000,
    VIEW_EXT_TOWER  = 1014,
    VIEW_EXT_RNWY   = 1015,
    VIEW_EXT_CHASE  = 1017,
    VIEW_EXT_CIRCLE = 1018,
    VIEW_EXT_STILL  = 1020,
    VIEW_EXT_LINEAR = 1021,
    VIEW_FWD_HUD    = 1023,
    VIEW_FWD_NODISP = 1024,
    VIEW_FWD_3D     = 1026,
    VIEW_FREE_CAM   = 1028,
    VIEW_EXT_RIDE   = 1031,
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
    
    // configuration options
    DR_CFG_AIRCRAFT_DISPLAYED,
    DR_CFG_AUTO_START,
    DR_CFG_AI_ON_REQUEST,
    DR_CFG_AI_UNDER_CONTROL,
    DR_CFG_LABELS,
    DR_CFG_LABEL_SHOWN,
    DR_CFG_LABEL_COL_DYN,
    DR_CFG_LABEL_COLOR,
    DR_CFG_LOG_LEVEL,
    DR_CFG_MSG_AREA_LEVEL,
    DR_CFG_USE_HISTORIC_DATA,
    DR_CFG_MAX_NUM_AC,
    DR_CFG_MAX_FULL_NUM_AC,
    DR_CFG_FULL_DISTANCE,
    DR_CFG_FD_STD_DISTANCE,
    DR_CFG_FD_REFRESH_INTVL,
    DR_CFG_FD_BUF_PERIOD,
    DR_CFG_AC_OUTDATED_INTVL,
    DR_CFG_NETW_TIMEOUT,
    DR_CFG_LND_LIGHTS_TAXI,
    DR_CFG_HIDE_BELOW_AGL,
    DR_CFG_HIDE_TAXIING,
    DR_CFG_DR_LIBXPLANEMP,
    DR_CFG_LAST_CHECK_NEW_VER,
    
    // debug options
    DR_DBG_AC_FILTER,
    DR_DBG_AC_POS,
    DR_DBG_LOG_RAW_FD,
    DR_DBG_MODEL_MATCHING,
    
    // channel configuration options
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
    CR_ACINFOWND_OPEN = 0,
    CR_ACINFOWND_OPEN_POPPED_OUT,
    CR_ACINFOWND_HIDE_SHOW,
    CR_ACINFOWND_CLOSE_ALL,
    CR_AC_DISPLAYED,
    CR_AC_TCAS_CONTROLLED,
    CR_LABELS_TOGGLE,
    CNT_CMDREFS_LT                      // always last, number of elements
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
        
    public:
        // constructor for xplmType_Int
        dataRefDefinitionT (const char* name,
                            XPLMGetDatai_f _ifRead, XPLMSetDatai_f _ifWrite = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false) :
        dataName(name), dataType(xplmType_Int),
        ifRead(_ifRead), ifWrite(_ifWrite),
        refCon(_refCon), bCfgFile(_bCfg) {}

        // constructor for xplmType_Float
        dataRefDefinitionT (const char* name,
                            XPLMGetDataf_f _ffRead, XPLMSetDataf_f _ffWrite = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false) :
        dataName(name), dataType(xplmType_Float),
        ffRead(_ffRead), ffWrite(_ffWrite),
        refCon(_refCon), bCfgFile(_bCfg) {}

        // constructor for xplmType_Data
        dataRefDefinitionT (const char* name,
                            XPLMGetDatab_f _bfRead, XPLMSetDataf_f /*_bfWrite*/ = NULL,
                            void* _refCon = NULL,
                            bool _bCfg = false) :
        dataName(name), dataType(xplmType_Data),
        bfRead(_bfRead), 
        refCon(_refCon), bCfgFile(_bCfg) {}
        
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
        inline int GetInt() const { return *reinterpret_cast<const int*>(this); }
        inline void SetInt(int i) { *reinterpret_cast<int*>(this) = i; }
        inline bool operator != (const LabelCfgTy& o) const
        { return GetInt() != o.GetInt(); }
    };
    
    // when to show a/c labels?
    struct LabelShowCfgTy {
        unsigned
        bExternal : 1,              // external/outside views
        bInternal : 1,              // internal/cockpit views
        bVR : 1;                    // VR views

        // this is a bit ugly but avoids a wrapper union with an int
        inline int GetInt() const { return *reinterpret_cast<const int*>(this); }
        inline void SetInt(int i) { *reinterpret_cast<int*>(this) = i; }
        inline bool operator != (const LabelCfgTy& o) const
        { return GetInt() != o.GetInt(); }
    };
    
    struct CSLPathCfgTy {           // represents a line in the [CSLPath] section of LiveTrafic.prg
        bool        bEnabled = false;
        std::string path;
        
        CSLPathCfgTy () {}
        CSLPathCfgTy (bool b, std::string&& p) : bEnabled(b), path(std::move(p)) {}
        inline bool empty() const   { return path.empty(); }
        inline bool enabled() const { return bEnabled && !empty(); }
        inline bool operator== (const CSLPathCfgTy& o) const { return path == o.path; }
        inline bool operator== (const std::string& s) const { return path == s; }
    };
    typedef std::vector<CSLPathCfgTy> vecCSLPaths;
    
public:
    pluginStateTy pluginState = STATE_STOPPED;
#ifdef DEBUG
    bool bSimVREntered = false;                 // for me to simulate some aspects of VR
#endif
    
//MARK: DataRefs
protected:
    XPLMDataRef adrXP[CNT_DATAREFS_XP];                 // array of XP data refs to read from
    XPLMDataRef adrLT[CNT_DATAREFS_LT];                 // array of data refs LiveTraffic provides
public:
    XPLMCommandRef cmdXP[CNT_CMDREFS_XP];               // array of command refs
    XPLMCommandRef cmdLT[CNT_CMDREFS_LT];

//MARK: Provided Data, i.e. global variables
protected:
    XPLMPluginID pluginID       = 0;
    logLevelTy iLogLevel        = logWARN;
    logLevelTy iMsgAreaLevel    = logINFO;
    int bShowingAircraft        = false;
    unsigned uDebugAcFilter     = 0;    // icao24 for a/c filter
    int bDebugAcPos             = false;// output debug info on position calc into log file?
    int bDebugLogRawFd          = false;// log raw flight data to LTRawFD.log
    int bDebugModelMatching     = false;// output debug info on model matching in xplanemp?
    std::string XPSystemPath;
    std::string LTPluginPath;           // path to plugin directory
    std::string DirSeparator;
    int bUseHistoricData        = false;
    int bChannel[CNT_DR_CHANNELS];      // is channel enabled?
    double chTsOffset           = 0.0f; // offset of network time compared to system clock
    int chTsOffsetCnt           = 0;    // how many offset reports contributed to the calculated average offset?
    int iTodaysDayOfYear        = 0;
    time_t tStartThisYear = 0, tStartPrevYear = 0;
    int lastCheckNewVer         = 0;    // when did we last check for updates? (hours since the epoch)
    
    // generic config values
    int bAutoStart              = true; // shall display a/c right after startup?
    int bAIonRequest            = false;// acquire multiplayer control for TCAS on request only, not automatically?
    // which elements make up an a/c label?
    LabelCfgTy labelCfg = { 0,1,0,0,0,0,0,0, 0,0,0,0,0,0 };
    LabelShowCfgTy labelShown = { 1, 1, 1 };        // when to show? (default: always)
    bool bLabelColDynamic  = false;     // dynamic label color?
    int labelColor      = COLOR_YELLOW; // label color, by default yellow
    int maxNumAc        = 50;           // how many aircraft to create at most?
    int maxFullNumAc    = 50;           // how many of these to draw in full (as opposed to 'lights only')?
    int fullDistance    = 3;            // nm: Farther away a/c is drawn 'lights only'
    int fdStdDistance   = 15;           // nm: miles to look for a/c around myself
    int fdRefreshIntvl  = 20;           // how often to fetch new flight data
    int fdBufPeriod     = 90;           // seconds to buffer before simulating aircraft
    int acOutdatedIntvl = 50;           // a/c considered outdated if latest flight data more older than this compare to 'now'
    int netwTimeout     = 90;           // [s] of network request timeout
    int bLndLightsTaxi = false;         // keep landing lights on while taxiing? (to be able to see the a/c as there is no taxi light functionality)
    int hideBelowAGL    = 0;            // if positive: a/c visible only above this height AGL
    int hideTaxiing     = 0;            // hide a/c while taxiing?
    int drLibXplaneMP   = 1;            // CSL models: register original 'libxplanemp' dataRefs?

    // channel config options
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
    std::string sADSBExAPIKey;
    
    // live values
    bool bReInitAll     = false;        // shall all a/c be re-initiaized (e.g. time jumped)?
    
    int cntAc           = 0;            // number of a/c being displayed
    std::string keyAc;                  // key (transpIcao) for a/c whose data is returned
    const LTAircraft* pAc = nullptr;    // ptr to that a/c
    
//MARK: Debug helpers (public)
public:
    std::string cslFixAcIcaoType;       // set of fixed values to use for...
    std::string cslFixOpIcao;           // ...newly created aircraft for...
    std::string cslFixLivery;           // ...CSL model package testing
    RealTrafficConnection *pRTConn = nullptr;   // ptr to RealTraffic connection object
    long ADSBExRLimit = 0;              // ADSBEx: Limit on RapidAPI
    long ADSBExRRemain = 0;             // ADSBEx: Remaining Requests on RapidAPI

//MARK: Constructor
public:
    DataRefs ( logLevelTy initLogLevel );               // Constructor doesn't do much
    bool Init();                                        // Init DataRefs, return "OK?"
    void Stop();                                        // unregister what's needed
    
protected:
    // call XPLMRegisterDataAccessor
    bool RegisterDataAccessors();
    bool RegisterCommands();
    void* getVarAddr (dataRefsLT dr);

//MARK: DataRef access
public:
    inline float GetTotalRunningTimeSec() const { return XPLMGetDataf(adrXP[DR_TOTAL_RUNNING_TIME_SEC]); }
    inline float GetLocalTimeSec() const        { return XPLMGetDataf(adrXP[DR_LOCAL_TIME_SEC]); }
    inline int   GetLocalDateDays() const       { return XPLMGetDatai(adrXP[DR_LOCAL_DATE_DAYS]); }
    inline bool  GetUseSystemTime() const       { return XPLMGetDatai(adrXP[DR_USE_SYSTEM_TIME]) != 0; }
    inline float GetZuluTimeSec() const         { return XPLMGetDataf(adrXP[DR_ZULU_TIME_SEC]); }
    inline bool  IsViewExternal() const         { return XPLMGetDatai(adrXP[DR_VIEW_EXTERNAL]) != 0; }
    inline XPViewTypes GetViewType () const     { return (XPViewTypes)XPLMGetDatai(adrXP[DR_VIEW_TYPE]); }
    inline bool  IsVREnabled() const            { return
#ifdef DEBUG
        bSimVREntered ? true :                  // simulate some aspects of VR
#endif
        adrXP[DR_VR_ENABLED] ? XPLMGetDatai(adrXP[DR_VR_ENABLED]) != 0 : false; }  // for XP10 compatibility we accept not having this dataRef
    // weather / air pressure
    inline float GetWeatherBaroSea_inch () const { return XPLMGetDataf(adrXP[DR_WEATHER_BARO_SEA]) * 100.0f; }
    inline double GetWeatherBaroSea_hpa () const { return GetWeatherBaroSea_inch() * HPA_per_INCH; }
    inline bool IsRealWeatherInUse () const     { return XPLMGetDatai(adrXP[DR_WEATHER_USE_REAL]) != 0; }
    // how many feet to add to barometric altitude to get geometric altitude?
    inline double GetAltCorrection_ft () const  { return (GetWeatherBaroSea_hpa() - HPA_STANDARD) * FT_per_HPA; }

    inline void SetLocalDateDays(int days)      { XPLMSetDatai(adrXP[DR_LOCAL_DATE_DAYS], days); }
    inline void SetUseSystemTime(bool bSys)     { XPLMSetDatai(adrXP[DR_USE_SYSTEM_TIME], (int)bSys); }
    inline void SetZuluTimeSec(float sec)       { XPLMSetDataf(adrXP[DR_ZULU_TIME_SEC], sec); }
    inline void SetViewType(XPViewTypes vt)     { XPLMSetDatai(adrXP[DR_VIEW_TYPE], (int)vt); }
    positionTy GetUsersPlanePos(double& trueAirspeed_m, double& track) const;
    void GetPilotsHeadPos(XPLMCameraPosition_t& headPos) const;

//MARK: DataRef provision by LiveTraffic
    // Generic Get/Set callbacks
    static int   LTGetInt(void* p);
    static float LTGetFloat(void* p);
    static void  LTSetBool(void* p, int i);
    
    // Bulk data access to transfer a lot of a/c info to LTAPI
    static int LTGetBulkAc (void* inRefcon, void * outValue,
                            int inStartIdx, int inNumAc);

protected:
    // a/c info
    bool FetchPAc ();

public:
    static void LTSetAcKey(void*p, int i);
    static int LTGetAcInfoI(void* p);
    static float LTGetAcInfoF(void* p);
    
    // seconds since epoch including fractionals
    double GetSimTime() const;
    std::string GetSimTimeString() const;
    
    // livetraffic/sim/date and .../time
    static void LTSetSimDateTime(void* p, int i);
    static int LTGetSimDateTime(void* p);

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
    
    // livetraffic/cfg/use_historic_data: Simulate history
    static void LTSetUseHistData(void*, int i);
    bool SetUseHistData (bool bUseHistData, bool bForceReload);
    inline bool GetUseHistData() const           { return bUseHistoricData; }
    
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
    static int HaveAIUnderControl(void* =NULL) { return XPMPHasControlOfAIAircraft(); }
    inline LabelCfgTy GetLabelCfg() const { return labelCfg; }
    inline LabelShowCfgTy GetLabelShowCfg() const { return labelShown; }
    inline bool IsLabelColorDynamic() const { return bLabelColDynamic; }
    inline int GetLabelColor() const { return labelColor; }
    void GetLabelColor (float outColor[4]) const;
    inline int GetMaxNumAc() const { return maxNumAc; }
    inline int GetMaxFullNumAc() const { return maxFullNumAc; }
    inline int GetFullDistance_nm() const { return fullDistance; }
    inline int GetFdStdDistance_nm() const { return fdStdDistance; }
    inline int GetFdStdDistance_m() const { return fdStdDistance * M_per_NM; }
    inline int GetFdStdDistance_km() const { return fdStdDistance * M_per_NM / M_per_KM; }
    inline int GetFdRefreshIntvl() const { return fdRefreshIntvl; }
    inline int GetFdBufPeriod() const { return fdBufPeriod; }
    inline int GetAcOutdatedIntvl() const { return acOutdatedIntvl; }
    inline int GetNetwTimeout() const { return netwTimeout; }
    inline bool GetLndLightsTaxi() const { return bLndLightsTaxi != 0; }
    inline int GetHideBelowAGL() const { return hideBelowAGL; }
    inline bool GetHideTaxiing() const { return hideTaxiing != 0; }
    inline bool IsAutoHidingActive() const { return hideBelowAGL > 0 || hideTaxiing != 0; }

    inline bool GetDrLibXplaneMP() const { return drLibXplaneMP != 0; }
    inline void SetDrLibXplaneMP(int i) { drLibXplaneMP = i; }

    bool NeedNewVerCheck () const;
    void SetLastCheckedNewVerNow ();

    const vecCSLPaths& GetCSLPaths() const { return vCSLPaths; }
    vecCSLPaths& GetCSLPaths()             { return vCSLPaths; }
    void SaveCSLPath(int idx, const CSLPathCfgTy path);
    bool LoadCSLPackage(int idx);
    std::string GetDefaultAcIcaoType() const { return sDefaultAcIcaoType; }
    std::string GetDefaultCarIcaoType() const { return sDefaultCarIcaoType; }
    bool SetDefaultAcIcaoType(const std::string type);
    bool SetDefaultCarIcaoType(const std::string type);
    
    // livetraffic/channel/...
    inline void SetChannelEnabled (dataRefsLT ch, bool bEnable) { bChannel[ch - DR_CHANNEL_FIRST] = bEnable; }
    inline bool IsChannelEnabled (dataRefsLT ch) const { return bChannel[ch - DR_CHANNEL_FIRST]; }
    int CntChannelEnabled () const;
    
    std::string GetADSBExAPIKey () const { return sADSBExAPIKey; }
    void SetADSBExAPIKey (std::string apiKey) { sADSBExAPIKey = apiKey; }
    
    // timestamp offset network vs. system clock
    inline void ChTsOffsetReset() { chTsOffset = 0.0f; chTsOffsetCnt = 0; }
    inline double GetChTsOffset () const { return chTsOffset; }
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
    
    // livetraffic/dbg/model_matching: Debug Model Matching (by XPMP API)
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
    
//MARK: Processed values
public:
    static positionTy GetViewPos();            // view position in World coordinates
    static double GetViewHeading();
    static inline boundingBoxTy GetBoundingBox(double dist) // bounding box around current view pos
    { return boundingBoxTy(GetViewPos(), dist); }
    bool ShallDrawLabels() const;
    bool ToggleLabelDraw();                 // returns new value
};

extern DataRefs::dataRefDefinitionT DATA_REFS_LT[CNT_DATAREFS_LT];

#endif /* DataRefs_h */
