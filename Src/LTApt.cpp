/// @file       LTApt.cpp
/// @brief      Access to X-Plane's `apt.dat` file(s) and data
/// @details    Scans `apt.dat` file for airport, runway, and taxiway information.\n
///             Finds potential runway for an auto-land flight.\n
///             Finds center lines on runways and taxiways to snap positions to.
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

#include "LiveTraffic.h"

// File paths

/// Path to the `scenery_packs.ini` file, which defines order and activation status of scenery packs
#define APTDAT_SCENERY_PACKS "Custom Scenery/scenery_packs.ini"
/// How a line in `scenery_packs.ini` file needs to start in order to be processed by us
#define APTDAT_SCENERY_LN_BEGIN "SCENERY_PACK "
/// Path to add after the scenery pack location read from the ini file
#define APTDAT_SCENERY_ADD_LOC "Earth nav data/apt.dat"
/// Path to the global airports file under Resources / Default
#define APTDAT_RESOURCES_DEFAULT "Resources/default scenery/default apt dat/"

// Log output
#define WARN_APTDAT_NOT_OPEN "Can't open '%s': %s"
#define WARN_APTDAT_FAILED   "Could not open ANY apt.dat file. No runway/taxiway info available to guide ground traffic."
#define WARN_APTDAT_READ_FAIL "Could not completely read '%s'. Some runway/taxiway info will be missing to guide ground traffic: %s"

/// This flag stops the file reading thread
volatile bool bStopThread = false;

//
// MARK: Airports, Runways and Taxiways
//

/// Represents a runway end as read from apt.dat
class RwyEnd {
public:
    char id[4];                         ///< runway id, like "05R"
    positionTy pos;                     ///< position of touch-down point (note: this already _excludes_ displaced threshold and is a place 10% into the runway)
    double len=NAN;                     ///< length of runway (note: this already _excludes_ both displaced thresholds)
};

/// List of runway ends
typedef std::list<RwyEnd> listRwyEndTy;

/// Represents an airport as read from apt.dat
class Apt {
protected:
    std::string id;                     ///< ICAO code or other unique id
    boundingBoxTy bounds;               ///< bounding box around airport, calculated from rwy and taxiway extensions
    listRwyEndTy listRwyEnd;            ///< List of runway ends
    static XPLMProbeRef YProbe;         ///< Y Probe for terrain altitude computation

public:
    /// Constructor expects an id
    Apt (const std::string& _id = "") : id(_id) {}
    
    /// Id of the airport, typicall the ICAO code
    std::string GetId () const { return id; }
    
    /// ID defined? (Used as an indicator for initialization or "is of interest")
    bool HasId () const { return !id.empty(); }
    
    // --- Runway ends ---
    
    /// The list of runway ends
    const listRwyEndTy& GetRwyEndsList () const { return listRwyEnd; }
    
    /// Any runways defined?
    bool HasRwyEnds () const { return !listRwyEnd.empty(); }
    
    /// Adds the rwy end to list and enlarges the airport's bounds
    void AddRwyEnd (RwyEnd&& re)
    {
        bounds.enlarge(re.pos);
        listRwyEnd.emplace_back(std::move(re));
    }

    /// Adds both rwy ends from apt.dat information fields
    void AddRwyEnds (double lat1, double lon1, double displaced1, const std::string& id1,
                     double lat2, double lon2, double displaced2, const std::string& id2)
    {
        positionTy re1 (lat1,lon1,NAN,NAN,NAN,NAN,NAN,positionTy::GND_ON);
        positionTy re2 (lat2,lon2,NAN,NAN,NAN,NAN,NAN,positionTy::GND_ON);
        vectorTy vecRwy = re1.between(re2);
        vecRwy.dist -= displaced1;
        vecRwy.dist -= displaced2;

        // move by displayed threshold
        // and then by another 10% to determine actual touch-down point
        re1 += vectorTy (vecRwy.angle,   displaced1 + vecRwy.dist * ART_RWY_TD_POINT_F );
        re2 += vectorTy (vecRwy.angle, -(displaced2 + vecRwy.dist * ART_RWY_TD_POINT_F));
        
        // 1st rwy end
        RwyEnd re;
        STRCPY_S(re.id, id1.c_str());
        re.pos = re1;
        re.pos.heading() = vecRwy.angle;
        re.len = vecRwy.dist;
        AddRwyEnd(std::move(re));
        
        // 2nd rwy end
        STRCPY_S(re.id, id2.c_str());
        re.pos = re2;
        re.pos.heading() = vecRwy.angle + 180;
        if (re.pos.heading() >= 360.0) re.pos.heading() -= 360.0;
        re.len = vecRwy.dist;
        AddRwyEnd(std::move(re));
    }

    /// @brief Update rwy ends with proper altitude
    /// @note Must be called from XP's main thread, otherwise Y probes won't work
    void UpdateRwyAltitudes ()
    {
        for (RwyEnd& re: listRwyEnd)                // for all their rwys
            if (std::isnan(re.pos.alt_m()))         // if altitude is missing
                // determine the rwy end's altitude (this will auto-init the YProbe)
                re.pos.alt_m() = YProbe_at_m(re.pos, YProbe);
    }
    
    /// Destroy the YProbe
    static void DestroyYProbe ()
    {
        if (YProbe) {
            XPLMDestroyProbe(YProbe);
            YProbe = NULL;
        }
    }

    /// @brief Returns iterator to the opposite end of given runway
    /// @details Rwy ends are added to `listRwyEnd` in pairs. So if we are given an even iterator we need to
    ///          return the next iterator, otherwise the previous
    listRwyEndTy::const_iterator GetOppositeRwyEnd(listRwyEndTy::const_iterator& iterRE) const
    {
        // even case
        if (std::distance(listRwyEnd.cbegin(), iterRE) % 2 == 0)
            return std::next(iterRE);
        else
            return std::prev(iterRE);
    }
    
    // --- Bounding box ---

    /// Returns the bounding box of the airport as defined by all runways and taxiways
    const boundingBoxTy& GetBounds () const { return bounds; }
    
    /// Does airport contain this point?
    bool Contains (const positionTy& pos) const { return bounds.contains(pos); }
};

// Y Probe for terrain altitude computation
XPLMProbeRef Apt::YProbe = NULL;

/// Map of airports, key is the id (typically: ICAO code)
typedef std::map<std::string, Apt> mapAptTy;

/// Global map of airports
static mapAptTy gmapApt;

/// Lock to access global map of airports
static std::mutex mtxGMapApt;

//
// MARK: File Reading Thread
// This code runs in the thread for file reading operations
//

// Add airport to list of airports
static void AddApt (Apt&& apt)
{
    // Fancy debug-level logging message, listing all runways
    if (dataRefs.GetLogLevel() == logDEBUG) {
        std::string apts;       // concatenate airport ids
        const listRwyEndTy& listRwyEnd = apt.GetRwyEndsList();
        if (!listRwyEnd.empty()) {
            bool bFirst = true;
            for (const RwyEnd& re: listRwyEnd) {
                if (!apts.empty())
                    apts += bFirst ? " / " : "-";
                bFirst ^= true;
                apts += re.id;
            }
        }
        LOG_MSG(logDEBUG, "apt.dat: Added %s at %s with %d runways: %s",
                apt.GetId().c_str(),
                std::string(apt.GetBounds()).c_str(),
                (int)apt.GetRwyEndsList().size(), apts.c_str());
    }

    // Access to the list of airports is guarded by a lock
    {
        std::lock_guard<std::mutex> lock(mtxGMapApt);
        std::string key = apt.GetId();          // make a copy of the key, as `apt` gets moved soon:
        gmapApt.emplace(std::move(key), std::move(apt));
    }
}

/// Read airports in the one given `apt.dat` file
static void ReadOneAptFile (std::ifstream& fIn, const boundingBoxTy& box)
{
    // Walk the file
    Apt apt;
    while (!bStopThread && fIn)
    {
        // read a line
        std::string lnBuf;
        safeGetline(fIn, lnBuf);

        // test for beginning of an airport
        if (lnBuf.size() > 10 &&
            lnBuf[0] == '1' &&
            (lnBuf[1] == ' ' || lnBuf[1] == '\t'))
        {
            // found an airport's beginning
            
            // If the previous airport is valid add it to the list
            if (apt.HasId() && apt.HasRwyEnds())
                AddApt(std::move(apt));
            // clear the airport object
            apt = Apt();
            
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            if (fields.size() >= 5 &&           // line contains an airport id, and
                gmapApt.count(fields[4]) == 0)  // airport is not yet defined in map
            {
                // re-init apt object, now with the proper id defined
                apt = Apt(fields[4]);
            }
        }
        
        // test for a runway...just to find location info
        else if (apt.HasId() &&             // an airport identified and of interest?
            lnBuf.size() > 20 &&            // line long enough?
            lnBuf[0] == '1' &&              // starting with "100 "?
            lnBuf[1] == '0' &&
            lnBuf[2] == '0' &&
            (lnBuf[3] == ' ' || lnBuf[3] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            if (fields.size() == 26) {      // runway description has to have 26 fields
                const double lat = std::atof(fields[ 9].c_str());
                const double lon = std::atof(fields[10].c_str());
                if (-90.0 <= lat && lat <= 90.0 &&
                    -180.0 <= lon && lon < 180.0)
                {
                    // Have we accepted the airport already?
                    // Or - this being the first rwy - does the rwy lie in the bounding box?
                    if (apt.HasRwyEnds() ||
                        box.contains(positionTy(lat,lon)))
                    {
                        // add both runway ends to the airport
                        apt.AddRwyEnds(lat, lon,
                                       std::atof(fields[11].c_str()),   // displayced
                                       fields[ 8],                      // id
                                       // other rwy end:
                                       std::atof(fields[18].c_str()),   // lat
                                       std::atof(fields[19].c_str()),   // lon
                                       std::atof(fields[20].c_str()),   // displayced
                                       fields[17]);                     // id
                    }
                    // airport is outside bounding box -> mark it uninteresting
                    else
                    {
                        // clear the airport object
                        apt = Apt();
                    }
                }   // if lat/lon in acceptable range
            }       // if line contains 26 field values
        }           // if a runway line startin with "100 "
    }               // for each line of the apt.dat file
    
    // If the last airport read is valid don't forget to add it to the list
    if (apt.HasId() && apt.HasRwyEnds())
        AddApt(std::move(apt));
}

/// @brief Read airports from apt.dat files around a given center position
/// @details This function first walks along the `scenery_packs.ini` file
///          and reads all `apt.dat` files available in the scenery packs listed there in the given order.
///          Lastly, it also reads the generic `apt.dat` file given in `APTDAT_RESOURCES_DEFAULT`.
/// @see Understanding scener order: https://www.x-plane.com/kb/changing-custom-scenery-load-order-in-x-plane-10/
/// @param ctr Center position
/// @param radius Search radius around center position in meter
void AsyncReadApt (positionTy ctr, double radius)
{
    static size_t lenSceneryLnBegin = strlen(APTDAT_SCENERY_LN_BEGIN);
    
    // To avoid costly distance calculations we define a bounding box
    // just by calculating lat/lon values north/east/south/west of given pos
    // and include all airports with coordinates falling into it
    const boundingBoxTy box (ctr, radius);
    
    // Count the number of files we have accessed
    int cntFiles = 0;

    // Try opening scenery_packs.ini
    std::ifstream fScenery (LTCalcFullPath(APTDAT_SCENERY_PACKS));
    while (!bStopThread && fScenery.good() && fScenery.is_open())
    {
        // read a line from scenery_packs.ini
        std::string lnScenery;
        safeGetline(fScenery, lnScenery);
        
        // we only process lines starting with "SCENERY_PACK ",
        // ie. we skip any header info and also lines with SCENERY_PACK_DISABLED
        if (lnScenery.length() <= lenSceneryLnBegin ||
            lnScenery.substr(0,lenSceneryLnBegin) != APTDAT_SCENERY_LN_BEGIN)
            continue;
        
        // the remainder is a path into X-Plane's main folder
        lnScenery.erase(0,lenSceneryLnBegin);
        lnScenery = LTCalcFullPath(lnScenery);      // make it a full path
        lnScenery += APTDAT_SCENERY_ADD_LOC;        // add the location to the actual `apt.dat` file
        
        // open that apt.dat
        std::ifstream fIn (lnScenery);
        if (fIn.good() && fIn.is_open()) {
            LOG_MSG(logDEBUG, "Reading apt.dat from %s", lnScenery.c_str());
            ReadOneAptFile(fIn, box);
            cntFiles++;
        }
        
        // problem was not just "not found" (which we ignore for scenery packs) or eof?
        if (!fIn && errno != ENOENT && !fIn.eof()) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_CFG_FILE_READ,
                    lnScenery.c_str(), sErr);
        }
        
        fIn.close();

    } // processing scenery_packs.ini
    
    // Last but not least we also process the global generic apt.dat file
    if (!bStopThread)
    {
        const std::string sFileName = LTCalcFullPath(APTDAT_RESOURCES_DEFAULT APTDAT_SCENERY_ADD_LOC);
        std::ifstream fIn (sFileName);
        if (fIn.good() && fIn.is_open()) {
            LOG_MSG(logDEBUG, "Reading apt.dat from %s", sFileName.c_str());
            ReadOneAptFile(fIn, box);
            cntFiles++;
        }

        // problem was not just eof?
        if (!fIn && !fIn.eof()) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_CFG_FILE_READ,
                    sFileName.c_str(), sErr);
        }
        
        fIn.close();
    }
    
    // Not successful in opening ANY apt.dat file?
    if (!cntFiles) {
        SHOW_MSG(logWARN, WARN_APTDAT_FAILED);
        return;
    }
    
    LOG_MSG(logDEBUG, "Done reading from %d apt.dat files, have now %d airports",
            cntFiles, (int)gmapApt.size());
}


//
// MARK: X-Plane Main Thread
// This code runs in X-Plane's thread, called from XP callbacks
//

/// Is currently an async operation running to refresh the airports from apt.dat?
static std::future<void> futRefreshing;
        
/// Last position for which airports have been read
static positionTy lastCameraPos;

/// New airports added, so that a call to LTAptUpdateRwyAltitude(9 is necessary?
static bool bAptsAdded = false;
        
// Start reading apt.dat file(s)
bool LTAptEnable ()
{
    LTAptRefresh();
    return true;
}

/// Update altitudes of runways
void LTAptUpdateRwyAltitudes ()
{
    // access is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // loop all airports and their runways
    for (mapAptTy::value_type& p: gmapApt)
        p.second.UpdateRwyAltitudes();
    
    LOG_MSG(logDEBUG, "apt.dat: Finished updating ground altitudes");
}

// Update the airport data with airports around current camera position
void LTAptRefresh ()
{
    // Safety check: Thread already running?
    // Future object is valid, i.e. initialized with an async operation?
    if (futRefreshing.valid() &&
        // but status is not yet ready?
        futRefreshing.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            // then stop here
            return;
        
    // Distance since last read not far enough?
    // Must have travelled at least as far as standard search radius for planes:
    const positionTy camera = DataRefs::GetViewPos();
    if (!camera.isNormal(true))                     // have no good camery position (yet)
        return;

    double radius = dataRefs.GetFdStdDistance_m();
    if (lastCameraPos.dist(camera) < radius)        // is false if lastCameraPos is NAN
    {
        // Didn't move far, so no new scan for new airports needed.
        // But do we need to check for rwy altitudes after last scan of apt.dat file?
        if (bAptsAdded)
            LTAptUpdateRwyAltitudes();
        bAptsAdded = false;
        return;
    }
    else
        lastCameraPos = camera;

    // Start the thread to read apt.dat, using current camera position as center point
    // and _double_ plane search radius as search radius
    radius *= 2;
    LOG_MSG(logDEBUG, "Starting thread to read apt.dat for airports %.1fnm around %s",
            radius / M_per_NM, std::string(lastCameraPos).c_str());
    bStopThread = false;
    futRefreshing = std::async(std::launch::async,
                               AsyncReadApt, lastCameraPos, radius);
    // need to check for rwy altitudes soon!
    bAptsAdded = true;
}

// Return the best possible runway to auto-land at
positionTy LTAptFindRwy (const LTAircraft& _ac)
{
    // --- Preparation of aircraft-related data ---
    // allowed VSI range depends on aircraft model, converted to m/s
    const double vsi_min = _ac.mdl.VSI_FINAL * ART_RWY_MAX_VSI_F * Ms_per_FTm;
    const double vsi_max = _ac.mdl.VSI_FINAL / ART_RWY_MAX_VSI_F * Ms_per_FTm;
    
    // last known go-to position of aircraft, serving as start of search
    const positionTy& from = _ac.GetToPos();
    // and the speed to use, cut off at a reasonable approach speed:
    const double speed_m_s = std::min (_ac.GetSpeed_m_s(),
                                       _ac.mdl.FLAPS_DOWN_SPEED * ART_APPR_SPEED_F / KT_per_M_per_S);
    
    // --- Variables holding Best Match ---
    // this will point to the best matching apt
    mapAptTy::const_iterator iterBestApt;
    // ...and this to the best matching runway end
    listRwyEndTy::const_iterator iterBestRwyEnd;
    // The heading diff of the best match to its runway
    // (initialized to the max allowed value so that worse heading diffs aren't considered)
    double bestHeadingDiff = ART_RWY_MAX_HEAD_DIFF;
    // when would we arrive there?
    double bestArrivalTS = NAN;
    
    // --- Iterate the airports ---
    // Access to the list of airports is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // loop over airports
    for (mapAptTy::const_iterator iterApt = gmapApt.cbegin();
         iterApt != gmapApt.cend();
         ++iterApt)
    {
        const Apt& apt = iterApt->second;
        
        // loop over rwy ends of this airport
        for (listRwyEndTy::const_iterator iterRE = apt.GetRwyEndsList().cbegin();
             iterRE != apt.GetRwyEndsList().cend();
             ++iterRE)
        {
            // Test if runway is an option
            const RwyEnd& re = *iterRE;
            // We need to know the runway's altitude
            if (std::isnan(re.pos.alt_m()))
                continue;
            // 1. Heading of rwy as compared to heading of flight
            if (std::abs(HeadingDiff(from.heading(), re.pos.heading())) > ART_RWY_MAX_HEAD_DIFF)
                continue;
            // 2. Heading towards rwy, compared to current flight's heading
            const double bearing = from.angle(re.pos);
            const double headingDiff = std::abs(HeadingDiff(from.heading(), bearing));
            if (headingDiff > bestHeadingDiff)      // worse than best known match?
                continue;
            // 3. Vertical speed, for which we need to know distance / flying time
            const double dist = from.dist(re.pos);
            const double d_ts = dist / speed_m_s;
            const double vsi = (re.pos.alt_m()-from.alt_m())/d_ts;
            if (vsi < vsi_min || vsi > vsi_max)
                continue;
            
            // We've got a match!
            iterBestApt = iterApt;              // the airport
            iterBestRwyEnd = iterRE;            // the runway
            bestHeadingDiff = headingDiff;      // the heading diff (which would be a selection criterion on several rwys match)
            bestArrivalTS = from.ts() + d_ts;   // the arrival timestamp
        }
    }
    
    // Didn't find a suitable airport?
    if (std::isnan(bestArrivalTS)) {
        LOG_MSG(logDEBUG, "Didn't find runway for %s",
                std::string(_ac).c_str());
        return positionTy();
    }
    
    // Found a match!
    positionTy retPos = iterBestRwyEnd->pos;
    retPos.ts() = bestArrivalTS;                        // Arrival time
    retPos.flightPhase = LTAPIAircraft::FPH_TOUCH_DOWN; // This is a calculated touch-down point
    LOG_MSG(logDEBUG, "Found runway %s/%s at %s for %s",
            iterBestApt->second.GetId().c_str(),
            iterBestRwyEnd->id,
            std::string(retPos).c_str(),
            std::string(_ac).c_str());
    return retPos;
}

// Cleanup
void LTAptDisable ()
{
    // Stop all threads
    bStopThread = true;
    
    // wait for refresh function
    if (futRefreshing.valid())
        futRefreshing.wait();
    
    // destroy the Y Probe
    Apt::DestroyYProbe();
}
