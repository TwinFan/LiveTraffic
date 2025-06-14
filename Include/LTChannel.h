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
        CHT_SYNTHETIC_DATA,         ///< data created internally by LiveTraffic, like kept parked aircraft, has lower priority than any real-life data
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
    virtual int GetNumAcServed () const = 0;     ///< how many a/c do we feed?
    
    // shall data of this channel be subject to LTFlightData::DataSmoothing?
    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
    { gndRange = 0.0; airbRange = 0.0; return false; }
    // shall data of this channel be subject to hovering flight detection?
    virtual bool DoHoverDetection () const { return false; }

public:
    virtual bool FetchAllData (const positionTy& pos) = 0;
    virtual bool ProcessFetchedData () = 0;
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
    /// @brief logs raw data to a text file
    /// @param data The data to print, assumed to be zero-terminated text
    /// @param httpCode `-1` for SENDing data, any other code is a received HTTP response code
    /// @param bHeader Shall the header with timestamp be printed?
    void DebugLogRaw (const char* data, long httpCode, bool bHeader = true);
    /// URL-encode a string
    std::string URLEncode (const std::string& s) const;
    
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
    LTFlightDataChannel (dataRefsLT ch, const char* chName, LTChannelType eType = CHT_TRACKING_DATA) :
        LTOnlineChannel(ch, eType, chName) {}
    int GetNumAcServed () const override;   ///< how many a/c do we feed when counted last?
};

//
//MARK: LTACMasterdata
//

/// @brief list of a/c for which static data is yet missing
/// @note If no call sign is set then we ask for a/c master data, otherwise for route information
struct acStatUpdateTy {
public:
    LTFlightData::FDKeyTy acKey;    ///< a/c key to find a/c master data
    std::string callSign;           ///< call sign to query route information
    unsigned long dist = UINT_MAX;  ///< distance of plane to camera, influences priority

    DatRequTy type = DATREQU_NONE;   ///< type of this master data request
    
    /// Request Attempt count, allows to route request to services of different priority
    unsigned nRequCount = 0;
    
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

/// @brief Parent class for master data channels, handles queue for master data requests
/// @details Static functions of LTACMasterdataChannel handle the queue of
///          requests for master data. Implementations of this class register
///          themselves and this way form a queue of channels.
///          Requests received through RequestMasterData()
///          and RequestRouteInfo() are passed on to the channels in the list.
///          Each channel implementation can accept a request, add it to a local
///          queue and (try to) process it, or reject it right away,
///          so that it is offered to the next channel in the list.
class LTACMasterdataChannel : public LTOnlineChannel
{
protected:
    /// Lock synchronizing any thread access to the request lists
    static std::recursive_mutex mtxMaster;
    /// List of register master data services, in order of priority
    static std::list<LTACMasterdataChannel*> lstChn;
    /// list of static data requests for the current channel
    setAcStatUpdateTy setAcStatRequ;
    /// Last time the above list of requests got maintained, ie. cleared from outdated stuff
    float tSetRequCleared = 0.0f;
    /// List of a/c to ignore, as we know we don't get data online
    setFdKeyTy setIgnoreAc;
    /// List of call signs to ignore, as we know we don't get route info online
    setStringTy setIgnoreCallSign;
    /// The request currently being processed
    acStatUpdateTy currRequ;

public:
    // Constructor
    LTACMasterdataChannel (dataRefsLT ch, const char* chName);

    int GetNumAcServed () const override { return 0; }  ///< how many a/c do we feed?
    
protected:
    /// @brief Called from static functions to receive a request for processing
    /// @returns if request has been accepted
    virtual bool AcceptRequest (const acStatUpdateTy& requ) = 0;
    /// Add the request to the set if not duplicate
    bool InsertRequest (const acStatUpdateTy& requ);
    /// Is any request waiting?;
    bool HaveAnyRequest () const { return !setAcStatRequ.empty(); }
    /// @brief Fetch next master data request from our set into `currRequ`
    /// @returns `true` if a request has been passed, `false` if no request was waiting
    bool FetchNextRequest ();
    /// Called regularly to keep the request queue updated
    void MaintainMasterDataRequests ();

    /// Perform the update to flight's static data
    bool UpdateStaticData (const LTFlightData::FDKeyTy& keyAc,
                           const LTFlightData::FDStaticData& dat);

    /// Add the current request `currRequ` to the ignore list
    void AddIgnore ();
    /// Is the request already in one of the ignore lists?
    bool ShallIgnore (const acStatUpdateTy& requ) const;
    
    // *** Static function coordinating requests between channel objects ***
public:
    /// Add request to fetch master data (returns `true` if added, `false` if duplicate)
    static bool RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                   double distance)
    { return RequestMasterData (keyAc, "", distance); }
    /// Add request to fetch route info (returns `true` if added, `false` if duplicate)
    static bool RequestRouteInfo  (const LTFlightData::FDKeyTy& keyAc,
                                   const std::string& callSign,
                                   double distance)
    { return callSign.empty() ? false : RequestMasterData (keyAc, callSign, distance); }
    
protected:
    /// @brief Register a master data channel, that will be called to process requests
    /// @note The order, in which registration happens, serves as a priority
    static void RegisterMasterDataChn (LTACMasterdataChannel* pChn, bool bToFrontOfQueue);
    /// Unregister a mster data channel
    static void UnregisterMasterDataChn (LTACMasterdataChannel* pChn);
    /// Generically, uniquely add request to fetch data (returns `true` if added, `false` if duplicate)
    static bool RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                   const std::string& callSign,
                                   double distance);
    /// @brief Pass on a message to the next channel
    /// @param pChn The calling channel, or `nullptr` if to process the channels from the beginning
    /// @param requ The request to be passed on
    /// @returns `true` if any channel accepted the request
    static bool PassOnRequest (LTACMasterdataChannel* pChn, const acStatUpdateTy& requ);

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
// MARK: Network helper functions
//

// cleanup the slist, returns if something was actually deleted
bool CurlCleanupSlist (curl_slist* &pList);

//
//MARK: Parson Helper Functions
//

/// Smart pointer that guarantees freeing of JSON memory by calling `json_value_free` when it goes out of context
class JSONRootPtr : public std::unique_ptr<JSON_Value,decltype(&json_value_free)>
{
public:
    /// Constructs a JSON root object from a given JSON string that is passed to `json_parse_string`
    JSONRootPtr (const char* sJson) :
    std::unique_ptr<JSON_Value,decltype(&json_value_free)>
    (json_parse_string(sJson), &json_value_free)
    {}
    /// Don't copy
    JSONRootPtr (const JSONRootPtr&) = delete;
};

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
    return json_object_dotget_number (object, name);
}

inline long jog_l (const JSON_Object *object, const char *name)
{
    return std::lround(json_object_dotget_number (object, name));
}

/// access to JSON number with 'null' returned as 'NAN'
double jog_n_nan (const JSON_Object *object, const char *name);
/// access to JSON number, encoded as string, with 'null' and empty string returned as 'NAN'
double jog_sn_nan (const JSON_Object *object, const char *name);

// access to JSON boolean field (replaces -1 with false)
inline bool jog_b (const JSON_Object *object, const char *name)
{
    // json_object_dotget_boolean returns -1 if field doesn't exit, so we
    // 'convert' -1 and 0 both to false with the following comparison:
    return json_object_dotget_boolean (object, name) > 0;
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
    // json_array_get_boolean returns -1 if field doesn't exit, so we
    // 'convert' -1 and 0 both to false with the following comparison:
    return json_array_get_boolean (array, idx) > 0;
}

// access to JSON array integer number field
inline long jag_l (const JSON_Array *array, size_t idx)
{
    return std::lround(json_array_get_number(array, idx));
}

/// return an entire JSON array as float vector
std::vector<float> jag_f_vector (const JSON_Array* array);

/// Find first non-Null value in several JSON array fields
JSON_Value* jag_FindFirstNonNull(const JSON_Array* pArr, std::initializer_list<size_t> aIdx);

// normalize a time in seconds since epoch to a full minute
inline time_t stripSecs ( double time )
{
    return time_t(time) - (time_t(time) % SEC_per_M);
}


#endif /* LTChannel_h */
