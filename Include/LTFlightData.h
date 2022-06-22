/// @file       LTFlightData.h
/// @brief      LTFlightData represents the tracking data of one aircraft, even before it is drawn
/// @details    Keeps statis and dynamic tracking data.\n
///             Dynamic tracking data is kept as a list.\n
///             Various optimizations and cleansing applied to dynamic data in a separate thread.\n
///             Provides fresh tracking data to LTAircraft upon request.
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

#ifndef LTFlightData_h
#define LTFlightData_h

// from LTChannel.h
class LTChannel;

//
//MARK: Flight Data
//      Represents an Aircraft's flight data, as read from the source(s)
//      Can be combined from multiple sources, key is transpIcao

class LTAircraft;
class Apt;

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
        inline bool similarTo (const FDDynamicData& d) const { return std::abs(ts-d.ts) < SIMILAR_TS_INTVL; }
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
        
        // more aircraft info
        const Doc8643*  pDoc8643 = NULL;

        // flight details
        std::string     call;           // Call sign          EWG8AY
        std::string     originAp;       // origin Airport
        std::string     destAp;         // destination Airport
        std::string     flight;         // flight code
        std::string     slug;           ///< URL to flight details
        
        // operator
        std::string     op;             // operator                                     Air Berlin
        std::string     opIcao;         // XPMP API: "Airline"                          BER

    protected:
        /// Has this static data object already been filled from a proper master data channel?
        bool            bFilledFromMasterCh = false;

    public:
        FDStaticData() {}
        // default move/copy constructor/operators
        FDStaticData(const FDStaticData&) = default;
        FDStaticData(FDStaticData&&) = default;
        FDStaticData& operator=(const FDStaticData&) = default;
        FDStaticData& operator=(FDStaticData&&) = default;
        /// @brief  Merges data, i.e. copy only filled fields from 'other'
        /// @return Have matching-relevant fields now changed?
        bool merge (const FDStaticData& other, bool bIsMasterChData);
        // returns flight, call sign, registration, or provieded _default (e.g. transp hex code)
        std::string acId (const std::string _default) const;
        // route (this is "originAp-destAp", but considers empty txt)
        std::string route() const;
        // flight + route (this is "flight: originAp-destAp", but considers empty txt)
        std::string flightRoute() const;
        // best guess for an airline livery: opIcao if exists, otherwise first 3 digits of call sign
        inline std::string airlineCode() const
            { return opIcao.empty() ? call.substr(0,3) : opIcao; }
        /// is this a ground vehicle?
        bool isGrndVehicle() const;
        /// has been initialized from a proper master data channel?
        bool hasMasterChData() const { return bFilledFromMasterCh; }
    };
    
    // KEY (protected, can be set only once, no mutex-control)
public:
    // in ascending order of priority
    enum FDKeyType { KEY_UNKNOWN=0, KEY_OGN, KEY_RT, KEY_FLARM, KEY_ICAO, KEY_FSC, KEY_ADSBEX };
    struct FDKeyTy {
        FDKeyType               eKeyType = KEY_UNKNOWN;
        std::string             key;            // the primary key in use
        unsigned long           num = 0;        // primary key's numeric representation
        
        // setting keys
        std::string SetKey (FDKeyType _eType, unsigned long _num);
        std::string SetKey (FDKeyType _eType, const std::string _key, int base=16);
        
        // construction
        FDKeyTy() {}
        FDKeyTy(FDKeyType _eType, unsigned long _num)                   { SetKey(_eType,_num); }
        FDKeyTy(FDKeyType _eType, const std::string _key, int base=16)  { SetKey(_eType, _key, base); }
        
        // copy/move operations
        FDKeyTy(const FDKeyTy& o) = default;
        FDKeyTy(FDKeyTy&& o) = default;
        FDKeyTy& operator=(const FDKeyTy& o) = default;
        FDKeyTy& operator=(FDKeyTy&& o) = default;

        // strict order based on numeric value
        inline bool operator==(const FDKeyTy& o) const { return eKeyType == o.eKeyType && num == o.num; }
        inline bool operator!=(const FDKeyTy& o) const { return eKeyType != o.eKeyType || num != o.num; }
        inline bool operator<(const FDKeyTy& o) const { return eKeyType == o.eKeyType ? num < o.num : eKeyType < o.eKeyType; }

        // imitate some (std::)string functionality
        inline bool operator==(const std::string o) const { return key == o; }
        inline bool operator!=(const std::string o) const { return key != o; }
        inline operator std::string() const { return key; }
        
        inline const char* c_str() const    { return key.c_str(); }
        inline bool empty() const           { return key.empty(); }
        void clear()                        { *this = FDKeyTy(); }
        
        // matches any string?
        bool isMatch (const std::string t) const;
        
        /// return the type of key (as string)
        const char* GetKeyTypeText () const;
    };
protected:
    FDKeyTy acKey;

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
    positionTy              posRwy;     ///< determined rwy (likely) to land on (position)
    std::string             rwyId;      ///< determined rwy (likely) to land non (human-readable text)

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

#ifdef DEBUG
public:
    bool                bIsSelected = false;    // is selected aircraft for debugging/logging?
#endif
public:
    // the lock we use to update / fetch data for thread safety
    mutable std::recursive_mutex   dataAccessMutex;
    /// Export file for tracking data
    static std::ofstream fileExport;
    static double fileExportTsBase;         ///< when normalizing timestamps this is the base

    /// Cache for flight model in use, actually of type LTAircraft::FlightModel, but we can't forward-declare it here
    const void* pMdl = nullptr;
    
protected:
    static std::string fileExportName;      ///< current export file's name

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
    ~LTFlightData();
    
    LTFlightData& operator=(const LTFlightData&);
    
    bool IsValid() const { return bValid; }
    void SetInvalid(bool bAlsoAc = true);
    
    // KEY into the map
    void SetKey    (const FDKeyTy& _key);
    void SetKey    (FDKeyType eType, unsigned long _num)                    { acKey.SetKey(eType, _num); }
    void SetKey    (FDKeyType eType, const std::string _key, int base=16)   { acKey.SetKey(eType, _key, base); }
    const FDKeyTy& key() const                              { return acKey; }
    std::string keyDbg() const                              { return key().key + ' ' + statData.acId("-"); }
    /// Checks for a duplicate key on another key type and updates _key if so
    static bool CheckDupKey(FDKeyTy& _key, FDKeyType _ty);

    // Search support: icao, registration, call sign, flight number matches?
    bool IsMatch (const std::string t) const;
    
    // struct not yet properly filled?
    inline bool empty() const       { return key().empty(); }
    // is data useful, can an aircraft be created based on it? (yes, if we know from where to where to fly)
    bool validForAcCreate( double simTime = NAN ) const;
    // a/c available for this FD?
    inline bool hasAc() const       { return pAc != nullptr; }

    // produce a/c label
    void UpdateStaticLabel();
    std::string ComposeLabel() const;
    
    // based on buffered positions calculate the next position to fly to in a separate thread
    void DataCleansing (bool& bChanged);
    void DataSmoothing (bool& bChanged);
    void SnapToTaxiways (bool& bChanged);   ///< shift ground positions to taxiways, insert positions at taxiway nodes
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
    /// returns next position in posDeque with timestamp after ts
    /// @param ts Need a position with a timestamp larger than this
    /// @param[out] pos Receives the position if found
    /// @return Indicates if the call was successful
    tryResult TryGetNextPos (double ts, positionTy& pos) const;

    /// Has a determined rwy position?
    bool HasRwyPos() const { return posRwy.isNormal(); }
    /// Get determined rwy position
    const positionTy& GetRwyPos() const { return posRwy; }
    /// Get determined rwy id
    const std::string& GetRwyId() const { return rwyId; }
    
    // stringify all position information - mainly for debugging purposes
    std::string Positions2String () const;
    
    // access dynamic data (other than position)
    void AddDynData ( const FDDynamicData& inDyn, int rcvr, int sig, positionTy* pos = nullptr ); // new data read from stream to be stored
    // access to current dynData, i.e. dnDataDeque[0]
    bool TryGetSafeCopy ( FDDynamicData& outDyn ) const;    // tries to get a copy, fails if lock unavailable
    FDDynamicData WaitForSafeCopyDyn(bool bFirst = true) const;  // waits for lock and returns a copy
    FDDynamicData GetUnsafeDyn() const;                     // no lock, potentially inconsistent!
    bool GetCurrChannel (const LTChannel* &pChn) const;
    dataRefsLT GetCurrChannel () const;                     ///< Current channel's id
    
    inline int GetRcvr() const { return rcvr; }
    
    // access static data
    void UpdateData ( const FDStaticData& inStat, double distance, bool bIsMasterChData = false );
    bool TryGetSafeCopy ( FDStaticData& outStat ) const;
    FDStaticData WaitForSafeCopyStat() const;
    inline const FDStaticData& GetUnsafeStat() const { return statData; }    // no lock, potentially inconsistent!

    // Export of tracking data
protected:
    /// Temporary storage for data to be written to the export file
    struct ExportDataTy {
        unsigned long ts = 0;
        std::string s;
        ExportDataTy (unsigned long _ts, const char* _s) : ts(_ts), s(_s) {}
        bool operator> (const ExportDataTy& o) const { return ts > o.ts; }
    };
    /// Export data needs to be sorted by timestampe, written out only when that timestamp passed
    typedef std::priority_queue<ExportDataTy, std::deque<ExportDataTy>, std::greater<ExportDataTy> > quExportTy;
    static quExportTy quExport;             ///< the priority queue holding data to be exported for sorting
    /// Coordinates writing into the export file to avoid lines overwriting
    static std::recursive_mutex exportFdMutex;
    /// Export Flight Data to a file LTExportFD.csv
    void ExportFD (const FDDynamicData& inDyn,
                   const positionTy& pos);
public:
    /// Moves a line to the export priority queue, flushes data which is ready to be written
    static void ExportAddOutput (unsigned long ts, const char* s);
    /// Export Weather data record, based on DataRefs::GetWeather()
    static void ExportLastWeather ();
    /// @brief Open/Close the tracking data export file as needed
    /// @return if file is now open
    static bool ExportOpenClose ();

public:
    //
    // Functions which should be called from within the X-Plane drawing thread
    // as XPLM and XPMP API functions are called
    //
    
    // access/create/destroy aircraft
    bool AircraftMaintenance ( double simTime );    // returns: delete me?
    bool DetermineAcModel ();                       ///< try interpreting model text or check for ground vehicle, last resort: default a/c type
    bool AcSlotAvailable ();                        ///< checks if there is a slot available to create this a/c, tries to remove the farest a/c if too many a/c rendered
    bool CreateAircraft ( double simTime );
    void DestroyAircraft ();
    LTAircraft* GetAircraft () const { return pAc; }
    
    // actions on all flight data / treating mapFd as lists
    static void UpdateAllModels ();
    static const LTFlightData* FindFocusAc (const double bearing);
#ifdef DEBUG
    static void RemoveAllAcButSelected ();
#endif
    
    // LTApt inserts positions during the "snap-to-taxiway" precedure
    friend Apt;
};

// global map of flight data, keyed by transpIcao
typedef std::map<LTFlightData::FDKeyTy,LTFlightData>  mapLTFlightDataTy;

// the global map of all received flight data,
// which also includes pointer to the simulated aircraft
extern mapLTFlightDataTy mapFd;
// modifying the map is controlled by a mutex
// (note that mapFdMutex must be locked before dataAccessMutex
//  to avoid deadlocks, mapFdMutex is considered a higher-level lock)
extern std::mutex      mapFdMutex;

/// @brief Returns the next flight data, which has a defined aircraft (pAc)
/// @param iter Starting point
inline mapLTFlightDataTy::iterator mapFdNextWithAc (mapLTFlightDataTy::iterator iter)
{   return std::find_if(std::next(iter), mapFd.end(),
                        [](const mapLTFlightDataTy::value_type& fd){return fd.second.hasAc();} );
}

/// @brief Find "i-th" aircraft, i.e. the i-th flight data with assigned pAc
/// @param idx Index of aircraft to find, 1-based: pass in 1 to find the first
mapLTFlightDataTy::iterator mapFdAcByIdx (int idx);

/// Find a/c by text, compares with key, call sigh, registration etc., passes pure numbers to mapFdAcByIdx()
mapLTFlightDataTy::iterator mapFdSearchAc (const std::string& _s);

#endif /* LTFlightData_h */
