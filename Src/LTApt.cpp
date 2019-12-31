/// @file       LTApt.cpp
/// @brief      Access to X-Plane's `apt.dat` file(s) and data
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

/// Path to the global airports file under Custom Scenery
#define APTDAT_CUSTOM_GLOBAL "Custom Scenery/Global Airports/Earth nav data/apt.dat"
/// Path to the global airports file under Resources / Default
#define APTDAT_RESOURCES_DEFAULT "Resources/default scenery/default apt dat/Earth nav data/apt.dat"

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
public:
    char id[8] = {0,0,0,0,0,0,0,0};     ///< ICAO code or other unique id
    boundingBoxTy bounds;               ///< bounding box around airport
    
    listRwyEndTy listRwyEnd;            ///< List of runway ends
    void AddRwyEnd (RwyEnd&& re);       ///< adds the rwy end to list and enlarges the airport's bounds
    
    /// Adds both rwy ends from apt.dat information fields
    void AddRwyEnds (double lat1, double lon1, double displayed1, const std::string& id1,
                     double lat2, double lon2, double displayed2, const std::string& id2);
    
    /// Returns iterator to the opposite end of given runway
    listRwyEndTy::const_iterator GetOppositeRwyEnd(listRwyEndTy::const_iterator& iterRE) const;
    
    bool HasId () const { return id[0] != '\0'; }
};

/// Vector of airports
typedef std::vector<Apt> vecAptTy;

/// Global vector of airports
static vecAptTy gvecApt;

/// Lock to access global vector of airports
static std::mutex mtxGVecApt;

// adds the rwy end to list and enlarges the airport's bounds
void Apt::AddRwyEnd(RwyEnd&& re)
{
    bounds.enlarge(re.pos);
    listRwyEnd.emplace_back(std::move(re));
}

// Adds both rwy ends from apt.dat information fields
void Apt::AddRwyEnds (double lat1, double lon1, double displaced1, const std::string& id1,
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

// Returns iterator to the opposite end of given runway
/// Rwy ends are added to `listRwyEnd` in pairs. So if we are given an even iterator we need to
/// return the next iterator, otherwise the previous
listRwyEndTy::const_iterator Apt::GetOppositeRwyEnd(listRwyEndTy::const_iterator& iterRE) const
{
    // even case
    if (std::distance(listRwyEnd.cbegin(), iterRE) % 2 == 0)
        return std::next(iterRE);
    else
        return std::prev(iterRE);
}

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
        if (!apt.listRwyEnd.empty()) {
            bool bFirst = true;
            for (const RwyEnd& re: apt.listRwyEnd) {
                if (!apts.empty())
                    apts += bFirst ? " / " : "-";
                bFirst ^= true;
                apts += re.id;
            }
        }
        LOG_MSG(logDEBUG, "apt.dat: Added %s at %s with %d runways: %s",
                apt.id, std::string(apt.bounds).c_str(),
                (int)apt.listRwyEnd.size(), apts.c_str());
    }

    // Access to the list of airports is guarded by a lock
    {
        std::lock_guard<std::mutex> lock(mtxGVecApt);
        gvecApt.emplace_back(std::move(apt));
    }
}

/// @brief Read airports from apt.dat around a given center position
/// @param ctr Center position
/// @param radius Search radius around center position in meter
void AsyncReadApt (positionTy ctr, double radius)
{
    // Try opening apt.dat files...first successful wins
    std::ifstream fIn;
    std::string sFileName;
    for (const char* aszPath: {APTDAT_CUSTOM_GLOBAL,APTDAT_RESOURCES_DEFAULT})
    {
        sFileName = LTCalcFullPath(aszPath);
        fIn.open(sFileName.c_str());
        if (fIn.good() && fIn.is_open()) {
            break;
        }

        // doesn't exist or any other problem:
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        LOG_MSG(logWARN, WARN_APTDAT_NOT_OPEN,
                sFileName.c_str(), sErr);
    }
    
    // Not successful in opening ANY apt.dat file?
    if (!fIn || !fIn.is_open()) {
        SHOW_MSG(logWARN, WARN_APTDAT_FAILED);
        return;
    }
    
    // We opened a file and can continue
    LOG_MSG(logDEBUG, "Reading apt.dat from %s", sFileName.c_str());
    
    // Preparation
    // To avoid costly distance calculations we define a bounding box
    // just by calculating lat/lon values north/east/south/west of given pos
    // and include all airports with coordinates falling into it
    boundingBoxTy box (ctr, radius);

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
            if (apt.HasId() && !apt.listRwyEnd.empty())
                AddApt(std::move(apt));
            
            apt = Apt();                    // clear the airport object
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            if (fields.size() >= 5) {
                STRCPY_S(apt.id, fields[4].c_str());
            }
        }
        
        // test for a runway...just to find location info
        else if (apt.HasId() &&             // an airport identified?
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
                    if (!apt.listRwyEnd.empty() ||
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
                        apt.id[0] = '\0';       // by clearing its id
                    }
                }   // if lat/lon in acceptable range
            }       // if line contains 26 field values
        }           // if a runway line startin with "100 "
    }               // for each line of the apt.dat file
    
    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        LOG_MSG(logERR, ERR_CFG_FILE_READ,
                sFileName.c_str(), sErr);
    }
    
    // close the file and exit
    fIn.close();
    
    LOG_MSG(logDEBUG, "Done reading from apt.dat, have now %d airports",
            (int)gvecApt.size());
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
/// Y Probe for terrain altitude computation
static XPLMProbeRef aptYProbe = NULL;
        
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
    std::lock_guard<std::mutex> lock(mtxGVecApt);

    // loop all airports and their runways
    for (Apt& apt: gvecApt) {                       // for all airports
        for (RwyEnd& re: apt.listRwyEnd) {          // for all their rwys
            if (std::isnan(re.pos.alt_m()))         // if altitude is missing
                // determine the rwy end's altitude (this will auto-init the YProbe)
                re.pos.alt_m() = YProbe_at_m(re.pos, aptYProbe);
        }
    }
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
    vecAptTy::const_iterator iterBestApt;
    // ...and this to the best matching runway end
    listRwyEndTy::const_iterator iterBestRwyEnd;
    // The heading diff of the best match to its runway
    // (initialized to the max allowed value so that worse heading diffs aren't considered)
    double bestHeadingDiff = ART_RWY_MAX_HEAD_DIFF;
    // when would we arrive there?
    double bestArrivalTS = NAN;
    
    // --- Iterate the airports ---
    // Access to the list of airports is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGVecApt);

    // loop over airports
    for (vecAptTy::const_iterator iterApt = gvecApt.cbegin();
         iterApt != gvecApt.cend();
         ++iterApt)
    {
        const Apt& apt = *iterApt;
        
        // loop over rwy ends of this airport
        for (listRwyEndTy::const_iterator iterRE = apt.listRwyEnd.cbegin();
             iterRE != apt.listRwyEnd.cend();
             ++iterRE)
        {
            // Test if runway is an option
            const RwyEnd& re = *iterRE;
            // We need to know the runway's altitude
            if (std::isnan(re.pos.alt_m()))
                continue;
            // 1. Heading of rwy as compared to heading of flight
            if (fabs(HeadingDiff(from.heading(), re.pos.heading())) > ART_RWY_MAX_HEAD_DIFF)
                continue;
            // 2. Heading towards rwy, compared to current flight's heading
            const double bearing = from.angle(re.pos);
            const double headingDiff = fabs(HeadingDiff(from.heading(), bearing));
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
            iterBestApt->id, iterBestRwyEnd->id,
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
    if (aptYProbe) {
        XPLMDestroyProbe(aptYProbe);
        aptYProbe = NULL;
    }
}
