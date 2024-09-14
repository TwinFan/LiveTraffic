/// @file       LTSynthetic.cpp
/// @brief      Synthetic tracking data, e.g. for parked aircraft
/// @details    Defines SyntheticConnection:
///             - Scans mapFd (all available tracking data in LiveTraffic)
///               for parked aircraft and keeps a position copy
///             - For any parked aircraft no longer actively served by any other channel,
///               send the same position data regularly
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

// All includes are collected in one header
#include "LiveTraffic.h"

//
// MARK: SyntheticConnection
//

#define SYNTHETIC_NAME                "Synthetic"               ///< Human-readable Name of connection

// Position information per tracked plane
SyntheticConnection::mapSynDataTy SyntheticConnection::mapSynData;

// Constructor
SyntheticConnection::SyntheticConnection () :
LTFlightDataChannel(DR_CHANNEL_SYNTHETIC, SYNTHETIC_NAME, CHT_SYNTHETIC_DATA)
{}


// virtual thread main function
void SyntheticConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_Synthetic", LC_ALL_MASK);

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


// Scan for relevant flight data
bool SyntheticConnection::FetchAllData(const positionTy&)
{
    // --- Parked Aircraft ---
    // Loop over all flight data and make sure
    // - 'Parked' aircraft are in our repository
    // - Not 'Parked' aircraft are not or no longer
    
    // Lock to acces mapFD
    std::lock_guard<std::mutex> lock (mapFdMutex);
    // Loop over all known flight data
    for (const auto& p: mapFd) {
        const LTFlightData::FDKeyTy& key = p.first;
        const LTFlightData& fd = p.second;
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        if (fd.hasAc()) {
            const LTAircraft& ac = *fd.GetAircraft();
            if (ac.GetFlightPhase() == FPH_PARKED) {
                // This a/c is parked, find/create the entry in our storage
                SynDataTy& parkDat = mapSynData[key];
                // we keep the previous heading because we looked that up from Startup location master data
                // (it's `NAN` if we just only created the entry, which is fine)
                const double prevHead = parkDat.pos.heading();
                parkDat.pos = ac.GetPPos();         // update storage with latest position
                parkDat.pos.heading() = prevHead;   // but don't overwrite heading (we know better ;-))
                // Copy the static data, too, just in case we need to re-create the plane
                parkDat.stat = fd.GetUnsafeStat();
            }
            else {
                // this a/c is _not_ or no longer parked, so we should forget about it in our stored data
                mapSynData.erase(p.first);
            }
                
            // Test if the aircraft came too close to any other parked aircraft on the ground
            if (ac.IsOnGrnd() && !ac.IsGroundVehicle()) {
                for (auto i = mapSynData.begin(); i != mapSynData.end(); ) {
                    // Only compare to other aircraft (not myself)
                    if (i->first == key) {
                        ++i;
                    } else {
                        const double dist = i->second.pos.dist(ac.GetPPos());
                        if (dist < GND_COLLISION_DIST)
                        {
                            // Remove the other parked aircraft
                            LOG_MSG(logDEBUG, "%s came too close to parked %s, removing the parked aircraft",
                                    fd.keyDbg().c_str(), i->first.c_str());
                            i = mapSynData.erase(i);
                        } else
                            ++i;
                    }
                }
            }
        }
    }
    return true;
}


// Processes the available stored data
bool SyntheticConnection::ProcessFetchedData ()
{
    // Timestamp with which we send the data
    const double tNow = (double)std::time(nullptr);
    // Camera pos
    const positionTy posCam = dataRefs.GetViewPos();
    // Squared search distance for distance comparison
    const double distSearchSqr = sqr(double(dataRefs.GetFdStdDistance_m()));
    
    // --- Parked Aircraft ---
    // For all stored aircraft
    // - lookup heading to have them point into the right direction of the startup position
    // - send an dynamic data update for LiveTraffic to process
    for (auto i = mapSynData.begin(); i != mapSynData.end();) {
        const LTFlightData::FDKeyTy& key = i->first;
        SynDataTy& parkDat = i->second;

        // Only process planes in search distance
        // We keep the data in memory, just in case we come back, but we don't feed data for unneeded planes
        if (parkDat.pos.distRoughSqr(posCam) > distSearchSqr) {
            ++i;                                                // next plane
            continue;
        }

        // Find the related flight data
        std::unique_lock<std::mutex> mapLock (mapFdMutex);      // lock the entire map
        LTFlightData& fd = mapFd[key];                          // fetch or create flight data
        mapLock.unlock();                                       // release the map lock
        
        // Haven't yet looked up startup position's heading?
        if (std::isnan(parkDat.pos.heading())) {
            parkDat.pos.heading() = LTAptFindStartupLoc(parkDat.pos).heading();
            // Still have no heading? That means we don't have a startup position...should not happen but does
            if (std::isnan(parkDat.pos.heading()))
            {
                i = mapSynData.erase(i);
                continue;
            }
        }
        
        // Send position for LiveTraffic's processing
        LTFlightData::FDDynamicData dyn;
        dyn.pChannel = this;
        dyn.spd = 0.0;
        dyn.vsi = 0.0;
        dyn.gnd = true;
        dyn.heading = parkDat.pos.heading();
        parkDat.pos.f.specialPos = SPOS_STARTUP;
        parkDat.pos.f.bHeadFixed = true;
        parkDat.pos.f.flightPhase = FPH_PARKED;

        // Update flight data
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        if (fd.key().empty()) {                                 // just created!
            // a/c exists no longer, we re-create it, can happen after disable/enable
            fd.SetKey(key);
            fd.UpdateData(parkDat.stat, parkDat.pos.dist(dataRefs.GetViewPos()));
            // to speed up creation of the actual aircraft we send the position for a past timestamp first
            dyn.ts = parkDat.pos.ts() = tNow - dataRefs.GetFdBufPeriod();
            fd.AddDynData(dyn, 0, 0, &parkDat.pos);
            LOG_MSG(logDEBUG, "Created parked aircraft %s", key.c_str());
        }

        // Add a (nearly) _current_ data item
        // We reduce timestamp a bit so we don't appear better than live stream's data,
        // which can't be current up to the minute
        dyn.ts = parkDat.pos.ts() = tNow - double(dataRefs.GetFdRefreshIntvl()/2);
        fd.AddDynData(dyn, 0, 0, &parkDat.pos);
        
        // next plane
        ++i;
    }
    return true;
}
