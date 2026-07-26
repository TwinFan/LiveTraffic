/// @file       LTSynthetic.h
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

#ifndef LTSynthetic_h
#define LTSynthetic_h

#include "LTChannel.h"
#include <set>

//
// MARK: SyntheticConnection
//

/// Synthetic tracking data creation
class SyntheticConnection : public LTFlightDataChannel
{
protected:
    /// What information are we storing for parked planes
    struct SynDataTy {
        LTFlightData::FDStaticData stat;        // plane's static data
        positionTy pos;                         // plane's position
    };
    /// Stores position per tracked plane
    typedef std::map<LTFlightData::FDKeyTy, SynDataTy> mapSynDataTy;
    /// @brief Position information per tracked plane
    /// @note Defined `static` to preserve information across restarts
    static mapSynDataTy mapSynData;

    /// @brief Hex IDs of aircraft that have been evicted from a stand by
    ///        the gate-handoff logic (see `FetchAllData`).
    /// @details Permanent for the plugin lifetime. Once a real, live-tracked
    ///          aircraft reaches FPH_PARKED on top of a stale parked-feed
    ///          ghost and the ghost is evicted, the ghost's hex id is
    ///          remembered here so neither:
    ///            (a) a subsequent FetchAllData pass falling inside the
    ///                async-SetInvalid teardown window can re-adopt it
    ///                into `mapSynData` (the "ghost comes back ~40 s
    ///                later via Synthetic" symptom — task #43), nor
    ///            (b) a future RealTraffic parked-feed re-fetch (which
    ///                runs every RT_PARKED_REFRESH_INTVL_S) can re-seed
    ///                that hex id with the stale gate position RT's DB
    ///                still carries.
    ///          Long-lived because RT's parked DB updates on a daily
    ///          cadence; a short grace period would just let the ghost
    ///          come back on the next 5-min re-fetch.
    /// @note `unsigned long` because that is the form
    ///       `LTFlightData::FDKeyTy::num` exposes for an ICAO transponder
    ///       hex (which is what we evict against; non-ICAO key types do
    ///       not participate in gate-handoff).
    static std::set<unsigned long> evictedHexIds;

public:
    /// Constructor
    SyntheticConnection ();
    /// No URL involved
    std::string GetURL (const positionTy&) override { return ""; }
    bool DoHoverDetection () const override { return false; }
    /// Scan for relevant flight data
    bool FetchAllData(const positionTy&) override;
    /// Processes the available stored data
    bool ProcessFetchedData () override;

    /// Record that a hex id was evicted from a stand by gate-handoff.
    /// Call from the eviction path so subsequent re-adoption attempts
    /// (Synthetic and RealTraffic parked-feed) can skip the ghost.
    static void MarkEvicted (unsigned long hex);
    /// Has the given hex id been evicted from a stand in this session?
    /// Read from both Synthetic re-adoption and RealTraffic
    /// ProcessParkedAcBuffer to short-circuit re-seeding.
    static bool WasEvicted (unsigned long hex);

protected:
    void Main () override;          ///< virtual thread main function
};

#endif /* LTSynthetic_h */
