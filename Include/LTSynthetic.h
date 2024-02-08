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
public:
    /// Constructor
    SyntheticConnection ();
    /// No URL involved
    std::string GetURL (const positionTy&) override { return ""; }
    /// Scan for relevant flight data
    bool FetchAllData(const positionTy&) override;
    /// Processes the available stored data
    bool ProcessFetchedData () override;

protected:
    void Main () override;          ///< virtual thread main function
};

#endif /* LTSynthetic_h */
