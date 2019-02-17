//
//  LTFlightData.h
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


#ifndef LTFlightData_h
#define LTFlightData_h

#include <mutex>
#include <deque>
#include "CoordCalc.h"

// from LTChannel.h
class LTChannel;

// transponder types (as defined by ADSB exchange)
enum transpTy {
    trt_Unknown=0,
    trt_Mode_S=1,
    trt_ADS_B_unknown=2,
    trt_ADS_B_0=3,
    trt_ADS_B_1=4,
    trt_ADS_B_2=5
};

//
//MARK: Flight Data
//      Represents an Aircraft's flight data, as read from the source(s)
//      Can be combined from multiple sources, key is transpIcao

class LTAircraft;
struct LTFlightDataList;

class LTFlightData
{
    // sub classes for sets of data
public:
    // data (potentially) changing dynamically during one flight
    class FDDynamicData
    {
    public:
        // communication
        XPMPPlaneRadar_t  radar;        // code=Sqk           5020
        
        // positional
        bool            gnd;            // on ground?         false
        double          heading;        // heading            231.2 [°]
        double          inHg;           // air pressure    29.88189 [mm Hg]
        
        // relative position
        double          brng;           // Bearing            304.0 [°]
        double          dst;            // Distance           19.54 [km]
        
        // movement
        double          spd;            // speed              190.0 [kt]
        double          vsi;            // vertical speed      2241 [ft/min]

        // timestamp is in seconds since Unix epoch (like time_t) but including fractional seconds
        double          ts;             // last update of dyn data?           1523789873,329 [Epoch s]
        
        // Channel which provided the data
        const LTChannel* pChannel = nullptr;
        
    public:
        FDDynamicData();
        // default move/copy constructor/operators
        FDDynamicData(const FDDynamicData&) = default;
        FDDynamicData(FDDynamicData&&) = default;
        FDDynamicData& operator=(const FDDynamicData&) = default;
        FDDynamicData& operator=(FDDynamicData&&) = default;
        // purely timestamp-based comparison
        inline bool similarTo (const FDDynamicData& d) const { return abs(ts-d.ts) < SIMILAR_TS_INTVL; }
        inline int cmp (const FDDynamicData& d)        const { return ts < d.ts ? -1 : (ts > d.ts ? 1 : 0); }
        inline bool operator< (const FDDynamicData& d) const { return ts < d.ts; }
        // purely timestamp-based comparion with positionTy
        inline int cmp (const positionTy& p)            const { return ts < p.ts() ? -1 : (ts > p.ts() ? 1 : 0); }
        // formatted Squawk Code
        std::string GetSquawk() const;
    };
    
    typedef std::deque<FDDynamicData> dequeFDDynDataTy;
    
    // data, which stays static during one flight
    class FDStaticData
    {
    public:
        // aircraft details                Field                                        Example
        std::string     reg;            // Registration                                 D-ABQE
        std::string     country;        // registry country (based on transpIcao)       Germany
        std::string     acTypeIcao;     // XPMP API: "ICAOCode" as the aircraft type    DH8D
        std::string     man;            // aircraft manufacturer                        Bombardier
        std::string     mdl;            // aircraft model (long text)                   Bombardier DHC-8 402
        std::string     catDescr;       // category description
        int             engType = -1;   // type of engine
        int             engMount = -1;  // type of engine mount
        int             year = 0;       // year built                                   2008
        bool            mil  = false;   // military?                                    false
        transpTy        trt  = trt_Unknown; // transponder type                             ADS_B_unknown=2
        
        // more aircraft info
        const Doc8643*  pDoc8643 = NULL;

        // flight details
        std::string     call;           // Call sign          EWG8AY
        std::string     originAp;       // origin Airport
        std::string     destAp;         // destination Airport
        std::string     flight;         // flight code
        
        // operator
        std::string     op;             // operator                                     Air Berlin
        std::string     opIcao;         // XPMP API: "Airline"                          BER

    protected:
        bool            bInit    = false;   // has been initialized?

    public:
        FDStaticData() {}
        // default move/copy constructor/operators
        FDStaticData(const FDStaticData&) = default;
        FDStaticData(FDStaticData&&) = default;
        FDStaticData& operator=(const FDStaticData&) = default;
        FDStaticData& operator=(FDStaticData&&) = default;
        // 'merge' data, i.e. copy only filled fields from 'other'
        FDStaticData& operator |= (const FDStaticData& other);
        // returns flight, call sign, registration, or provieded _default (e.g. transp hex code)
        std::string acId (const std::string _default) const;
        // route (this is "originAp-destAp", but considers empty txt)
        std::string route() const;
        // flight + route (this is "flight: originAp-destAp", but considers empty txt)
        std::string flightRoute() const;
        // has been initialized at least once?
        bool isInit() const { return bInit; }
    };
    
    // KEY (protected, can be set only once, no mutex-control)
protected:
    std::string             transpIcao;     // 24bit transponder address                    3C4A25
    unsigned int            transpIcaoInt;  // dito, but as an integer                      3951141

    // last used Receiver ID, identifies the receiver of the signal of this flight data
    int             rcvr;
    int             sig;            // signal level
    
    std::string     labelStat;      // static part of the a/c label
    DataRefs::LabelCfgTy labelCfg = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0 };  // the configuration the label was saved for
    
protected:
    // DYNAMIC DATA (protected, access will be mutex-controlled for thread-safety)
    // buffered positions / dynamic data as deque, sorted by timestamp
    // first element is oldest and current (the 'from' position/data)
    // second is pos a/c is currently headed for, and the others then further on into the future
    dequePositionTy         posDeque, posToAdd;
    dequeFDDynDataTy        dynDataDeque;
    double                  rotateTS;
    double                  youngestTS;

    // STATIC DATA (protected, access will be mutex-controlled for thread-safety)
    FDStaticData            statData;
    
protected:
    // the simulated aircraft, which is based on this flight data
    // see Create/DestroyAircraft
    LTAircraft*             pAc;
    // Y probe reference
    XPLMProbeRef        probeRef;
    
    // object valid? (will be re-set in case of exceptions)
    bool                bValid;
public:
    // the lock we use to update / fetch data for thread safety
    mutable std::recursive_mutex   dataAccessMutex;
    
protected:
    // find two positions around given timestamp ts (before <= ts < after)
    // pBefore and pAfter can come back NULL!
    // if pbSimilar is not NULL then function also checks for 'similar' pos
    // if a 'similar' pos (ts within SIMILAR_TS_INTVL) is found then
    // *pbSimilar is set to true and pBefore points to that one.
    // Caller must own lock to ensure pointers stay valid!
    void dequeFDDynFindAdjacentTS (double ts,
                                   FDDynamicData*& pBefore,
                                   FDDynamicData*& pAfter,
                                   bool* pbSimilar = nullptr);

    // calculate heading of given element of posDeque
    void CalcHeading (dequePositionTy::iterator it);
    
public:
    LTFlightData();
    LTFlightData(const LTFlightData&);
    LTFlightData(LTFlightData&&) = default;
    ~LTFlightData();
    
    LTFlightData& operator=(const LTFlightData&);
    LTFlightData& operator=(LTFlightData&&) = default;
    
    bool IsValid() const { return bValid; }
    void SetInvalid();
    
    // KEY into the map
    void SetKey ( std::string key );
    inline const std::string& key() const   { return transpIcao; }
    inline unsigned int keyInt() const      { return transpIcaoInt; }
    std::string keyDbg() const              { return key() + ' ' + statData.acId("-"); }
    
    // Search support: icao, registration, call sign, flight number matches?
    bool IsMatch (const std::string t) const;
    
    // struct not yet properly filled?
    inline bool empty() const       { return key().empty(); }
    // is data useful, can an aircraft be created based on it? (yes, if we know from where to where to fly)
    bool validForAcCreate( double simTime = NAN ) const;
    // a/c available for this FD?
    inline bool hasAc() const       { return pAc != nullptr; }
    // is the data outdated (considered too old to be useful)?
    bool outdated ( double simTime = NAN ) const;

    // produce a/c label
    void UpdateStaticLabel();
    std::string ComposeLabel() const;
    
    // based on buffered positions calculate the next position to fly to in a separate thread
    void DataCleansing (bool& bChanged);
    bool CalcNextPos ( double simTime );
    static void CalcNextPosMain ();
    void TriggerCalcNewPos ( double simTime );

    // new pos read from data stream to be stored
    void AddNewPos ( positionTy& pos ); // called from network thread, no terrain calc
    static void AppendAllNewPos();      // called from main thread, can calc terrain
    void AppendNewPos();                // called from AppendAllNewPos

    // check if thisPos would be OK after lastPos
    bool IsPosOK (const positionTy& lastPos,
                  const positionTy& thisPos,
                  double* pHeading = nullptr,
                  bool* pbChanged = nullptr);
    
    // youngest ts, i.e. timestamp of youngest used good position
    inline double GetYoungestTS() const { return youngestTS; }
    
    // possible return codes of 'trying' functions:
    enum tryResult {
        TRY_TECH_ERROR=-1,                      // unexpected technical error
        TRY_NO_LOCK=0,                          // didn't get the lock
        TRY_NO_DATA,                            // functionally OK, but no data to return
        TRY_SUCCESS                             // found something to return
    };
    
    // a/c reads available positions if lock available
    tryResult TryFetchNewPos ( dequePositionTy& posList, double& rotateTS );
    // const access to posDeque
    const dequePositionTy& GetPosDeque() const { return posDeque; }
    
    // determine Ground-status based on dynDataDeque, requires lock for access, so may fail if locked
    bool TryDeriveGrndStatus (positionTy& pos);
    // determine terrain alt at pos
    double YProbe_at_m (const positionTy& pos);
    // returns vector at timestamp (which has speed, direction and the like)
    tryResult TryGetVec (double ts, vectorTy& vec) const;
    
    // stringify all position information - mainly for debugging purposes
    std::string Positions2String () const;
    
    // access dynamic data (other than position)
    void AddDynData ( const FDDynamicData& inDyn, int rcvr, int sig, positionTy* pos = nullptr ); // new data read from stream to be stored
    // access to current dynData, i.e. dnDataDeque[0]
    bool TryGetSafeCopy ( FDDynamicData& outDyn ) const;    // tries to get a copy, fails if lock unavailable
    FDDynamicData WaitForSafeCopyDyn(bool bFirst = true) const;  // waits for lock and returns a copy
    FDDynamicData GetUnsafeDyn() const;                     // no lock, potentially inconsistent!
    
    inline int GetRcvr() const { return rcvr; }
    
    // access static data
    void UpdateData ( const FDStaticData& inStat );
    bool TryGetSafeCopy ( FDStaticData& outStat ) const;
    FDStaticData WaitForSafeCopyStat() const;
    inline const FDStaticData& GetUnsafeStat() const { return statData; }    // no lock, potentially inconsistent!


    //
    // Functions which should be called from within the X-Plane drawing thread
    // as XPLM and XPMP API functions are called
    //
    
    // access/create/destroy aircraft
    bool AircraftMaintenance ( double simTime );    // returns: delete me?
    bool CreateAircraft ( double simTime );
    void DestroyAircraft ();
    LTAircraft* GetAircraft () const { return pAc; }
    
    // actions on all flight data / treating mapFd as lists
    static void UpdateAllModels ();
    static const LTFlightData* FindFocusAc (const double bearing);
    friend LTFlightDataList;
};

// global map of flight data, keyed by transpIcao
typedef std::map<std::string,LTFlightData>  mapLTFlightDataTy;

// the global map of all received flight data,
// which also includes pointer to the simulated aircrafts
extern mapLTFlightDataTy mapFd;
// modifying the map is controlled by a mutex
// (note that mapFdMutex must be locked before dataAccessMutex
//  to avoid deadlocks, mapFdMutex is considered a higher-level lock)
extern std::mutex      mapFdMutex;

//
// MARK: Ordered lists of flight data
//       Note that included objects aren't valid for long!
//       Usage in a flight loop callback is fine as deletion
//       happens in a flight loop callback thread, too.
//       Usage in other threads without mapFdMutex is not fine.
//

typedef std::vector<LTFlightData*> vecLTFlightDataRefTy;

struct LTFlightDataList
{
    enum OrderByTy {
        ORDR_UNKNOWN = 0,
        // static fields
        ORDR_REG, ORDR_AC_TYPE_ICAO, ORDR_CALL,
        ORDR_ORIGIN_DEST, ORDR_FLIGHT, ORDR_OP_ICAO,
        // dynamic fields
        ORDR_DST, ORDR_SPD, ORDR_VSI, ORDR_ALT, ORDR_PHASE
    } orderedBy = ORDR_UNKNOWN;
    
    vecLTFlightDataRefTy lst;
    
    LTFlightDataList ( OrderByTy ordrBy = ORDR_DST );
    void ReorderBy ( OrderByTy ordrBy );
};

#endif /* LTFlightData_h */
