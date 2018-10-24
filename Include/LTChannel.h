//
//  LTChannel.h
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

#ifndef LTChannel_h
#define LTChannel_h

#include <list>
#include "curl/curl.h"              // for CURL*

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

//
//MARK: Flight Data Connection (abstract base class)
//
class LTChannel
{
protected:
    dataRefsLT channel;             // id of channel (see dataRef)

private:
    bool bValid;                    // valid connection?
    int errCnt;                     // number of errors tolerated

public:
    LTChannel (dataRefsLT ch) : channel(ch), bValid(false), errCnt(0) {}
    virtual ~LTChannel ();
    
public:
    static const char* ChId2String (dataRefsLT ch);
    inline const char* ChName() const { return ChId2String(channel); }
    
    virtual bool IsLiveFeed () const = 0;
    virtual bool IsValid () const {return bValid;}      // good to provide data after init?
    virtual void SetValid (bool _valid, bool bMsg = true);
    virtual bool IncErrCnt();               // increases error counter, returns if (still) valid
    virtual bool IsEnabled () const;
    virtual void SetEnable (bool bEnable);
    
public:
    // TODO: Rename to FetchAllData
    virtual bool FetchAllData (const positionTy& pos) = 0;
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fd) = 0;
};

// Collection of smart pointers requires C++ 17 to compile correctly!
#if __cplusplus < 201703L
#error Collection of smart pointers requires C++ 17 to compile correctly
#endif

// a smart pointer to the above flight data connection class
typedef std::unique_ptr<LTChannel> ptrLTChannelTy;

// a list of flight data connections smart pointers
typedef std::list<ptrLTChannelTy> listPtrLTChannelTy;

//
//MARK: LTFlightDataChannel
//
class LTFlightDataChannel : virtual public LTChannel {
public:
    LTFlightDataChannel () {}
};

//
//MARK: LTACMasterdata
//
class LTACMasterdataChannel : virtual public LTChannel
{
protected:
    std::string currKey;
    listStringTy  listMd;           // read buffer, one string per a/c data
public:
    virtual bool UpdateStaticData (std::string key,
                                   const LTFlightData::FDStaticData& dat);
};

//
// MARK: LTOnlineChannel
//       Any request/reply via internet, uses CURL library
//
class LTOnlineChannel : virtual public LTChannel
{
protected:
    CURL* pCurl;                    // handle into CURL
    char* netData;                  // where the response goes
    size_t netDataPos;              // current write pos into netData
    size_t netDataSize;             // current size of netData
    char curl_errtxt[CURL_ERROR_SIZE];    // where error text goes
    long httpResponse;              // last HTTP response code
    
public:
    LTOnlineChannel ();
    virtual ~LTOnlineChannel ();
    
protected:
    bool InitCurl ();
    void CleanupCurl ();
    // CURL callback
    static size_t ReceiveData ( const char *ptr, size_t size, size_t nmemb, void *userdata );
    
public:
    virtual bool FetchAllData (const positionTy& pos);
    virtual std::string GetURL (const positionTy& pos) = 0;
    virtual bool IsLiveFeed () const    { return true; }
    virtual bool IsValid () const       {return true;}
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
//MARK: OpenSky
//
class OpenSkyConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    OpenSkyConnection () :
    LTChannel(DR_CHANNEL_OPEN_SKY_ONLINE),
    LTOnlineChannel(),
    LTFlightDataChannel()  {}
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};

//
//MARK: Flightradar24
//
class FlightradarConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    FlightradarConnection () :
    LTChannel(DR_CHANNEL_FLIGHTRADAR24_ONLINE),
    LTOnlineChannel(),
    LTFlightDataChannel()  {}
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};

//
//MARK: ADS-B Exchange
//
class ADSBExchangeConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    ADSBExchangeConnection () :
    LTChannel(DR_CHANNEL_ADSB_EXCHANGE_ONLINE),
    LTOnlineChannel(),
    LTFlightDataChannel()  {}
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};

//
//MARK: ADS-B Exchange Historical Data
//
class ADSBExchangeHistorical : public LTFileChannel, LTFlightDataChannel
{
    // helper type to select best receiver per a/c from multiple in one file
    struct FDSelection
    {
        int quality;                // quality value
        std::string ln;             // line of flight data from file
    };
    
    typedef std::map<std::string, FDSelection> mapFDSelectionTy;
    
public:
    ADSBExchangeHistorical (std::string base = ADSBEX_HIST_PATH,
                            std::string fallback = ADSBEX_HIST_PATH_2);
    virtual bool FetchAllData (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};


//
//MARK: OpenSkyAcMasterdata
//
class OpenSkyAcMasterdata : public LTOnlineChannel, LTACMasterdataChannel
{
protected:
    listStringTy invIcaos;          // list of not-to-query-again icaos
public:
    OpenSkyAcMasterdata () :
    LTChannel(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA),
    LTOnlineChannel(),
    LTACMasterdataChannel()  {}
public:
    virtual bool FetchAllData (const positionTy& pos);
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
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

//
//MARK: Aircraft Maintenance (called from flight loop callback)
//
void LTFlightDataAcMaintenance();


#endif /* LTChannel_h */
