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

// MARK: Generic types
// just a generic list of strings
typedef std::list<std::string> listStringTy;

// MARK: Thread control
extern std::thread FDMainThread;               // the main thread (LTFlightDataSelectAc)
extern std::thread CalcPosThread;              // the thread for pos calc (TriggerCalcNewPos)
extern std::mutex  FDThreadSynchMutex;         // supports wake-up and stop synchronization
extern std::condition_variable FDThreadSynchCV;
// stop all threads?
extern volatile bool bFDMainStop;
extern std::chrono::time_point<std::chrono::steady_clock> gNextWakeup;  ///< when to wake up next for network requests

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
    
private:
    bool bValid = true;             ///< valid connection?
    int errCnt = 0;                 ///< number of errors tolerated

public:
    LTChannel (dataRefsLT ch, const char* chName) : pszChName(chName), channel(ch) {}
    virtual ~LTChannel () {}
    
public:
    const char* ChName() const { return pszChName; }
    inline dataRefsLT GetChannel() const { return channel; }
    
    virtual bool IsLiveFeed () const = 0;
    virtual LTChannelType GetChType () const = 0;
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
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fd) = 0;
    // do something while disabled?
    virtual void DoDisabledProcessing () {}
    // (temporarily) close a connection, (re)open is with first call to FetchAll/ProcessFetchedData
    virtual void Close () {}
};

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
//MARK: LTFlightDataChannel
//
class LTFlightDataChannel : virtual public LTChannel {
protected:
    mutable float timeLastAcCnt = 0.0;      ///< when did we last count the a/c served by this channel?
    mutable int     numAcServed = 0;        ///< how many a/c do we feed when counted last?
public:
    LTFlightDataChannel () {}
    int GetNumAcServed () const override;   ///< how many a/c do we feed when counted last?
};

//
//MARK: LTACMasterdata
//

// list of a/c for which static data is yet missing
struct acStatUpdateTy {
public:
    LTFlightData::FDKeyTy acKey;    // to find master data
    std::string callSign;           // to query route information
    unsigned order = UINT_MAX;      ///< processing order, lowest value first
protected:
    bool bProcessed = false;        ///< has been processed by some master data channel?
    
public:
    acStatUpdateTy() {}
    acStatUpdateTy(const LTFlightData::FDKeyTy& k, std::string c, unsigned o) :
    acKey(k), callSign(c), order(o) {}

    inline bool operator == (const acStatUpdateTy& o) const
    { return acKey == o.acKey && callSign == o.callSign; }
    inline bool empty () const { return acKey.empty() && callSign.empty(); }
    
    inline void SetProcessed () { bProcessed = true; }
    inline bool HasBeenProcessed () const { return bProcessed; }
};
typedef std::vector<acStatUpdateTy> vecAcStatUpdateTy;

class LTACMasterdataChannel : virtual public LTChannel
{
private:
    /// @brief global list of a/c for which static data is yet missing
    /// (reset with every network request cycle)
    static vecAcStatUpdateTy vecAcStatUpdate;
    /// Lock controlling multi-threaded access to `listAcSTatUpdate`
    static std::mutex vecAcStatMutex;

protected:
    vecAcStatUpdateTy vecAc;        ///< object-private list of a/c to query
    std::string currKey;
    listStringTy  listMd;           // read buffer, one string per a/c data
public:
	LTACMasterdataChannel () {}
    virtual bool UpdateStaticData (const LTFlightData::FDKeyTy& keyAc,
                                   const LTFlightData::FDStaticData& dat);
    int GetNumAcServed () const override { return 0; }  ///< how many a/c do we feed?

    // request to fetch master data
    static void RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                   const std::string callSign,
                                   double distances);
    static void ClearMasterDataRequests ();
    
protected:
    // uniquely copies entries from listAcStatUpdate to vecAc
    void CopyGlobalRequestList ();
};

//
// MARK: LTOnlineChannel
//       Any request/reply via internet, uses CURL library
//
class LTOnlineChannel : virtual public LTChannel
{
protected:
    CURL* pCurl;                    // handle into CURL
    std::string requBody;           ///< body of a POST request
    int nTimeout;                   ///< current network timeout of this channel
    char* netData;                  // where the response goes
    size_t netDataPos;              // current write pos into netData
    size_t netDataSize;             // current size of netData
    char curl_errtxt[CURL_ERROR_SIZE];    // where error text goes
    long httpResponse;              // last HTTP response code
    
    static std::ofstream outRaw;    // output file for raw logging
    
public:
    LTOnlineChannel ();
    virtual ~LTOnlineChannel ();
    
protected:
    virtual bool InitCurl ();
    virtual void CleanupCurl ();
    // CURL callback
    static size_t ReceiveData ( const char *ptr, size_t size, size_t nmemb, void *userdata );
    // logs raw data to a text file
    void DebugLogRaw (const char* data, bool bHeader = true);
    
public:
    virtual bool FetchAllData (const positionTy& pos);
    virtual std::string GetURL (const positionTy& pos) = 0;
    virtual void ComputeBody (const positionTy& /*pos*/) { requBody.clear(); }   ///< in case of a POST request this call puts together its body
    virtual bool IsLiveFeed () const    { return true; }
    
    /// Is the given network error text possibly caused by problems querying the revocation list?
    static bool IsRevocationError (const std::string& err);
};

//
//MARK: LTFileChannel
//
class LTFileChannel : virtual public LTChannel
{
protected:
    // the path to the underlying historical files
    std::string pathBase;           // base path
    time_t zuluLastRead;            // the time of the last read file (UTC)
    listStringTy  listFd;           // read buffer, one string per a/c line in the hist file
public:
    LTFileChannel ();
    virtual bool IsLiveFeed () const    {return false;}
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
