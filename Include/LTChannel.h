/// @file       LTChannel.h
/// @brief      Abstract base classes for any class reading tracking data from providers
/// @details    Network error handling .\n
///             Handles initializing and calling CURL library.\n
///             Global functions controlling regular requests to tracking data providers.
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

#ifndef LTChannel_h
#define LTChannel_h

#include <condition_variable>
#include <fstream>
#include <list>
#include "curl/curl.h"              // for CURL*
#include "parson.h"                 // for JSON parsing

// MARK: Thread control
extern std::thread CalcPosThread;              // the thread for pos calc (TriggerCalcNewPos)
extern std::mutex  FDThreadSynchMutex;         // supports wake-up and stop synchronization
extern std::condition_variable FDThreadSynchCV;
// stop all threads?
extern volatile bool bFDMainStop;

//
//MARK: Flight Data Connection (abstract base class)
//
class LTChannel
{
public:
    enum LTChannelType {
        CHT_UNKNOWN = 0,
        CHT_TRACKING_DATA,
        CHT_MASTER_DATA,
        CHT_TRAFFIC_SENDER,         // sends out data (not receiving)
    };
    
public:
    std::string urlLink;            ///< an URL related to that channel, eg. a radar view for testing coverage, or a home page
    std::string urlName;            ///< Name for the URL, could show on link buttons
    std::string urlPopup;           ///< more detailed text, shows eg. as popup when hovering over the link button
    const char* const pszChName;    ///< the cahnnel's name
    const dataRefsLT channel;       ///< id of channel (see dataRef)
    const LTChannelType eType;      ///< type of channel

protected:
    std::thread thr;                ///< Main Thread the channel runs in
    std::chrono::time_point<std::chrono::steady_clock> tNextWakeup; ///< when to wake up next for networking?
    typedef enum {
        THR_NONE = 0,               ///< no thread, not running
        THR_STARTING,               ///< Start of thread requested
        THR_RUNNING,                ///< Thread is running
        THR_STOP,                   ///< Thread shall stop
        THR_ENDED,                  ///< Thread has ended, but is not yet joined
    } ThrStatusTy;
    /// Thread's state
    volatile ThrStatusTy eThrStatus = THR_NONE;
    
private:
    bool bValid = true;             ///< valid connection?
    int errCnt = 0;                 ///< number of errors tolerated

public:
    /// Constructor just sets initial values
    LTChannel (dataRefsLT ch, LTChannelType t, const char* chName) :
        pszChName(chName), eType(t), channel(ch) {}
    virtual ~LTChannel ();         ///< Destructor makes sure the thread is stopped
    
    virtual void Start ();                  ///< Start the channel, typically starts a separate thread
    virtual void Stop (bool bWaitJoin);     ///< Stop the channel
    bool isRunning () const         ///< Is channel's thread running?
    { return thr.joinable(); }
    virtual bool shallRun () const; ///< all conditions met to continue the thread loop?
    /// Thread has ended but still needs to be joined
    bool hasEnded () const { return eThrStatus == THR_ENDED; }

private:
    void _Main();                   ///< Thread main function, will call virtual Main()
protected:
    void virtual Main () = 0;       ///< virtual thread main function
    
public:
    const char* ChName() const { return pszChName; }
    inline dataRefsLT GetChannel() const { return channel; }
    
    LTChannelType GetChType () const { return eType; };
    virtual bool IsValid () const {return bValid;}      // good to provide data after init?
    virtual void SetValid (bool _valid, bool bMsg = true);
    virtual bool IncErrCnt();               // increases error counter, returns if (still) valid
    virtual void DecErrCnt();               // decreases error counter
    int GetErrCnt () const { return errCnt; }
    virtual bool IsEnabled () const;
    virtual void SetEnable (bool bEnable);
    virtual std::string GetStatusText () const;  ///< return a human-readable staus
    virtual std::string GetStatusTextExt () const///< optionally return an extended status
    { return std::string(); }
    virtual int GetNumAcServed () const = 0;     ///< how many a/c do we feed?
    
    // shall data of this channel be subject to LTFlightData::DataSmoothing?
    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
    { gndRange = 0.0; airbRange = 0.0; return false; }
    // shall data of this channel be subject to hovering flight detection?
    virtual bool DoHoverDetection () const { return false; }

public:
    virtual bool FetchAllData (const positionTy& pos) = 0;
    virtual bool ProcessFetchedData () = 0;
    // TODO: Remove Disabled Processing, should be done during end of main thread / do something while disabled?
    virtual void DoDisabledProcessing () {}
    // TODO: Remove Close / (temporarily) close a connection, (re)open is with first call to FetchAll/ProcessFetchedData
    virtual void Close () {}
};

// Mutex and condition variable with which threads are woken up
extern std::mutex               FDThreadSynchMutex;
extern std::condition_variable  FDThreadSynchCV;
extern volatile bool            bFDMainStop;

// Collection of smart pointers requires C++ 17 to compile correctly!
#if __cplusplus < 201703L
#error Collection of smart pointers requires C++ 17 to compile correctly
#endif

// a smart pointer to the above flight data connection class
typedef std::unique_ptr<LTChannel> ptrLTChannelTy;

// a list of flight data connections smart pointers
typedef std::list<ptrLTChannelTy> listPtrLTChannelTy;
/// the actual list of channels
extern listPtrLTChannelTy    listFDC;

//
// MARK: LTOnlineChannel
//       Any request/reply via internet, uses CURL library
//
class LTOnlineChannel : public LTChannel
{
protected:
    CURL* pCurl;                    // handle into CURL
    std::string requBody;           ///< body of a POST request
    char* netData;                  // where the response goes
    size_t netDataPos;              // current write pos into netData
    size_t netDataSize;             // current size of netData
    char curl_errtxt[CURL_ERROR_SIZE];    // where error text goes
    long httpResponse;              // last HTTP response code
    
    static std::ofstream outRaw;    // output file for raw logging
    
public:
    LTOnlineChannel (dataRefsLT ch, LTChannelType t, const char* chName);
    virtual ~LTOnlineChannel ();
    
protected:
    virtual bool InitCurl ();
    virtual void CleanupCurl ();
    // CURL callback
    static size_t ReceiveData ( const char *ptr, size_t size, size_t nmemb, void *userdata );
    // logs raw data to a text file
    void DebugLogRaw (const char* data, bool bHeader = true);
    
public:
    bool FetchAllData (const positionTy& pos) override;
    virtual std::string GetURL (const positionTy& pos) = 0;
    virtual void ComputeBody (const positionTy& /*pos*/) { requBody.clear(); }   ///< in case of a POST request this call puts together its body
    
    /// Is the given network error text possibly caused by problems querying the revocation list?
    static bool IsRevocationError (const std::string& err);
};


//
//MARK: LTFlightDataChannel
//

/// Parent class for any flight data channel
class LTFlightDataChannel : public LTOnlineChannel {
protected:
    mutable float timeLastAcCnt = 0.0;      ///< when did we last count the a/c served by this channel?
    mutable int     numAcServed = 0;        ///< how many a/c do we feed when counted last?
public:
    LTFlightDataChannel (dataRefsLT ch, const char* chName) :
        LTOnlineChannel(ch, CHT_TRACKING_DATA, chName) {}
    int GetNumAcServed () const override;   ///< how many a/c do we feed when counted last?
};

//
//MARK: LTACMasterdata
//

/// list of a/c for which static data is yet missing
/// Note: Either a key is set (then we ask for a/c master data), or a call sign (then we ask for route information)
struct acStatUpdateTy {
public:
    LTFlightData::FDKeyTy acKey;    ///< a/c key to find a/c master data
    std::string callSign;           ///< call sign to query route information
    unsigned long dist = UINT_MAX;  ///< distance of plane to camera, influences priority

    DatRequTy type = DATREQU_NONE;   ///< type of this master data request
    
public:
    /// @brief Constructor for both master data or route lookup
    /// @param k Key to aircraft, is always required to be able to update the aircraft after having fetched data
    /// @param cs callSign if and only if a route is requested, empty if a/c master data is requested
    /// @param d Distance of aircraft to camera, influence priority in which requests are processed
    acStatUpdateTy(const LTFlightData::FDKeyTy& k, const std::string& cs, unsigned long d) :
    acKey(k), callSign(cs), dist(d), type(cs.empty() ? DATREQU_AC_MASTER : DATREQU_ROUTE) {}
    
    /// Default constructor creates an empty, invalid object
    acStatUpdateTy () : type(DATREQU_NONE) {}
    
    /// @brief Priority order is: route info has lower prio than master data, and within that: longer distance has lower order
    /// @see confusing definition of std::priority_queue's `Compare` template parameter at https://en.cppreference.com/w/cpp/container/priority_queue
    bool operator < (const acStatUpdateTy& o) const
    { return type == o.type ? dist < o.dist : type < o.type; }
    
    /// Equality is used to test of a likewise request is included already and does _not_ take distance into account
    bool operator == (const acStatUpdateTy& o) const
    { return type == o.type && acKey == o.acKey && callSign == callSign; }
    
    /// Valid request? (need an a/c key, and if it is a route request also a call sign)
    operator bool () const { return type != DATREQU_NONE && !acKey.empty() && (type != DATREQU_ROUTE || !callSign.empty()); }
};

/// Set of all master data requests, ordered by `acStatUpdateTy::operator<`
typedef std::set<acStatUpdateTy> setAcStatUpdateTy;
typedef std::set<LTFlightData::FDKeyTy> setFdKeyTy;
typedef std::set<std::string> setStringTy;

/// Parent class for master data channels
class LTACMasterdataChannel : public LTOnlineChannel
{
private:
    /// Lock controlling multi-threaded access to all the 3 sets
    static std::mutex mtxSets;
    /// global list of static data requests
    static setAcStatUpdateTy setAcStatUpdate;
    /// List of a/c to ignore, as we know we don't get data online
    static setFdKeyTy setIgnoreAc;
    /// List of call signs to ignore, as we know we don't get route info online
    static setStringTy setIgnoreCallSign;

protected:
    /// The request currently being processed
    acStatUpdateTy requ;

public:
	LTACMasterdataChannel (dataRefsLT ch, const char* chName) :
        LTOnlineChannel(ch, CHT_MASTER_DATA, chName) {}

    virtual bool UpdateStaticData (const LTFlightData::FDKeyTy& keyAc,
                                   const LTFlightData::FDStaticData& dat);
    int GetNumAcServed () const override { return 0; }  ///< how many a/c do we feed?

    /// Add request to fetch master data (returns `true` if added, `false` if duplicate)
    static bool RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                   double distance)
    { return RequestMasterData (keyAc, "", distance); }
    /// Add request to fetch route info (returns `true` if added, `false` if duplicate)
    static bool RequestRouteInfo  (const LTFlightData::FDKeyTy& keyAc,
                                   const std::string& callSign,
                                   double distance)
    { return callSign.empty() ? false : RequestMasterData (keyAc, callSign, distance); }
    
    /// Called regularly to keep the priority list updated
    static void MaintainMasterDataRequests ();
    
protected:
    /// Generically, uniquely add request to fetch data (returns `true` if added, `false` if duplicate)
    static bool RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                   const std::string& callSign,
                                   double distance);

    /// Fetch next master data request from our set (`acKey` will be empty if there is no waiting request)
    static acStatUpdateTy FetchNextRequest ();
    /// Add an aircraft to the ignore list
    static void AddIgnoreAc (const LTFlightData::FDKeyTy& keyAc);
    /// Add a callsign to the ignore list
    static void AddIgnoreCallSign (const std::string& cs);
};

//
//MARK: LTOutputChannel
//

/// Parent class for any channel that outputs data
class LTOutputChannel : public LTOnlineChannel {
public:
    LTOutputChannel (dataRefsLT ch, const char* chName) :
        LTOnlineChannel(ch, CHT_TRAFFIC_SENDER, chName) {}
    int GetNumAcServed () const override { return 0; }  ///< We don't "feed" aircraft
};

//
//MARK: Init Functions (called from plugin entry points)
//
bool LTFlightDataInit();
bool LTFlightDataEnable();
bool LTFlightDataShowAircraft();
void LTFlightDataHideAircraft();
void LTFlightDataDisable();
void LTFlightDataStop();

bool LTFlightDataAnyTrackingChEnabled ();   ///< Is at least one tracking data channel enabled?
bool LTFlightDataAnyChInvalid ();           ///< Is any channel invalid?
void LTFlightDataRestartInvalidChs ();      ///< Restart all invalid channels (set valid)

/// Return channel object
LTChannel* LTFlightDataGetCh (dataRefsLT ch);

//
//MARK: Aircraft Maintenance (called from flight loop callback)
//
void LTFlightDataAcMaintenance();

//
//MARK: Parson Helper Functions
//

// tests for 'null', return ptr to value if wanted
bool jog_is_null (const JSON_Object *object,
                  const char *name,
                  JSON_Value** ppValue = NULL);
bool jag_is_null (const JSON_Array *array,
                  size_t idx,
                  JSON_Value** ppValue = NULL);

// access to JSON string fields, with NULL replaced by ""
const char* jog_s (const JSON_Object *object, const char *name);

// access to JSON number fields, encapsulated as string, with NULL replaced by 0
double jog_sn (const JSON_Object *object, const char *name);

inline long jog_sl (const JSON_Object *object, const char *name)
{
    return std::lround(jog_sn (object, name));
}

// access to JSON number field (just a shorter name, returns 0 if not a number)
inline double jog_n (const JSON_Object *object, const char *name)
{
    return json_object_get_number (object, name);
}

inline long jog_l (const JSON_Object *object, const char *name)
{
    return std::lround(json_object_get_number (object, name));
}

// access to JSON number with 'null' returned as 'NAN'
double jog_n_nan (const JSON_Object *object, const char *name);
double jog_sn_nan (const JSON_Object *object, const char *name);

// access to JSON boolean field (replaces -1 with false)
inline bool jog_b (const JSON_Object *object, const char *name)
{
    // json_object_get_boolean returns -1 if field doesn't exit, so we
    // 'convert' -1 and 0 both to false with the following comparison:
    return json_object_get_boolean (object, name) > 0;
}

// interprets a string-encapsulated number "0" as false, all else as true
inline bool jog_sb (const JSON_Object *object, const char *name)
{
    return jog_sl (object, name) != 0;
}

// access to JSON array string fields, with NULL replaced by ""
const char* jag_s (const JSON_Array *array, size_t idx);

// access to JSON array number fields, encapsulated as string, with NULL replaced by 0
double jag_sn (const JSON_Array *array, size_t idx);

// access to JSON array number field (just a shorter name, returns 0 if not number)
inline double jag_n (const JSON_Array *array, size_t idx)
{
    return json_array_get_number (array, idx);
}

// access to JSON array number field with 'null' returned as 'NAN'
double jag_n_nan (const JSON_Array *array, size_t idx);

// access to JSON array boolean field (replaces -1 with false)
inline bool jag_b (const JSON_Array *array, size_t idx)
{
    // json_object_get_boolean returns -1 if field doesn't exit, so we
    // 'convert' -1 and 0 both to false with the following comparison:
    return json_array_get_boolean (array, idx) > 0;
}

// normalize a time in seconds since epoch to a full minute
inline time_t stripSecs ( double time )
{
    return time_t(time) - (time_t(time) % SEC_per_M);
}


#endif /* LTChannel_h */
