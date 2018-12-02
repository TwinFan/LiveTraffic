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

#include <variant>

#include "XPLMDataAccess.h"
#include "TextIO.h"
#include "CoordCalc.h"

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
    
    // static functions for reading the doc8643.txt file
    // and returning information from it
public:
    static bool ReadDoc8643File ();
    static const Doc8643& get (const std::string _type);
};

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
    DR_VIEW_X,
    DR_VIEW_Y,
    DR_VIEW_Z,
    DR_LOCAL_TIME_SEC,
    DR_LOCAL_DATE_DAYS,
    DR_USE_SYSTEM_TIME,
    DR_ZULU_TIME_SEC,
    CNT_DATAREFS_XP                     // always last, number of elements
};

// Datarefs offered by LiveTraffic
enum dataRefsLT {
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
    
    DR_SIM_DATE,
    DR_SIM_TIME,
    DR_CFG_AIRCRAFTS_DISPLAYED,
    DR_CFG_AUTO_START,
    DR_CFG_LABELS,
    DR_CFG_LOG_LEVEL,
    DR_CFG_USE_HISTORIC_DATA,
    DR_CFG_MAX_NUM_AC,
    DR_CFG_MAX_FULL_NUM_AC,
    DR_CFG_FULL_DISTANCE,
    DR_CFG_FD_STD_DISTANCE,
    DR_CFG_FD_REFRESH_INTVL,
    DR_CFG_FD_BUF_PERIOD,
    DR_CFG_AC_OUTDATED_INTVL,
    DR_CHANNEL_ADSB_EXCHANGE_ONLINE,
    DR_CHANNEL_ADSB_EXCHANGE_HISTORIC,
    DR_CHANNEL_OPEN_SKY_ONLINE,
    DR_CHANNEL_OPEN_SKY_AC_MASTERDATA,
    DR_CHANNEL_FUTUREDATACHN_ONLINE,
    DR_DBG_AC_FILTER,
    DR_DBG_AC_POS,
    DR_DBG_LOG_RAW_FD,
    DR_DBG_MODEL_MATCHING,
    CNT_DATAREFS_LT                     // always last, number of elements
};

const int CNT_DR_CHANNELS = 5;          // number of flight data channels
const int DR_CHANNEL_FIRST = DR_CHANNEL_ADSB_EXCHANGE_ONLINE;

class DataRefs
{
public:
    //MARK: dataRefDefinitionT
    struct dataRefDefinitionT {
        std::string dataName;
        XPLMDataTypeID dataType;
        std::variant<XPLMGetDatai_f, XPLMGetDataf_f> fRead;
        std::variant<XPLMSetDatai_f, XPLMSetDataf_f> fWrite;
        void* refCon;
        bool bCfgFile;
        
        // allows using the object in string context -> dataName
        operator const char* () const { return dataName.c_str(); }
        
        bool isWriteable () const { return (dataType == xplmType_Int) ? (std::get<XPLMSetDatai_f>(fWrite) != NULL) : 
                                                                        (std::get<XPLMSetDataf_f>(fWrite) != NULL); }
        XPLMGetDatai_f getDatai_f () const { return dataType == xplmType_Int ? std::get<XPLMGetDatai_f>(fRead) : NULL; }
        XPLMSetDatai_f setDatai_f () const { return dataType == xplmType_Int ? std::get<XPLMSetDatai_f>(fWrite) : NULL; }
        XPLMGetDataf_f getDataf_f () const { return dataType == xplmType_Float ? std::get<XPLMGetDataf_f>(fRead) : NULL; }
        XPLMSetDataf_f setDataf_f () const { return dataType == xplmType_Float ? std::get<XPLMSetDataf_f>(fWrite) : NULL; }
        
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
        bTranspCode : 1,
        bReg : 1,
        bIcaoOp : 1,
        bCallSign : 1,              // default
        bFlightNo : 1,
        bRoute : 1,
        // dynamic info
        bPhase : 1,
        bHeading : 1,
        bAlt : 1,                   // default
        bHeightAGL : 1,
        bSpeed : 1,                 // default
        bVSI : 1;
    };
    
    union LabelCfgUTy {
        LabelCfgTy b;
        int i;
    };

    
public:
    pluginStateTy pluginState = STATE_STOPPED;
    
//MARK: DataRefs
protected:
    XPLMDataRef adrXP[CNT_DATAREFS_XP];                 // array of XP data refs to read from
    XPLMDataRef adrLT[CNT_DATAREFS_LT];                 // array of data refs LiveTraffic provides
    
//MARK: Provided Data, i.e. global variables
protected:
    XPLMPluginID pluginID       = 0;
    logLevelTy iLogLevel        = logWARN;
    int bShowingAircrafts       = false;
    unsigned uDebugAcFilter     = 0;    // icao24 for a/c filter
    int bDebugAcPos             = false;// output debug info on position calc into log file?
    int bDebugLogRawFd          = false;// log raw flight data to LTRawFD.log
    int bDebugModelMatching     = false;// output debug info on model matching in xplanemp?
    std::string XPSystemPath;
    std::string LTPluginPath;           // path to plugin directory
    std::string DirSeparator;
    int bUseHistoricData        = false;
    int bChannel[CNT_DR_CHANNELS];     // is channel enabled?
    int iTodaysDayOfYear        = 0;
    time_t tStartThisYear = 0, tStartPrevYear = 0;
    
    // generic config values
    int bAutoStart              = false;// shall display a/c right after startup?
    // which elements make up an a/c label?
    LabelCfgUTy labelCfg = { {1,0,0,0,1,0,0,0,0,1,0,1,0} };
    int maxNumAc        = 50;           // how many aircrafts to create at most?
    int maxFullNumAc    = 50;           // how many of these to draw in full (as opposed to 'lights only')?
    int fullDistance    = 5;            // kilometer: Farther away a/c is drawn 'lights only'
    int fdStdDistance   = 25;           // kilometer to look for a/c around myself
    int fdRefreshIntvl  = 20;           // how often to fetch new flight data
    int fdBufPeriod     = 90;           // seconds to buffer before simulating aircrafts
    int acOutdatedIntvl = 50;           // a/c considered outdated if latest flight data more older than this compare to 'now'
    
    // live values
    bool bReInitAll     = false;        // shall all a/c be re-initiaized (e.g. time jumped)?
    
    int cntAc           = 0;            // number of a/c being displayed
    std::string keyAc;                  // key (transpIcao) for a/c whose data is returned
    const LTAircraft* pAc = nullptr;    // ptr to that a/c

//MARK: Constructor
public:
    DataRefs ( logLevelTy initLogLevel );               // Constructor doesn't do much
    bool Init();                                        // Init DataRefs, return "OK?"
    void Stop();                                        // unregister what's needed
    
protected:
    // call XPLMRegisterDataAccessor
    bool RegisterDataAccessors (dataRefDefinitionT aDefs[],
                                int cnt);
    void* getVarAddr (dataRefsLT dr);

//MARK: DataRef access
public:
    inline float GetTotalRunningTimeSec() const { return XPLMGetDataf(adrXP[DR_TOTAL_RUNNING_TIME_SEC]); }
    inline float GetViewX() const               { return XPLMGetDataf(adrXP[DR_VIEW_X]); }
    inline float GetViewY() const               { return XPLMGetDataf(adrXP[DR_VIEW_Y]); }
    inline float GetViewZ() const               { return XPLMGetDataf(adrXP[DR_VIEW_Z]); }
    inline float GetLocalTimeSec() const        { return XPLMGetDataf(adrXP[DR_LOCAL_TIME_SEC]); }
    inline int   GetLocalDateDays() const       { return XPLMGetDatai(adrXP[DR_LOCAL_DATE_DAYS]); }
    inline bool  GetUseSystemTime() const       { return XPLMGetDatai(adrXP[DR_USE_SYSTEM_TIME]) != 0; }
    inline float GetZuluTimeSec() const         { return XPLMGetDataf(adrXP[DR_ZULU_TIME_SEC]); }
    
    inline void SetLocalDateDays(int days)      { XPLMSetDatai(adrXP[DR_LOCAL_DATE_DAYS], days); }
    inline void SetUseSystemTime(bool bSys)     { XPLMSetDatai(adrXP[DR_USE_SYSTEM_TIME], (int)bSys); }
    inline void SetZuluTimeSec(float sec)       { XPLMSetDataf(adrXP[DR_ZULU_TIME_SEC], sec); }

//MARK: DataRef provision by LiveTraffic
    // Generic Get/Set callbacks
    static int   LTGetInt(void* p);
    static float LTGetFloat(void* p);
    static void  LTSetBool(void* p, int i);

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

    // livetraffic/cfg/aircrafts_displayed: Aircrafts Displayed
    static void LTSetAircraftsDisplayed(void* p, int i);
    inline int GetAircraftsDisplayed() const  { return bShowingAircrafts; }
    void SetAircraftsDisplayed ( int bEnable );
    int ToggleAircraftsDisplayed ();        // returns new status (displayed?)
    
    // livetraffic/cfg/log_level: Log Level
    static void LTSetLogLevel(void* p, int i);
    void SetLogLevel ( int i );
    inline logLevelTy GetLogLevel()             { return iLogLevel; }
    
    // livetraffic/cfg/use_historic_data: Simulate history
    static void LTSetUseHistData(void*, int i);
    bool SetUseHistData (bool bUseHistData, bool bForceReload);
    inline bool GetUseHistData() const           { return bUseHistoricData; }
    
    // general config values
    static void LTSetCfgValue(void* p, int val);
    bool SetCfgValue(void* p, int val);
    bool GetAutoStart() const { return bAutoStart != 0; }
    LabelCfgUTy GetLabelCfg() const { return labelCfg; }
    int GetMaxNumAc() const { return maxNumAc; }
    int GetMaxFullNumAc() const { return maxFullNumAc; }
    int GetFullDistance_km() const { return fullDistance; }
    double GetFullDistance_nm() const { return fullDistance * double(M_per_KM) / double(M_per_NM); }
    int GetFdStdDistance() const { return fdStdDistance; }
    int GetFdStdDistance_m() const { return fdStdDistance * M_per_KM; }
    int GetFdRefreshIntvl() const { return fdRefreshIntvl; }
    int GetFdBufPeriod() const { return fdBufPeriod; }
    int GetAcOutdatedIntvl() const { return acOutdatedIntvl; }

    // livetraffic/channel/...
    inline void SetChannelEnabled (dataRefsLT ch, bool bEnable) { bChannel[ch - DR_CHANNEL_FIRST] = bEnable; }
    inline bool IsChannelEnabled (dataRefsLT ch) const { return bChannel[ch - DR_CHANNEL_FIRST]; }

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
    
    // Number of aircrafts
    inline int GetNumAircrafts() const          { return cntAc; }
    inline int IncNumAircrafts()                { return ++cntAc; }
    // decreses number of aircrafts
    // which by itself is simplistic, but as the just removed a/c
    // _could_ be the one we are monitoring in our dataRefs (we don't know)
    // we better invalidate the pAc ptr and force the dataRef to find the a/c again
    inline int DecNumAircrafts()                { pAc=nullptr; return --cntAc; }

    // Get XP System Path
    inline std::string GetXPSystemPath() const  { return XPSystemPath; }
    inline std::string GetLTPluginPath() const  { return LTPluginPath; }
    inline std::string GetDirSeparator() const  { return DirSeparator; }
    // this one returns the directory separator as used by the XPMP API
    std::string GetDirSeparatorMP();
    
    // Load/save config file (basically a subset of LT dataRefs)
    bool LoadConfigFile();
    bool SaveConfigFile();
    
    // Re-Init
    inline bool IsReInitAll() const { return bReInitAll; }
    inline void SetReInitAll (bool b) { bReInitAll = b; }
    
//MARK: Processed values
public:
    positionTy GetViewPos() const;            // view position in World coordinates
    inline boundingBoxTy GetBoundingBox(double dist) const // bounding box around current view pos
    { return boundingBoxTy(GetViewPos(), dist); }
};

extern DataRefs::dataRefDefinitionT DATA_REFS_LT[CNT_DATAREFS_LT];

#endif /* DataRefs_h */
