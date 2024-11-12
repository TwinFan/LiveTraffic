/// @file       LTSayIntentions.cpp
/// @brief      Channel to SayIntentions traffic map
/// @see        https://tracker.sayintentions.ai/
/// @details    Defines SayIntentionsConnection:\n
///             Takes traffic from https://lambda.sayintentions.ai/tracker/map
/// @author     Birger Hoppe
/// @copyright  (c) 2024 Birger Hoppe
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

#include "LiveTraffic.h"

//
// MARK: SayIntentions
//

// Constructor
SayIntentionsConnection::SayIntentionsConnection () :
LTFlightDataChannel(DR_CHANNEL_SAYINTENTIONS, SI_NAME)
{
    // purely informational
    urlName  = SI_CHECK_NAME;
    urlLink  = SI_CHECK_URL;
    urlPopup = SI_CHECK_POPUP;
}

// virtual thread main function
void SayIntentionsConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_SI", LC_ALL_MASK);
    
    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // basis for determining when to be called next
            tNextWakeup = std::chrono::steady_clock::now();
            
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // Next wakeup is "refresh interval" from _now_
                tNextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());
                
                // fetch data and process it
                if (FetchAllData(pos) && ProcessFetchedData())
                        // reduce error count if processed successfully
                        // as a chance to appear OK in the long run
                        DecErrCnt();
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNextWakeup += std::chrono::seconds(1);
            }
            
            // sleep for FD_REFRESH_INTVL or if woken up for termination
            // by condition variable trigger
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_until(lk, tNextWakeup,
                                           [this]{return !shallRun();});
            }
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
}


// returns the constant URL to SayIntentions traffic
std::string SayIntentionsConnection::GetURL (const positionTy&)
{
    return SI_URL_ALL;
}

// Process response, selecting traffic around us and forwarding to the processing queues
bool SayIntentionsConnection::ProcessFetchedData ()
{
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // Only proceed in case HTTP response was OK
    if (httpResponse != HTTP_OK) {
        IncErrCnt();
        return false;
    }
    
    // now try to interpret it as JSON
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // The entire JSON is an array of objects
    const JSON_Array* pArrAc = json_array(pRoot.get());
    if (!pArrAc) {
        LOG_MSG(logERR, "Expected a JSON Array, but got type %d",
                (int)json_type(pRoot.get()));
        IncErrCnt();
        return false;
    }
    
    // TODO: Implement processing of SI flight data
    LOG_MSG(logDEBUG, "Received %d flights from SayIntentions",
            (int)json_array_get_count(pArrAc));
    
    // all good
    return true;
}
