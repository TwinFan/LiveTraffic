/// @file       LTFlightData.cpp
/// @brief      LTFlightData represents the tracking data of one aircraft, even before it is drawn
/// @details    Keeps statis and dynamic tracking data.\n
///             Dynamic tracking data is kept as a list.\n
///             Various optimizations and cleansing applied to dynamic data in a separate thread.\n
///             Provides fresh tracking data to LTAircraft upon request.
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2020 Birger Hoppe
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
//MARK: Global
//

// the global map of all received flight data,
// which also includes pointer to the simulated aircraft
mapLTFlightDataTy mapFd;
// modifying the map is controlled by a mutex
// (note that mapFdMutex must be locked before dataAccessMutex
//  to avoid deadlocks, mapFdMutex is considered a higher-level lock)
std::mutex      mapFdMutex;

// flag to indicate that there is no new positional data
// to analyse for terrain altitude and subsequently
// add to posDeque, i.e. if true AppendAllNewPos returns immediately
std::atomic_flag flagNoNewPosToAdd = ATOMIC_FLAG_INIT;

//
//MARK: Flight Data Subclasses
//
LTFlightData::FDDynamicData::FDDynamicData () :
gnd(false),                             // positional
heading(NAN),
spd(0.0), vsi(0.0),                     // movement
ts(0),
pChannel(nullptr)
{}

// formatted Squawk Code
std::string LTFlightData::FDDynamicData::GetSquawk() const
{
    if (radar.code <= 0 || radar.code > 9999)
        return "-";
    else
    {
        char s[10];
        snprintf(s, sizeof(s), "%04ld", radar.code);
        return std::string(s);
    }
}

// Merges data, i.e. copy only filled fields from 'other'
bool LTFlightData::FDStaticData::merge (const FDStaticData& other,
                                        bool bIsMasterChData)
{
    // Have matching-relevant fields changed?
    bool bRet = false;
    
    // copy filled, and only filled data over current data
    // do it field-by-field only for fields which are actually filled
    
    // acTypeICAO
    // We never overwrite with nothing, ie. the new value must be _something_
    if (!other.acTypeIcao.empty() &&
        acTypeIcao != other.acTypeIcao)
    {
        // Accept anything if we are currently empty (unknown/default) or a car, can't be worse...
        // or if this is proper master data channel's data
        if (acTypeIcao.empty() ||
            acTypeIcao == dataRefs.GetDefaultCarIcaoType() ||
            bIsMasterChData)
        {
            acTypeIcao = other.acTypeIcao;
            bRet = true;
        }
        // else: we are non-empty, non-default -> no change, no matter what is delivered,
        //       to avoid ping-ponging the a/c plane when different channels have different opinion
    }
    
    // a/c details
    if (!other.country.empty()) country = other.country;
    if (!other.man.empty()) man = other.man;
    if (other.mdl.length() > mdl.length() ||    // the longer model text wins
        (bIsMasterChData && !other.mdl.empty()))// or what a proper master data channel delivers
    {
        if (mdl != other.mdl) {
            mdl = other.mdl;
            bRet = true;
        }
    }
    if (!other.catDescr.empty()) catDescr = other.catDescr;
    if (other.year) year = other.year;
    if (other.mil) mil = other.mil;     // this only overwrite if 'true'...
    
    // flight
    if (!other.call.empty()) call = other.call;
    if (!other.slug.empty()) slug = other.slug;
    
    // little trick for priority: we trust the info with the longer flight number
    if (other.flight.length() >= flight.length() ||
        // or certainly data of a proper master data channel
        bIsMasterChData ||
        // or no flight number info at all...
        (other.flight.empty() && flight.empty())) {
        if (!other.originAp.empty()) originAp = other.originAp;
        if (!other.destAp.empty()) destAp = other.destAp;
        if (!other.flight.empty()) flight = other.flight;
    }
    
    // operator / Airline
    if (!other.op.empty()) op = other.op;
    // operator ICAO: we only accept a change from nothing to something,
    //                or the data of a proper master data channel
    if ((opIcao.empty() || bIsMasterChData) && !other.opIcao.empty() &&
        opIcao != other.opIcao)
    {
        opIcao = other.opIcao;
        bRet = true;
    }
    
    // registration: we only accept a change from nothing to something,
    //               or the data of a proper master data channel
    if ((reg.empty() || bIsMasterChData) && !other.reg.empty() &&
        reg != other.reg)
    {
        reg = other.reg;
        bRet = true;
    }

    // now initialized from a proper master channel?
    if (bIsMasterChData)
        bFilledFromMasterCh = true;

    // find DOC8643 and fill man/mdl from there if needed
    pDoc8643 = &(Doc8643::get(acTypeIcao));
    LOG_ASSERT(pDoc8643 != NULL);
    if (man.empty())
        man = pDoc8643->manufacturer;
    if (mdl.empty())
        mdl = pDoc8643->model;
    
    // Some string trimming
    trim(reg);
    trim(country);
    trim(man);
    trim(mdl);
    trim(catDescr);
    trim(call);
    trim(originAp);
    trim(destAp);
    trim(flight);
    trim(op);
    trim(opIcao);
    
    return bRet;
}

// route (this is "originAp - destAp", but considers emoty txt)
std::string LTFlightData::FDStaticData::route () const
{
    // keep it an empty string if there is no info at all
    if (originAp.empty() && destAp.empty())
        return std::string();
    
    // if there is some info then replace missing info with question mark
    std::string s(originAp.empty() ? "?" : originAp);
    s += '-';
    s += destAp.empty() ? "?" : destAp;
    return s;
}

// returns flight, call sign, registration, or trans hex code
std::string LTFlightData::FDStaticData::acId (const std::string _default) const
{
    return
    !flight.empty() ?   flight  :
    !call.empty() ?     call    :
    !reg.empty() ?      reg     :
    _default;
}

// route (this is "originAp-destAp", but considers empty txt)
std::string LTFlightData::FDStaticData::flightRoute() const
{
    const std::string r(route());
    // keep it an empty string if there is no info at all
    if (flight.empty() && r.empty())
        return std::string();
    
    // if there is some info missing then just return the other
    if (flight.empty())
        return r;
    if (r.empty())
        return flight;
    
    // we have both...put it together
    return (flight + ": ") + r;
}

// is this a ground vehicle?
bool LTFlightData::FDStaticData::isGrndVehicle() const
{
    return acTypeIcao == dataRefs.GetDefaultCarIcaoType();
}

// set the key value
std::string LTFlightData::FDKeyTy::SetKey (FDKeyType _eType, unsigned long _num)
{
    eKeyType = _eType;
    num = _num;

    // convert to uppercase hex string
    char buf[50] = "";
    switch(_eType) {
        case KEY_ICAO:
        case KEY_FLARM:
        case KEY_FSC:
        case KEY_ADSBEX:
            snprintf(buf, sizeof(buf), "%06lX", _num);
            break;
        case KEY_OGN:
        case KEY_RT:
            snprintf(buf, sizeof(buf), "%08lX", _num);
            break;
        case KEY_UNKNOWN:
            // must not happen
            LOG_ASSERT(eKeyType!=KEY_UNKNOWN);
            break;
    }
    LOG_ASSERT(buf[0]);
    return key = buf;
}

std::string LTFlightData::FDKeyTy::SetKey (FDKeyType _eType, const std::string _key, int base)
{
    return SetKey(_eType, std::stoul(_key, nullptr, base));
}


// matches the key?
bool LTFlightData::FDKeyTy::isMatch (const std::string t) const
{
    return t == key;
}

// return the type of key (as string)
const char* LTFlightData::FDKeyTy::GetKeyTypeText () const
{
    switch (eKeyType) {
        case KEY_UNKNOWN:   return "unknown";
        case KEY_OGN:       return "OGN";
        case KEY_RT:        return "RealTraffic";
        case KEY_FLARM:     return "FLARM";
        case KEY_ICAO:      return "ICAO";
        case KEY_FSC:       return "FSCharter";
        case KEY_ADSBEX:    return "ADSBEx";
    }
    return "unknown";
}


//
//MARK: Flight Data
//

// Export file for tracking data
std::ofstream LTFlightData::fileExport;
std::string LTFlightData::fileExportName;       // current export file's name
double LTFlightData::fileExportTsBase = NAN;    // when normalizing timestamps this is the base
// the priority queue holding data to be exported for sorting
LTFlightData::quExportTy LTFlightData::quExport;
// Coordinates writing into the export file to avoid lines overwriting
std::recursive_mutex LTFlightData::exportFdMutex;

// Constructor
LTFlightData::LTFlightData () :
rcvr(0),sig(0),
rotateTS(NAN),
// created "now"...if no positions are ever added then it will be removed after 2 x outdated interval
youngestTS(dataRefs.GetSimTime() + 2 * dataRefs.GetAcOutdatedIntvl()),
pAc(nullptr), probeRef(NULL),
bValid(true)
{}

// Copy Constructor (needed for emplace into map) doesn't copy mutex
LTFlightData::LTFlightData(const LTFlightData& fd)
{
    // all logic is in the copy assignment operator
    *this = fd;
}

// Destructor makes sure lock is available and aircraft is removed, too
LTFlightData::~LTFlightData()
{
    try {
//        LOG_MSG(logDEBUG, "FD destroyed for %s", key().c_str());
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // make sure aircraft is removed, too
        DestroyAircraft();
        // Release probe handle
        if (probeRef)
            XPLMDestroyProbe(probeRef);
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Copy assignment operator copies all but the mutex
LTFlightData& LTFlightData::operator=(const LTFlightData& fd)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy data
        acKey               = fd.acKey;             // key
        rcvr                = fd.rcvr;
        sig                 = fd.sig;
        labelStat           = fd.labelStat;
        labelCfg            = fd.labelCfg;
        posDeque            = fd.posDeque;          // dynamic data
        posToAdd            = fd.posToAdd;
        dynDataDeque        = fd.dynDataDeque;
        rotateTS            = fd.rotateTS;
        youngestTS          = fd.youngestTS;
        statData            = fd.statData;          // static data
        pAc                 = fd.pAc;
        probeRef            = fd.probeRef;
        bValid              = fd.bValid;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return *this;
}

// set this FD invalid (which will cause it's removal)
void LTFlightData::SetInvalid(bool bAlsoAc)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
    bValid = false;
    // also need to make aircraft invalid so it won't be drawn again
    if (bAlsoAc && pAc)
        pAc->SetInvalid();
}

// Set the object's key, usually right after creation in fdMap
void LTFlightData::SetKey (const FDKeyTy& _key)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
    acKey = _key;
//    LOG_MSG(logDEBUG, "FD crated for %s", key().c_str());
}


/// @details Other channels also seem to catch some FLARM-equipped planes, but there is no
/// way telling if the plane at hand is one of them. To avoid duplicates with
/// Open Glider Network we now search for the same hex code as FLARM, and if we
/// find one (which must be relatively close by as we currently "see" it)
/// then we assume it is the same flight and change key type to FLARM,
/// so that both OGN and RealTraffic feed the flight:
bool LTFlightData::CheckDupKey(LTFlightData::FDKeyTy& _key, LTFlightData::FDKeyType _ty)
{
    LTFlightData::FDKeyTy cpyKey(_ty, _key.num);
    if (mapFd.count(cpyKey) > 0) {
        LOG_MSG(logDEBUG, "Handling same key %s of different types: %s replaced by %s",
                _key.c_str(), _key.GetKeyTypeText(), cpyKey.GetKeyTypeText());
        _key = std::move(cpyKey);
        return true;
    }
    return false;
}



// Search support: icao, registration, call sign, flight number matches?
bool LTFlightData::IsMatch (const std::string t) const
{
    // we can compare key without lock
    if (acKey.isMatch(t))
        return true;
    
    // everything else must be guarded
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // compare with registration, flight number, call sign, squawk code
        if (statData.flight == t    ||
            statData.reg == t       ||
            statData.call == t      ||
            GetUnsafeDyn().GetSquawk() == t)
            return true;
        
        // no match
        return false;
        
        // copy data
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

bool LTFlightData::validForAcCreate(double simTime) const
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // the obvious stuff first: we need basic data
    if ( empty() || dynDataDeque.empty() || posDeque.size() < 2 )
        return false;

    // simTime defaults to 'now'
    if (std::isnan(simTime))
        simTime = dataRefs.GetSimTime();

    // so we have two positions...
    // if it is _not_ one in the past, one in the future, then bail
    if (!(posDeque.front().ts() <= simTime && simTime < posDeque[1].ts()))
        return false;
    
    // So first pos is in the past, second in the future, great...
    // both are within limits in terms of distance?
    if (CoordDistance(dataRefs.GetViewPos(), posDeque.front()) > dataRefs.GetFdStdDistance_m() ||
        CoordDistance(dataRefs.GetViewPos(), posDeque[1]     ) > dataRefs.GetFdStdDistance_m())
        return false;

    // All checks passed
    return true;
}


#define ADD_LABEL(b,txt) if (b && !txt.empty()) { labelStat += txt; labelStat += ' '; }
// update static data parts of the a/c label for reuse for performance reasons
void LTFlightData::UpdateStaticLabel()
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg();
        
        // add parts as per config
        labelStat.clear();
        ADD_LABEL(cfg.bIcaoType,    statData.acTypeIcao);
        ADD_LABEL(cfg.bAnyAcId,     statData.acId(key()));
        ADD_LABEL(cfg.bTranspCode,  key());
        ADD_LABEL(cfg.bReg,         statData.reg);
        ADD_LABEL(cfg.bIcaoOp,      statData.opIcao);
        ADD_LABEL(cfg.bCallSign,    statData.call);
        ADD_LABEL(cfg.bFlightNo,    statData.flight);
        ADD_LABEL(cfg.bRoute,       statData.route());
        
        // this is the config we did the label for
        labelCfg = cfg;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// produce a/c label
#define ADD_LABEL_NUM(b,num) if (b) { label += std::to_string(lround(num)); label += ' '; }
std::string LTFlightData::ComposeLabel() const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg();
        std::string label(labelStat);       // copy static parts
        
        // only possible if we have an aircraft
        if (pAc) {
            // current position of a/c
            const positionTy& pos = pAc->GetPPos();
            // add more items as per configuration
            if (cfg.bPhase) { label +=  pAc->GetFlightPhaseString(); trim(label); label += ' '; }
            ADD_LABEL_NUM(cfg.bHeading,     pos.heading());
            ADD_LABEL_NUM(cfg.bAlt,         pos.alt_ft());
            if (cfg.bHeightAGL) {
                label += pAc->IsOnGrnd() ? positionTy::GrndE2String(GND_ON) :
                           std::to_string(long(pAc->GetPHeight_ft()));
                trim(label);
                label += ' ';
            }
            ADD_LABEL_NUM(cfg.bSpeed,       pAc->GetSpeed_kt());
            ADD_LABEL_NUM(cfg.bVSI,         pAc->GetVSI_ft());
        }
        
        // remove the trailing space
        if (!label.empty())
            label.pop_back();
        
        return label;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return "?";
}


//
//MARK: Flight Data - Mutex-Controlled access Dynamic
//

// Data Cleansing of the buffered positions (called from CalcNextPos)
void LTFlightData::DataCleansing (bool& bChanged)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // nothing to cleanse?
    if (posDeque.empty())
        return;
    
    // The flight model to use
    const LTAircraft::FlightModel& mdl = LTAircraft::FlightModel::FindFlightModel(*this);

    // *** Keep last pos in posDeque above 2.5° ILS path
    // Relevant if:
    // - airborne
    // - descending
    if ((pAc && !posDeque.empty()) || (posDeque.size() >= 2))
    {
        positionTy& last = posDeque.back();
        const positionTy& prev = posDeque.size() >= 2 ? *std::prev(posDeque.cend(),2) : pAc->GetToPos();
        double terrain_alt_m = pAc ? pAc->GetTerrainAlt_m() : NAN;
        if (!last.IsOnGnd() && !prev.IsOnGnd() &&   // too late? ;-) position shall not already be on the ground
            !std::isnan(last.alt_m()) &&            // do we have an altitude at all?
            last.alt_m() <= KEEP_ABOVE_MAX_ALT &&   // not way too high (this skips planes which are just cruising
            (std::isnan(terrain_alt_m) || (last.alt_m() - terrain_alt_m) < KEEP_ABOVE_MAX_AGL) && // pos not too high AGL
            prev.vsi_ft(last) < -mdl.VSI_STABLE)    // sinking considerably
        {
            // Try to find a rwy this plane might be headed for
            // based on the last known position
            posRwy = LTAptFindRwy(mdl, last, prev.speed_m(last), rwyId);
            if (posRwy.isNormal()) {            // found a suitable runway?
                // Now, with this runway, check/correct all previous positions
                for (positionTy& pos: posDeque) {
                    const double dist = DistLatLon(pos.lat(), pos.lon(),
                                                   posRwy.lat(), posRwy.lon());
                    // Are we flying below the 2.5° glidescope? ("- 0.5" to avoid rounding problems)
                    if (pos.alt_m() - posRwy.alt_m() < dist * KEEP_ABOVE_RATIO - 0.5) {
                        // Fix it!
                        const double old_alt_ft = pos.alt_ft();
                        pos.alt_m() = posRwy.alt_m() + dist * KEEP_ABOVE_RATIO;
                        pos.f.onGrnd = GND_OFF;            // we even lift ground positions into the air!
                        bChanged = true;
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG, DBG_KEEP_ABOVE,
                                    old_alt_ft, pos.dbgTxt().c_str());
                        }
                    }
                }
            }
        }
    }
    
    //
    // *** Remove weird positions ***
    //
    // A position is 'weird', if
    // - VSI would be more than +/- 2 * mdl.VSI_INIT_CLIMB
    // - heading change would be more than
    if (( pAc && posDeque.size() >= 1) ||
        (!pAc && posDeque.size() >= 3))
    {
        positionTy pos1;
        double h1 = NAN;
        dequePositionTy::iterator iter = posDeque.begin();
        
        // position _before_ the first position in the deque
        if (pAc) {
            pos1 = pAc->GetToPos(&h1);
            // if (still) the to-Pos is current iter pos then increment
            // (could be that plane's current 'to' is still the first
            //  in out queue)
            while (iter != posDeque.end() &&
                   pos1.cmp(*iter) >= 0)
                ++iter;
        } else {
            // in this case we have at least 3 positions
            pos1 = *std::next(iter);
            vectorTy v1 = iter->between(pos1);
            h1 = v1.dist > SIMILAR_POS_DIST ?
            v1.angle : pos1.heading();
            std::advance(iter, 2);
        }
        
        // loop over (remaining) pos and verify their validity
        while (iter != posDeque.end())
        {
            // is pos not OK compared to previous one?
            const double tempH = h1;
            if (!IsPosOK(pos1, *iter, &h1, &bChanged))
            {
                // remove pos and move on to next one
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_REMOVED_NOK_POS,iter->dbgTxt().c_str());
                iter = posDeque.erase(iter);
                h1 = tempH;
            } // if invalid pos
            else
            {
                // just move on to next position in deque
                // (heading h1 has been updated by IsPosOK to heading from pos1 to iter already
                pos1 = *iter;
                ++iter;
            }
        } // inner while loop over positions
    } // outer if of data cleansing
    
    // *** Hovering-along-the-runway detection ***
    
    // RealTraffic's data sometimes has the issue that after approach
    // a plane does not touch down but instead there is actual tracking
    // data that lets the plane fly along the runway a few dozen feet
    // above ground. Looks like calculated predictive data for
    // case of missing ADS-B data... But prevents LiveTraffic from
    // just using its autoland feature, which would look a lot better.
    // So let's remove that hovering stuff
    
    const LTChannel* pChn = nullptr;
    if (pAc && !posDeque.empty() &&
        FPH_APPROACH <= pAc->GetFlightPhase() &&
        pAc->GetFlightPhase() < FPH_LANDING &&
        GetCurrChannel(pChn) && pChn->DoHoverDetection())
    {
        // We have a plane which is in approach.
        const double maxHoverAlt_m = pAc->GetTerrainAlt_m() + (MAX_HOVER_AGL * M_per_FT);
        
        // What we now search for is data at level altitude following a descend.
        // So we follow our positions as long as they are descending.
        // Then we remove all data which is hovering at level altitude
        // some few dozen feet above ground.
        
        // this increments iter as long as the next pos is descending
        positionTy prevPos = pAc->GetToPos();       // we start comparing with current 'to'-pos of aircraft
        dequePositionTy::const_iterator iter;
        for (iter = posDeque.cbegin();              // start at the beginning
             
             iter != posDeque.cend() &&             // it's not yet the end, AND
             !iter->IsOnGnd() &&                    // not on ground, AND
             iter->vsi_ft(prevPos) < -mdl.VSI_STABLE; // descending considerably
             
             prevPos = *iter++ );                   // increment
        
        // 'prevPos' now is the last pos of the descend and will no longer change
        // 'iter' points to the first pos _after_ descend
        // and is the first deletion candidate.
        // Delete all positions hovering above the runway.
        while (iter != posDeque.cend() &&                         // not the end,
               !iter->IsOnGnd() &&                                // between ground and
               iter->alt_m() < maxHoverAlt_m &&                   // max hover altitude
               std::abs(iter->vsi_ft(prevPos)) <= mdl.VSI_STABLE) // and flying level
        {
            // remove that hovering position
            if (dataRefs.GetDebugAcPos(key())) {
                LOG_MSG(logDEBUG, DBG_HOVER_POS_REMOVED,
                        keyDbg().c_str(),
                        iter->dbgTxt().c_str());
            }
            iter = posDeque.erase(iter);        // erase and returns element thereafter
            bChanged = true;
        }
    }
}

// Smoothing data means:
// We change timestamps(!) of tracking data in order to have
// speed change smoothly.
// This is particularly necessary if position's timestamps aren't
// reliable as speed is a function of
// distance (between positions, which are assumed reliable) and
// time (between timestamps, which in _this_ function are assumed unreliable).
// Introduced with RealTraffic, which doesn't transmit the position's timestamp,
// hence timestamps are unreliable between [ts-10s;ts].
void LTFlightData::DataSmoothing (bool& bChanged)
{
    double gndRange = 0.0;
    double airbRange = 0.0;
    
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // shall we do data smoothing at all?
    const LTChannel* pChn = nullptr;
    if (!GetCurrChannel(pChn) || !pChn->DoDataSmoothing(gndRange,airbRange))
        return;
    
    // find first and last positions for smoothing
    const positionTy& posFirst = posDeque[0];
    const double tsRange = posFirst.IsOnGnd() ? gndRange : airbRange;
    dequePositionTy::iterator itLast = posDeque.begin();
    for (++itLast; itLast != posDeque.end(); ++itLast) {
        // there are various 'stop' conditions
        //  most important: leaving allowed smoothing range (in seconds)
        if (itLast->ts() - posFirst.ts() > tsRange  ||
            // don't smooth across gnd status changes
            itLast->f.onGrnd != posFirst.f.onGrnd       ||
            // don't smooth across artifically calculated positions
            itLast->f.flightPhase != FPH_UNKNOWN)
            break;
    }
    // we went one too far...so how far did we go into the deque?
    --itLast;
    // not far enough for any smoothing?
    if (std::distance(posDeque.begin(), itLast) < 2)
        return;
    
    // what is the total distance travelled between first and last?
    // (to take curves into account we need to sum up individual distances)
    double dist = 0.0;
    dequePositionTy::iterator itPrev = posDeque.begin();        // previous pos
    for (dequePositionTy::iterator it = std::next(itPrev);      // next pos
         itPrev != itLast;
         ++it, ++itPrev)
    {
        dist += itPrev->dist(*it);                              // distance between prev and next
    }
    const double totTime = itLast->ts() - posFirst.ts();
    // sanity check: some reasonable time
    if (totTime < 1.0)
        return;
    // avg speed:
    const double speed = dist / totTime;
    // sanity check: some reasonable speed to avoid INF and NAN values
    if (speed < 1.0)
        return;

    // all positions between first and last are now to be moved in a way
    // that the speed stays constant in all segments
    itPrev = posDeque.begin();
    for (dequePositionTy::iterator it = std::next(itPrev);
         it != itLast;
         ++it, ++itPrev)
    {
        // speed is constant, but distances differs from leg to leg
        // and, thus, determines time difference:
        it->ts() = itPrev->ts() + itPrev->dist(*it) / speed;
    }
    
    // If previously there where two (or more) positions with the exact same
    // position but different timestamps then these positions now have the very
    // same timestamp. (Distance between them is 0, with the above calculation
    // time difference now is also 0.) We must remove these duplicates:
    dequePositionTy::iterator dup;
    while ((dup = std::adjacent_find(posDeque.begin(), posDeque.end(),
                                     // find two adjacent positions with same timestamp:
                                     [](const positionTy& a, const positionTy& b){return dequal(a.ts(),b.ts());})) != posDeque.end())
    {
        posDeque.erase(dup);
    }
    
    // so we changed data
    bChanged = true;
}

// shift ground positions to taxiways, insert positions at taxiway nodes
void LTFlightData::SnapToTaxiways (bool& bChanged)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // Not enabled at all? (Or no positions at all?)
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0 ||
        posDeque.empty())
        return;
    
    // Loop over position in the deque
    dequePositionTy::iterator iter = posDeque.begin();
    while (iter != posDeque.end())
    {
        // Only act on positions on the ground,
        // which have (not yet) been artificially added
        positionTy& pos = *iter;
        if (pos.IsOnGnd() && !pos.IsPostProcessed())
        {
            // Try snapping to a rwy or taxiway
            if (LTAptSnap(*this, iter, true))
                bChanged = true;
        } // non-artificial ground position
        
        // move on to next
        ++iter;
    } // while all posDeque positions
}


// based on buffered positions calculate the next position to fly to
// (usually called in a separate thread via TriggerCalcNewPos,
//  with 'simTime' slightly [~0.5s] into the future,
//  called by LTAircraft shortly before running out of positions and
//  calling TryFetchNewPos)
//
// simTime should only be set when called from LTAircraft,
// others should pass in NAN.
bool LTFlightData::CalcNextPos ( double simTime )
{
    bool bChanged = false;          // change any positions?
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // *** maintenance of flight data deque ***
        const LTAircraft::FlightModel& mdl = LTAircraft::FlightModel::FindFlightModel(*this);

        // if no simTime given use a/c's 'to' position, or current sim time
        if (std::isnan(simTime)) {
            if (pAc)
                simTime = pAc->GetToPos().ts();
            else
                simTime = dataRefs.GetSimTime();
        }

        // remove from front until [0] <= simTime < [1] (or just one element left)
        while (dynDataDeque.size() >= 2 && dynDataDeque[1].ts <= simTime)
            dynDataDeque.pop_front();
        
        // *** Maintenance of positions queue ***
        
        // *** Data Smoothing ***
        // (potentially changes timestamp, so needs to be befure
        //  maintenance, which relies on timestamps)
        DataSmoothing(bChanged);
        
        // *** maintenance of buffered positions ***
        
        // Differs depending on: is there an a/c yet?
        if ( pAc ) {
            // if there is an a/c then we just remove all positions before 'simTime'
            while (!posDeque.empty() && posDeque.front().ts() <= simTime + 0.05f) {
                posDeque.pop_front();
                bChanged = true;
            }
            
            // no positions left?
            if (posDeque.empty()) {
                // If descending: Try finding a runway to land on
                if (pAc->GetVSI_ft() < -pAc->pMdl->VSI_STABLE)
                {
                    const positionTy& acTo = pAc->GetToPos();
                    posRwy = LTAptFindRwy(*pAc, rwyId, dataRefs.GetDebugAcPos(key()));
                    if (posRwy.isNormal()) {
                        // found a landing spot!
                        // If it is 'far' away in terms of time then we don't add it
                        // directly...maybe the channel wakes up and gives us
                        // real data that we don't want to miss.
                        // So we add only a part of the way to the rwy.
                        vectorTy vecRwy = acTo.between(posRwy);

                        // At most we travel as far as a refresh interval takes us
                        // and never too close to the touch-down point...
                        // we need room for the alignment point with the runway
                        const double d_ts = posRwy.ts() - simTime;
                        if (d_ts > (double)dataRefs.GetFdRefreshIntvl() &&
                            vecRwy.dist > 3 * ART_RWY_ALIGN_DIST)
                        {
                            // shorten the distance so it only takes as long as a refresh interval
                            vecRwy.dist *= (double)dataRefs.GetFdRefreshIntvl() / d_ts;
                            positionTy posInterm = acTo + vecRwy;
                            posInterm.f.flightPhase = FPH_APPROACH;
                            // Add the it to the queue
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added intermediate %s",
                                        keyDbg().c_str(),
                                        std::string(posInterm).c_str());
                            posDeque.emplace_back(std::move(posInterm));
                        } else {
                            // The final leg down onto the runway.
                            // Little trick here: We add 2 stops to make sure
                            // that latest shortly before touching down we
                            // are fully aligned with the runway.
                            // Rwy heading is given in posRwy.heading().
                            positionTy posBefore =
                            posRwy + vectorTy(std::fmod(posRwy.heading() + 180.0, 360.0),   // angle (reversed!)
                                              ART_RWY_ALIGN_DIST,                           // distance
                                              -vecRwy.vsi,                                  // VSI (reversed!)
                                              std::min(vecRwy.speed,                        // speed (capped at max final speed)
                                                       pAc->pMdl->FLAPS_DOWN_SPEED * ART_FINAL_SPEED_F / KT_per_M_per_S));
                            // Timestamp is now beyond posRwy.ts() as time always moves forward,
                            // but posBefore is _before_ posRwy:
                            posBefore.ts() -= 2 * (posBefore.ts() - posRwy.ts());
                            posBefore.pitch() = 0.0;
                            posBefore.f.onGrnd = GND_OFF;
                            posBefore.f.flightPhase = FPH_FINAL;
                            
                            // Add both position to the queue
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added final %s",
                                        keyDbg().c_str(),
                                        std::string(posBefore).c_str());
                            posDeque.emplace_back(std::move(posBefore));
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added touch-down %s",
                                        keyDbg().c_str(),
                                        std::string(posRwy).c_str());
                            posDeque.push_back(posRwy);     // make a copy, we want to keep posRwy!
                        }
                        bChanged = true;
                    }
                }
                // No more positions on the ground: Make the a/c stop
                // by adding the last known position just once again as artifical stop.
                else if (pAc->IsOnGrnd()) {
                    positionTy stopPos = pAc->GetToPos();
                    if (stopPos.IsOnGnd() &&
                        stopPos.f.flightPhase != FPH_TOUCH_DOWN &&      // don't copy touch down pos, that looks ugly, and hinders auto-land/stop
                        stopPos.f.flightPhase != FPH_STOPPED_ON_RWY &&  // avoid adding several stops
                        stopPos.ts() <= simTime + 3.0)                  // and time's running out for the plane's to-position
                    {
                        stopPos.ts() += 5.0;                            // just set some time after to-position
                        stopPos.f.flightPhase = FPH_STOPPED_ON_RWY;     // indicator for aritifical stop (not only on rwy now...)
                        if (dataRefs.GetDebugAcPos(key()))
                            LOG_MSG(logDEBUG, "%s: Added stop-position %s",
                                    keyDbg().c_str(),
                                    std::string(stopPos).c_str());
                        posDeque.emplace_back(std::move(stopPos));      // add it to the deque
                        bChanged = true;
                    }
                }

                // still no positions left?
                if (posDeque.empty())
                {
                    if (dataRefs.GetDebugAcPos(key()))
                        LOG_MSG(logDEBUG,DBG_NO_MORE_POS_DATA,Positions2String().c_str());
                    return false;
                }
                else {
                    // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
                    LOG_ASSERT_FD(*this,
                                  std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                                     [](const positionTy& a, const positionTy& b)
                                                     {return a > b;}
                                                     ) == posDeque.cend());
                }
            }
        } else {
            // If there is no a/c yet then we need one past and
            // one or more future positions
            // If already the first pos is in the future then we aren't valid yet
            if (posDeque.size() < 2 || simTime < posDeque.front().ts())
                return false;
        }
        
        // *** Data Cleansing ***
        DataCleansing(bChanged);
        
        // *** Snap to taxiways ***
        SnapToTaxiways(bChanged);

#ifdef DEBUG
        std::string deb0   ( !posDeque.empty() ? posDeque.front().dbgTxt() : "<none>" );
        std::string deb1   ( posDeque.size() >= 2 ? std::string(posDeque[1].dbgTxt()) : "<none>" );
        std::string debvec ( posDeque.size() >= 2 ? std::string(posDeque.front().between(posDeque[1])) : "<none>" );
#endif
        
        // *** Landing / Take-Off Detection ***
        
        if ( pAc && !posDeque.empty() ) {
            // clear outdated rotate timestamp
            if (!std::isnan(rotateTS) && (rotateTS + 10 * mdl.ROTATE_TIME < simTime) )
                rotateTS = NAN;
            
            // *** Landing ***
            
            // If current pos is in the air and next pos is approaching or touching ground
            // then we have live positional data on the ground.
            // However, if we would fly directly to next pos then we would touch down
            // then at next pos only (earliest), which might be a point far down the runway.
            // To simulate touching down at the _beginning_ of the runway and
            // then rolling out to (or through) next pos we determine this case
            // and then insert an artifical touch down position, which just keeps going with
            // previous vsi and speed down to the ground.
            const positionTy& toPos_ac = pAc->GetToPos();   // a/c's current to-position
            positionTy& next = posDeque.front();            // next pos waiting in posDeque

            if (!toPos_ac.IsOnGnd() &&                      // currently not heading for ground
                next.IsOnGnd() &&                           // future: on ground
                pAc->GetVSI_ft() < -mdl.VSI_STABLE) {       // right now descending considerably
                // Case determined: We are landing and have live positional
                //                  data down the runway
                const double descendAlt      = toPos_ac.alt_m() - next.alt_m(); // height to sink
                const double timeToTouchDown = descendAlt / -pAc->GetVSI_m_s(); // time to sink
                const double tsOfTouchDown   = toPos_ac.ts() + timeToTouchDown; // when to touch down
                // but only reasonably a _new_ position if between to pos and next
                // with some minima distance
                if (timeToTouchDown > TIME_REQU_POS &&
                    tsOfTouchDown + TIME_REQU_POS < next.ts())
                {
                    vectorTy vecTouch(pAc->GetTrack(),                          // touch down is straight ahead, don't turn last second
                                      timeToTouchDown * pAc->GetSpeed_m_s(),     // distance
                                      pAc->GetVSI_m_s(),                         // vsi
                                      pAc->GetSpeed_m_s());                      // speed
                    
                    // insert touch-down point at beginning of posDeque
                    positionTy& touchDownPos = posDeque.emplace_front(toPos_ac.destPos(vecTouch));
                    touchDownPos.f.onGrnd = GND_ON;
                    touchDownPos.f.flightPhase = FPH_TOUCH_DOWN;
                    touchDownPos.alt_m() = NAN;          // will set correct terrain altitude during TryFetchNewPos
                    
                    // Snap the touch down pos to the rwy:
                    dequePositionTy::iterator iter = posDeque.begin();
                    LTAptSnap(*this, iter, false);
                    
                    // output debug info on request
                    if (dataRefs.GetDebugAcPos(key())) {
                        LOG_MSG(logDEBUG,DBG_INVENTED_TD_POS,touchDownPos.dbgTxt().c_str());
                    }
                    
                    // If the touch-down point snapped to a rwy AND
                    // the next position in the deque is a TAXI position (and not also a RWY)
                    // then snap the TXI position again so that the (shortest)
                    // path from touch-down to taxi pos is inserted along
                    // proper taxi routes
                    if (iter->f.specialPos == SPOS_RWY &&
                        std::next(iter) != posDeque.end() &&
                        std::next(iter)->f.specialPos == SPOS_TAXI)
                    {
                        dequePositionTy::iterator txiIter = std::next(iter);
                        LTAptSnap(*this, txiIter, true);
                    }
                }
                else
                {
                    // not enough distance to 'next', so we declare 'next' the landing spot
                    next.f.flightPhase = FPH_TOUCH_DOWN;
                }
                    
                // Remove positions down the runway until the last RWY position
                // That allows for a better deceleration simulation.
                while (posDeque.size() > 2 &&       // keep at least two positions
                       posDeque[1].IsOnGnd() &&
                       posDeque[1].f.specialPos == SPOS_RWY &&
                       posDeque[2].f.specialPos == SPOS_RWY)
                {
                    // remove the second element (first is the just inserted touch-down pos)
                    posDeque.erase(std::next(posDeque.begin()));
                }
                
                // do Data Cleansing again, just to be sure the new
                // position does not screw up our flight path
                DataCleansing(bChanged);
                bChanged = true;
            } // (landing case)
            
            // *** Take Off ***
            
            // Similar issue as with landing, just reverse:
            // A/c is to take off (rotate and leave the runway) somewhere
            // between an on-ground position and an off-ground position.
            // We need to stick to the ground till that point. Where is that point?
            // If we'd just follow the known flight data then we likely
            // 'climb' with 100-200 ft/min from the start of the runway (A)
            // slowly to some point down the runway and only slightly in the air (B)
            // and then only change to initial climbing rate to a point
            // higher up in the air (C).
            // What we want is to stick to the ground, accelerate and leave
            // shortly before (B) at the right angle to extend climbing right
            // away to (C). (B)-(C) is a path really flown while (A)-(B)
            // is not 'flown' directly, but first there's the take-off-roll
            // followed by rotating and taking off.
            // So we extend the vector (B)-(C) _backwards_ to find the
            // take-off-point, achieved using vsi of (B)-(C).
            // Point of rotate is 3s earlier.
            
            // for the take off case we look further ahead to catch the case
            // that a data point is right between start of rotating and actual lift off
            else for (size_t i = 0;
                      std::isnan(rotateTS) && posDeque.size() >= i+1;
                      i++ )
            {
                // i == 0 is as above with actual a/c present position
                // in later runs we use future data from our queue
                const positionTy& ppos_i  = i == 0 ? pAc->GetPPos() : posDeque[i-1];
                positionTy& to_i          = posDeque[i];
                const double to_i_ts      = to_i.ts();  // the reference might become invalid later once we start erasing, so we copy this timestamp that we need
                
                // we look up to 60s into the future
                if (ppos_i.ts() > simTime + MDL_TO_LOOK_AHEAD)
                    break;
                
                // Now: is there a change from on-ground to off-ground?
                if (ppos_i.IsOnGnd() && !to_i.IsOnGnd())
                {
                    // direct vector from (A)-(B), in which the take off happens:
                    const vectorTy vec (ppos_i.between(to_i));
                    
                    // Get the vsi and speed after 'to' (the (B)-(C) vector's)
                    double climbVsi = mdl.VSI_INIT_CLIMB * Ms_per_FTm;
                    double climbSpeed = mdl.SPEED_INIT_CLIMB / KT_per_M_per_S;
                    if (posDeque.size() >= i+2) {     // take the data from the vector _after_ to
                        vectorTy climbVec (to_i.between(posDeque[i+1]));
                        if (climbVec.vsi > mdl.VSI_STABLE) {                // make sure it's really a climb!
                            climbVsi = climbVec.vsi;
                            climbSpeed = climbVec.speed;
                        }
                    }
                    
                    // Determine how much before 'to' is that take-off point
                    // We assume ppos_i, which is ON_GND, has good terrain alt
                    const double toTerrAlt = ppos_i.alt_m();
                    const double height_m = to_i.alt_m() - toTerrAlt; // height to climb to reach 'to'?
                    const double toClimb_s = height_m / climbVsi;   // how long to climb to reach 'to'?
                    const double takeOffTS = to_i.ts() - toClimb_s;   // timestamp at which to start the climb, i.e. take off
                    
                    // Continue only for timestamps in the future,
                    // i.e. if take off is calculated to be after currently analyzed position
                    if (ppos_i.ts() + SIMILAR_TS_INTVL < takeOffTS)
                    {
                        rotateTS = takeOffTS - mdl.ROTATE_TIME/2.0; // timestamp when to rotate

                        // find the TO position by applying a reverse vector to the pointer _after_ take off
                        vectorTy vecTO(fmod(vec.angle + 180, 360),  // angle (reverse!)
                                       climbSpeed * toClimb_s,      // distance
                                       -climbVsi,                   // vsi (reverse!)
                                       climbSpeed);                 // speed
                        // insert take-off point ('to' minus vector from take-off to 'to')
                        // at beginning of posDeque
                        positionTy takeOffPos = to_i.destPos(vecTO);
                        takeOffPos.f.onGrnd = GND_ON;
                        takeOffPos.f.flightPhase = FPH_LIFT_OFF;
                        takeOffPos.alt_m() = NAN;                   // TryFetchNewPos will calc terrain altitude
                        takeOffPos.heading() = vec.angle;           // from 'reverse' back to forward
                        takeOffPos.ts() = takeOffTS;                // ts was computed forward...we need it backward
                        
                        // find insert position, remove on-runway positions along the way
                        bool bDelRwyPos = false;
                        dequePositionTy::iterator toIter = posDeque.end();
                        for (dequePositionTy::iterator iter = posDeque.begin();
                             iter != posDeque.end();
                             )
                        {
                            // before take off...
                            if (*iter < takeOffPos) {
                                // Keep the first RWY position, but remove any later RWY positions,
                                // Which allows the accelerate algorithm to accelerate all the distance to take-off point
                                if (iter->f.specialPos == SPOS_RWY) {
                                    if (bDelRwyPos) {
                                        iter = posDeque.erase(iter);
                                        continue;               // start over loop with next element after the erased one
                                    }
                                    bDelRwyPos = true;          // any further RWY positions can be deleted
                                }
                                
                                // before take off we stay on the ground
                                if (!iter->IsOnGnd()) {
                                    iter->f.onGrnd = GND_ON;
                                    iter->alt_m() = NAN;            // TryFetchNewPos will calc terrain altitude
                                }
                            } else {
                                // found insert position! Insert and snap it to the rwy
                                toIter = posDeque.insert(iter, takeOffPos);
                                break;
                            }
                            
                            ++iter;
                        }
                        
                        // found no insert position??? need to add it to the end
                        if (toIter == posDeque.end())
                            posDeque.push_back(takeOffPos);
                        else
                        {
                            // we did find an insert position
                            // we now also remove everything between this
                            // inserted take-off position and the first
                            // in-flight position, which is to_i.
                            // (This can remove ppos_i!)
                            dequePositionTy::iterator rmIter = posDeque.begin();
                            // but runs only until first in-flight position (to_i, we saved its timestamp)
                            while (rmIter != posDeque.end() && rmIter->ts() < to_i_ts)
                            {
                                // skip positions before and including take off pos
                                if (rmIter->ts() <= takeOffTS + 0.001)
                                    rmIter++;
                                else
                                    // a position after take off but before in-flight is to be removed
                                    rmIter = posDeque.erase(rmIter);
                            }
                        }
                        
                        // output debug info on request
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG,DBG_INVENTED_TO_POS,takeOffPos.dbgTxt().c_str());
                        }
                        bChanged = true;
                        
                        // do Data Cleansing again, just to be sure the new
                        // position does not screw up our flight path
                        DataCleansing(bChanged);

                        // leave loop of szenarios
                        break;
                    }
                    // take off would start before ppos_i, we don't do that,
                    // so ppos_i is going to be lift-off
                    else {
                        // if ppos_i is still in the deque we can change it:
                        if (i > 0)
                            posDeque[i-1].f.flightPhase = FPH_LIFT_OFF;
                        rotateTS = ppos_i.ts() - mdl.ROTATE_TIME/2.0;
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG,DBG_REUSING_TO_POS,ppos_i.dbgTxt().c_str());
                        }
                    }
                } // (take off case)
            } // loop over szenarios

            // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
            LOG_ASSERT_FD(*this,
                          std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                             [](const positionTy& a, const positionTy& b)
                                             {return a > b;}
                                             ) == posDeque.cend());
        } // (has a/c and do landing / take-off detection)
        
        // *** Snap any newly inserted positions to taxiways ***
        if (bChanged)
            SnapToTaxiways(bChanged);
        
        // A lot might have changed now, even added.
        // If there is no aircraft yet then we need to "normalize"
        // to creation conditions: One pos in the past, the next in the future
        if ( !pAc )
        {
            // The first pos is in the past, good, make sure it's the only one
            // [0] <= simTime < [1]
            while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime) {
                posDeque.pop_front();
                bChanged = true;
            }
            
            // Unlikely, but theoretically there could now be just one (past) pos left
            if (posDeque.size() < 2)
                return false;
        }
        
        // if something changed
        if (bChanged) {
            // recalc all headings
            for (dequePositionTy::iterator iter = posDeque.begin();
                 iter != posDeque.end();
                 ++iter)
                CalcHeading(iter);
            
            // output all positional information as debug info on request
            if (dataRefs.GetDebugAcPos(key())) {
                LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
            }
        }
        
        // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
        LOG_ASSERT_FD(*this,
                      std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                         [](const positionTy& a, const positionTy& b)
                                         {return a > b;}
                                         ) == posDeque.cend());
        
        // success
        return true;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

// the mutex used to synch access to the list of keys which await pos calculation
std::mutex calcNextPosListMutex;
// and that list of pairs <key,simTime>
typedef std::pair<LTFlightData::FDKeyTy,double> keyTimePairTy;
typedef std::deque<keyTimePairTy> dequeKeyTimeTy;
dequeKeyTimeTy dequeKeyPosCalc;

// The main function for the position calculation thread
// It receives keys to work on in the dequeKeyPosCalc list and calls
// the CalcNextPos function on the respective flight data objects
void LTFlightData::CalcNextPosMain ()
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_CalcPos");

    // loop till said to stop
    while ( !bFDMainStop ) {
        keyTimePairTy pair;
        
        // thread-safely access the list of keys to fetch one for processing
        try {
            std::lock_guard<std::mutex> lock (calcNextPosListMutex);
            if ( !dequeKeyPosCalc.empty() ) {   // something's in the list, take it
                pair = dequeKeyPosCalc.front();
                dequeKeyPosCalc.pop_front();
            }
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "CalcNextPosMain", e.what());
            pair = keyTimePairTy();
        }
        
        // there was something in the list to process? Do so!
        if (!pair.first.empty()) {
            try {
                // To ensure a FD object stays available between mapFd.at and the
                // call to its local mutex we prohibit removal by locking the
                // general mapFd mutex.
                std::unique_lock<std::mutex> lockMap (mapFdMutex);
                // find the flight data object in the map and calc position
                LTFlightData& fd = mapFd.at(pair.first);
                
                // LiveTraffic Top Level Exception Handling:
                // CalcNextPos can cause exceptions. If so make fd object invalid and ignore it
                try {
                    std::lock_guard<std::recursive_mutex> lockFD (fd.dataAccessMutex);
                    lockMap.unlock();           // now that we have the detailed mutex we can release the global one
                    if (fd.IsValid())
                        fd.CalcNextPos(pair.second);
                } catch (const std::exception& e) {
                    LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION " - on aircraft %s", e.what(), pair.first.c_str());
                    fd.SetInvalid();
                } catch (...) {
                    fd.SetInvalid();
                }
                
            } catch(const std::out_of_range&) {
                // just ignore exception...fd object might have gone in the meantime
                if constexpr (LIVETRAFFIC_VERSION_BETA) {
                    LOG_MSG(logWARN, "No longer found aircraft %s", pair.first.c_str());
                }
            }
        }
            
        // sleep till woken up for processing or stopping
        {
            std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
            FDThreadSynchCV.wait(lk, []{return bFDMainStop || !dequeKeyPosCalc.empty();});
            lk.unlock();
        }
    }
}

// Add a new key to the list of positions to calculate
// and wake up the calculation thread
void LTFlightData::TriggerCalcNewPos ( double simTime )
{
    // thread-safely add the key to the list and start the calc thread
    try {
        std::lock_guard<std::mutex> lock (calcNextPosListMutex);
        
        // search for key in the list, if already included update simTime and return
        for (keyTimePairTy &i: dequeKeyPosCalc)
            if(i.first==key()) {
                i.second = fmax(simTime,i.second);   // update simTime to latest
                return;
            }
        
        // not in list, so add to list of keys to calculate including simTime
        dequeKeyPosCalc.emplace_back(key(),simTime);
        
        // trigger the calc thread to wake up
        FDThreadSynchCV.notify_all();
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "TriggerCalcNewPos", e.what());
    }
}


// calc heading from positions in a positionList around a given position (it)
// if there is only (it), then use heading from flight data
// if there are 2 positions then return heading from first to second
// if there are 3 positions, then calc both headings (from 1st to 2nd, from 2nd to 3rd)
//                           and return its average
//         ====>        <- this is the return value if 3 positions exist
//         P_it                p1   -> p_it =  30°
//          ^ \                p_it -> p3   = 150°
//         /   _|              return       = (30° + 150°) / 2 = 90°
//        /     P3
//       P1
//
// (Thinking behind it: By the time we reach P2 we should be flying a curve
//  and should be at heading as returned -- before changing course to P3)
//
// Also consider the distance between points: a distance less than 5m is
// considered "no point" for the purpose of this calculation.
// Such short distances will be seen when a/c stops on the gound:
// Very similar positions next to each other, which we don't want the entire
// a/c to turn to. Instead, favor the longer vector, or, if none of the
// vectors is long enough, fall back to the heading as reported in the
// flight data.
void LTFlightData::CalcHeading (dequePositionTy::iterator it)
{
    // skip any fiddling with the heading in case it is fixed
    if (it->f.bHeadFixed)
        return;
    
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // vectors to / from the position at "it"
    vectorTy vecTo, vecFrom;
    
    // is there a predecessor to "it"?
    if (it != posDeque.cbegin()) {
        const positionTy& prePos = *std::prev(it);
        vecTo = prePos.between(*it);
        if (vecTo.dist < SIMILAR_POS_DIST)      // distance from predecessor to it too short
        {
            it->heading() = prePos.heading();   // by default don't change heading for this short distance to avoid turning planes "on the spot"
            if (!std::isnan(it->heading()))     // if we now have a heading -> just use it
                return;
            vecTo = vectorTy();                 // clear the vector
        }
    } else if (pAc) {
        // no predecessor in the queue...but there is an a/c, take that
        const positionTy& prePos = pAc->GetToPos();
        vecTo = prePos.between(*it);
        if (vecTo.dist < SIMILAR_POS_DIST)      // distance from predecessor to it too short
        {
            it->heading() = prePos.heading();   // by default don't change heading for this short distance to avoid turning planes "on the spot"
            if (!std::isnan(it->heading()))     // if we now have a heading -> just use it
                return;
            vecTo = vectorTy();                 // clear the vector
        }
    }
    
    // is there a successor to it?
    if (std::next(it) != posDeque.cend()) {
        vecFrom = it->between(*std::next(it));
        if (vecFrom.dist < SIMILAR_POS_DIST)    // clear the vector if too short
            vecFrom = vectorTy();
    }
    
    // if both vectors are available return the average between both angles
    if (!std::isnan(vecTo.angle) && !std::isnan(vecFrom.angle))
        // with the linear factor in favor of the _shorter_ vector
        // (Idea: we have more time to turn on the longer side, so at the junction
        //        the heading should be closer to the shorter vector's heading)
        it->heading() = HeadingAvg(vecTo.angle,
                                   vecFrom.angle,
                                   vecFrom.dist,
                                   vecTo.dist);
    // if just one vector is available take that one
    else if (!std::isnan(vecFrom.angle))
        it->heading() = vecFrom.angle;
    else if (!std::isnan(vecTo.angle))
        it->heading() = vecTo.angle;
    // no calculated vector available...if the position came with some heading leave it untouched
    // else we fall back to the heading delivered by the flight data
    else if (std::isnan(it->heading())) {
        FDDynamicData *pBefore = nullptr, *pAfter = nullptr;
        dequeFDDynFindAdjacentTS(it->ts(), pBefore, pAfter);
        // get the best heading out of it
        if (pAfter && pBefore)
            it->heading() = HeadingAvg(pBefore->heading,
                                       pAfter->heading,
                                       pAfter->ts - it->ts(),   // factor "before" higher if close (and after is further away!)
                                       it->ts() - pBefore->ts); // factor "after" higher if close (and before is further away!)
        else if (pBefore)
            it->heading() = pBefore->heading;
        else if (pAfter)
            it->heading() = pAfter->heading;
        else
            it->heading() = 0;
    }
    
    // just as a safeguard...they can't be many situations this triggers,
    // but we don't want nan values any longer after this
    if (std::isnan(it->heading()))
        it->heading() = 0;
}

// check if thisPos would be OK after lastPos,
// pHeading: if given overrides lastPos.Heading()
//           if NAN, then no check for heading
bool LTFlightData::IsPosOK (const positionTy& lastPos,
                            const positionTy& thisPos,
                            double* pHeading,
                            bool* /*pbChanged*/)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
    
    // only compare positions which are either both on the ground or both in the air
    if (thisPos.IsOnGnd() != lastPos.IsOnGnd())
        return true;

    // aircraft model to use
    const std::string* pIcaoType = nullptr;
    const LTAircraft::FlightModel& mdl = LTAircraft::FlightModel::FindFlightModel(*this, false, &pIcaoType);
    if (!pIcaoType)     // if we can't really determine a model we can't really validate
        return true;
    
    // if pHeading not given we assume we can take it from lastPos
    const double lastHead = pHeading ? *pHeading : lastPos.heading();
    // vector from last to this
    const vectorTy v = lastPos.between(thisPos);
    if (pHeading) *pHeading = v.angle;      // return heading from lastPos to thisPos
    // maximum turn allowed depends on 'on ground' or not
    const double maxTurn = mdl.maxHeadChange(thisPos.IsOnGnd(), thisPos.ts() - lastPos.ts());

    // angle between last and this, i.e. turn angle at thisPos
    const double hDiff = (std::isnan(lastHead) ? 0.0 :
                          lastPos.f.bHeadFixed || thisPos.f.bHeadFixed ? 0.0 :
                          v.dist <= SIMILAR_POS_DIST ? 0.0 :
                          HeadingDiff(lastHead, v.angle));
    
    // Speed limits
    const double minSpeed = thisPos.IsOnGnd() ? 0.0                          : mdl.MIN_FLIGHT_SPEED;
    const double maxSpeed = thisPos.IsOnGnd() ? (mdl.SPEED_INIT_CLIMB * 1.2) : mdl.MAX_FLIGHT_SPEED;
    
    // --- Validations ---
    const char* szViolTxt = nullptr;
    
    if (-maxTurn > hDiff || hDiff > maxTurn)
        szViolTxt = "Turn too far";
    else if (v.vsi_ft() < -mdl.VSI_MAX || mdl.VSI_MAX < v.vsi_ft())
        szViolTxt = "VSI too high";
    else if (!std::isnan(v.speed_kn()) && v.speed_kn() > maxSpeed)
        szViolTxt = "Speed too high";
    else if (!std::isnan(v.speed_kn()) && v.speed_kn() < minSpeed)
        szViolTxt = "Speed too low";
        
    // Any problem found?
    if (szViolTxt) {
        LOG_MSG(logDEBUG, "%s: %s: %s with headingDiff = %.0f (speed = %.f - %.fkn, max turn = %.f, max vsi = %.fft/min, mdl %s, type %s)",
                keyDbg().c_str(), szViolTxt,
                std::string(v).c_str(), hDiff,
                minSpeed, maxSpeed, maxTurn, mdl.VSI_MAX,
                mdl.modelName.c_str(), pIcaoType->c_str());
        return false;
    }
    
    // all OK
    return true;
}

// Static: Open/Close the tracking data export file as needed
bool LTFlightData::ExportOpenClose ()
{
    // no logging? return (after closing the file if open)
    if (!dataRefs.AnyExportData()) {
        if (fileExport.is_open()) {
            std::lock_guard<std::recursive_mutex> lock(exportFdMutex);
            // write remaining lines before close
            while (!quExport.empty()) {
                fileExport << quExport.top().s;
                quExport.pop();
            }
            fileExport.close();
            SHOW_MSG(logWARN, DBG_EXPORT_FD_STOP, fileExportName.c_str());
        }
        return false;
    }
    // Logging on: Need to open the file first?
    else if (!fileExport.is_open()) {
        std::lock_guard<std::recursive_mutex> lock(exportFdMutex);
        // previous test was unsafe, not locked, so with lock once again:
        if (!fileExport.is_open()) {
            // Create the file name from a fixed part and a date/time stamp
            // much like X-Plane names screenshots
            char currFileName[100];
            const std::time_t t = std::time(nullptr);
            const std::tm tm = *std::localtime(&t);
            std::strftime(currFileName, sizeof(currFileName),
                          PATH_DEBUG_EXPORT_FD, &tm);
            fileExportName = currFileName;
            
            // open the file, append to it
            fileExport.open (fileExportName, std::ios_base::out | std::ios_base::app);
            if (!fileExport) {
                char sErr[SERR_LEN];
                strerror_s(sErr, sizeof(sErr), errno);
                // could not open output file: bail out, decativate logging
                SHOW_MSG(logERR, DBG_RAW_FD_ERR_OPEN_OUT,
                         fileExportName.c_str(), sErr);
                dataRefs.SetAllExportData(false);
                return false;
            }
            else {
                SHOW_MSG(logWARN, DBG_EXPORT_FD_START, fileExportName.c_str());
                // In case we are to normalize timestamps we'll do it against NOW
                fileExportTsBase = dataRefs.ShallExportNormalizeTS() ? dataRefs.GetSimTime() : NAN;
                // always start with current weather
                ExportLastWeather();
            }
        }
    }
    return fileExport.is_open();
}

// Moves a line to the export priority queue, flushes data which is ready to be written
void LTFlightData::ExportAddOutput (unsigned long ts, const char* s)
{
    // make sure a file is open before continuing
    if (!ExportOpenClose())
        return;
    
    // As there are different threads (e.g. in LTRealTraffic), which send data,
    // we guard file writing with a lock, so that no line gets intermingled
    // with another thread's data:
    std::lock_guard<std::recursive_mutex> lock(exportFdMutex);

    // add the line to the queue
    quExport.emplace(ts, s);
    
    // flush all lines to the file that are due for writing
    const unsigned long now = (unsigned long)(dataRefs.GetSimTime()) + 5;
    while (!quExport.empty() && quExport.top().ts < now) {
        fileExport << quExport.top().s;
        quExport.pop();
    }
    fileExport.flush();
}

// debug: log raw network data to a log file
void LTFlightData::ExportFD(const FDDynamicData& inDyn,
                            const positionTy& pos)
{
    // We are to log tracking data?
    if (!ExportOpenClose() ||
        !dataRefs.GetDebugExportFD())
        return;
    
    // output a tracking data record
    char buf[1024];
    switch (dataRefs.GetDebugExportFormat()) {
        case EXP_FD_AITFC:
            snprintf(buf, sizeof(buf),
                     "AITFC,%lu,%.6f,%.6f,%.0f,%.0f,%c,%.0f,%.0f,%s,%s,%s,%s,%s,%.0f\n",
                     key().num,                                                 // hexid
                     pos.lat(), pos.lon(),                                      // lat, lon
                     nanToZero(dataRefs.WeatherPressureAlt_ft(pos.alt_ft())),   // alt
                     inDyn.vsi,                                                 // vs
                     (pos.IsOnGnd() ? '0' : '1'),                               // airborne
                     inDyn.heading, inDyn.spd,                                  // hdg,spd
                     statData.call.c_str(),                                     // cs
                     statData.acTypeIcao.c_str(),                               // type
                     statData.reg.c_str(),                                      // tail
                     statData.originAp.c_str(),                                 // from
                     statData.destAp.c_str(),                                   // to
                     pos.ts() - nanToZero(fileExportTsBase));                   // timestamp: if requested normalize timestamp in output
            break;
            
        case EXP_FD_RTTFC:
            snprintf(buf, sizeof(buf),
                     "RTTFC,%lu,%.6f,%.6f,%.0f,%.0f,%c,%.0f,%.0f,%s,%s,%s,%s,%s,%.0f,"
                     "%s,%s,%s,%.0f,"
                     "-1,-1,-1,-1,-1,-1,"                                       // IAS, TAS, Mach, track_rate, roll, mag_heading
                     "%.2f,%.0f,%s,%s,"
                     "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"                        // nav_qnh, nav_altitude_mcp, nav_altitude_fms, nav_heading, nav_modes, seen, rssi, winddir, windspd, OAT, TAT
                     "%c,,\n",
                     // equivalent to AITFC
                     key().num,                                                 // hexid
                     pos.lat(), pos.lon(),                                      // lat, lon
                     nanToZero(dataRefs.WeatherPressureAlt_ft(pos.alt_ft())),   // baro_alt
                     inDyn.vsi,                                                 // baro_rate
                     (pos.IsOnGnd() ? '1' : '0'),                               // gnd
                     inDyn.heading, inDyn.spd,                                  // track, gsp
                     statData.call.c_str(),                                     // cs_icao
                     statData.acTypeIcao.c_str(),                               // ac_type
                     statData.reg.c_str(),                                      // ac_tailno
                     statData.originAp.c_str(),                                 // from_iata
                     statData.destAp.c_str(),                                   // to_iata
                     pos.ts() - nanToZero(fileExportTsBase),                    // timestamp: if requested normalize timestamp in output
                     // additions by RTTFC
                     inDyn.pChannel ? inDyn.pChannel->ChName() : "LT",          // source
                     statData.call.c_str(),                                     // cs_iata (copy of cs_icao)
                     "lt_export",                                               // msg_type
                     nanToZero(pos.alt_ft()),                                   // alt_geom
                     // -- here follows a set of 6 fields we can't fill, they are set constant already in the format string, see above
                     pos.heading(),                                             // true_heading
                     inDyn.vsi,                                                 // geom_rate
                     "none",                                                    // emergency
                     statData.isGrndVehicle() ? "C2" : "",                      // category
                     // -- here follows a set of 11 fields we can't fill, they are set constant already in the format string, see above
                     key().eKeyType == KEY_ICAO ? '1' : '0');                   // isICAOhex
            break;
    }
    ExportAddOutput((unsigned long)std::lround(pos.ts()), buf);
}

// Export Weather data record, based on DataRefs::GetWeather()
void LTFlightData::ExportLastWeather ()
{
    // The file is expected to be open if we are actively exporting, see ExportFD
    // We are to log tracking data? And export file is open?
    if (!ExportOpenClose())
        return;

    // make sure no other thread is writing to the file right now
    std::lock_guard<std::recursive_mutex> lock(exportFdMutex);

    // get latest data
    float hPa = NAN;
    std::string stationId, METAR;
    dataRefs.GetWeather(hPa, stationId, METAR);
    
    fileExport
    << "{\"ICAO\": \""      << stationId
    << "\",\"QNH\": \""     << std::lround(hPa)
    << "\", \"METAR\": \""  << METAR
    << "\", \"NAME\": \""   << stationId        // don't have a proper name, doesn't matter
    << "\"}\n";
}

// adds a new position to the queue of positions to analyse
void LTFlightData::AddNewPos ( positionTy& pos )
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // We only consider data that is newer than what we have already
        const positionTy* pLatestPos =
        !posToAdd.empty() ? &(posToAdd.back()) :
        !posDeque.empty() ? &(posDeque.back()) :
        hasAc()           ? &(pAc->GetToPos()) : nullptr;
        
        if (pLatestPos) {
            // pos is before or close to 'to'-position: don't add!
            if (pos.ts() <= pLatestPos->ts() + SIMILAR_TS_INTVL)
            {
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS_TS,pos.dbgTxt().c_str());
                return;
            }
        }

        // add pos to the queue of data to be added
        // (we shall not do Y probes but need accurate GND info...)
        posToAdd.emplace_back(pos);
        flagNoNewPosToAdd.clear();

        if (dataRefs.GetDebugAcPos(key()))
            LOG_MSG(logDEBUG,DBG_ADDED_NEW_POS,pos.dbgTxt().c_str());
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// walks all flight data objects and works the posToAdd queue
// called from flight loop callback, i.e. from the main thread
void LTFlightData::AppendAllNewPos()
{
    // short-cut if nothing to do
    if (flagNoNewPosToAdd.test_and_set())
        return;

    // somewhere there is something to do
    // need access to flight data map
    try {
        std::unique_lock<std::mutex> lock (mapFdMutex, std::try_to_lock);
        if (!lock) {
            // couldn't get the lock right away
            // -> return, we don't want to hinder rendering
            flagNoNewPosToAdd.clear();      // but need to try again
            return;
        }
        
        // loop all flight data objects and check for new data to analyse
        for (mapLTFlightDataTy::value_type& fdPair: mapFd) {
            LTFlightData& fd = fdPair.second;
            try {
                std::unique_lock<std::recursive_mutex> lockFD (fd.dataAccessMutex, std::try_to_lock);
                if (!lockFD) {
                    flagNoNewPosToAdd.clear();          // need to try it again
                } else {
                    if (fd.IsValid())
                        fd.AppendNewPos();
                }
            } catch (const std::exception& e) {
                LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
                fd.SetInvalid();
            } catch (...) {
                fd.SetInvalid();
            }
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFdMutex", e.what());
        flagNoNewPosToAdd.clear();
    }
}

// analyse and add new positional data
// called from AppendAllNewPos, i.e. from within flight loop callback
void LTFlightData::AppendNewPos()
{
    // short-cut if nothing to do...we dare doing that without lock
    if (posToAdd.empty())
        return;
    
    try {
        // access guarded by a mutex, but we don't wait (inside the flight loop)
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if (!lock) {
            flagNoNewPosToAdd.clear();          // need to try it again
            return;
        }
        
       // loop the positions to add
        while (!posToAdd.empty())
        {
            // take next pos from queue
            positionTy pos = posToAdd.front();
            posToAdd.pop_front();
            
            // Once again a final check: We only add data after the last known position
            // We only consider data that is newer than what we have already
            const positionTy* pLatestPos = nullptr;
            double headToLatest = NAN;          // heading/track when flying to that latest pos, relevant for turn validation
            
            // There are positions waiting in the deque
            if (!posDeque.empty())
            {
                pLatestPos = &(posDeque.back());
                if (posDeque.size() >= 2)
                    headToLatest = posDeque[posDeque.size()-2].angle(*pLatestPos);
                else if (hasAc())
                    headToLatest = pAc->GetToPos().angle(*pLatestPos);
            }
            // no deque positions, but an aircraft?
            else if (hasAc())
            {
                pLatestPos = &(pAc->GetToPos());
                headToLatest = pAc->GetTrack();
            }

            // Is the new position _after_ the latest known position?
            if (pLatestPos &&
                pos.ts() <= pLatestPos->ts() + SIMILAR_TS_INTVL)
            {
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS_TS,pos.dbgTxt().c_str());
                continue;                   // skip
            }
            
            // ground status: will set ground altitude if on ground
            TryDeriveGrndStatus(pos);
            
            // Now that we have a proper Grnd status we can test the pos for validty
            if (pLatestPos && !IsPosOK(*pLatestPos, pos, &headToLatest)) {
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS_NOK,pos.dbgTxt().c_str());
                return;
            }
            
            // *** pitch ***
            // just a rough value, LTAircraft::CalcPPos takes care of the details
            if (pos.IsOnGnd())
                pos.pitch() = 0;
            else
                pos.pitch() = 2;
            
            // *** roll ***
            // LTAircraft::CalcPPos takes care of the details
            pos.roll() = 0;
            
            // add to the end of the deque
            posDeque.emplace_back(pos);
            dequePositionTy::iterator i = std::prev(posDeque.end());

            // *** heading ***
            
            // Recalc heading of adjacent positions: before p, p itself, and after p
            if (i != posDeque.begin())              // is there anything before i?
                CalcHeading(std::prev(i));
            CalcHeading(i);                         // i itself, latest here a nan heading is rectified
            
            // *** last checks ***
            
            // should be fully valid position now
            LOG_ASSERT_FD(*this, i->isFullyValid());
        }
        
        // posDeque should be sorted, i.e. no two adjacent positions a,b should be a > b
        if constexpr (LIVETRAFFIC_VERSION_BETA) {
            LOG_ASSERT_FD(*this,
                          std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                             [](const positionTy& a, const positionTy& b)
                                             {return a > b;}
                                             ) == posDeque.cend());
        }
        
        // now the youngest timestamp is this one of the last known position:
        if (!posDeque.empty()) {
            youngestTS = posDeque.back().ts();
        
            // *** trigger recalc ***
            TriggerCalcNewPos(NAN);
        }
        
        // print all positional information as debug info on request
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Called by a/c: reads available positions if lock available
LTFlightData::tryResult LTFlightData::TryFetchNewPos (dequePositionTy& acPosList,
                                                      double& _rotateTS)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( !lock )                            // didn't get the lock -> return
            return TRY_NO_LOCK;
        
        // we are called from X-Plane's main thread,
        // so we take our chance to determine proper terrain altitudes
        for (positionTy& pos: posDeque) {
            if ((pos.IsOnGnd() && std::isnan(pos.alt_m())) ||    // GND_ON but alt unknown
                pos.f.onGrnd == GND_UNKNOWN) {    // GND_UNKNOWN
                TryDeriveGrndStatus(pos);
            }
        }
        
        // the very first call (i.e. FD doesn't even know the a/c's ptr yet)?
        if (!pAc) {
            // there must be two positions, one in the past, one in the future!
            LOG_ASSERT_FD(*this, validForAcCreate());
            // move the first two positions to the a/c, so that the a/c can start flying from/to
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
        } else {
            // there is an a/c...only use stuff past current 'to'-pos
            const positionTy& to = pAc->GetToPos();
            LOG_ASSERT_FD(*this, !std::isnan(to.ts()));
            
            // Remove outdated positions from posDeque,
            // ie. all positions before 'to'
            while (!posDeque.empty() && posDeque.front() < to)
                posDeque.pop_front();
            
            // nothing left???
            if (posDeque.empty())
                return TRY_NO_DATA;
            
            // move that next position to the a/c
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
            
            // Was that position one that is _not_ to be reached because the corner is to be cut?
            // In that case we also need the _next_ position to properly calculate the required Bezier curve:
            if (acPosList.back().f.bCutCorner && !posDeque.empty()) {
                acPosList.emplace_back(std::move(posDeque.front()));
                posDeque.pop_front();
            }
        }
        
        // store rotate timestamp if there is one (never overwrite with NAN!)
        if (!std::isnan(rotateTS))
            _rotateTS = rotateTS;
        
        // output all positional information as debug info on request
        if (dataRefs.GetDebugAcPos(key()))
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        
        // return success as something has been added
        return TRY_SUCCESS;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Caught some error
    return TRY_TECH_ERROR;
}


// determine ground-status based on comparing altitude to terrain
// Note: If pos.onGnd == GND_ON then this will not change, but the altitude will be set to terrain altitude
//       If pos.onGnd != GND_ON then onGnd will be decided based on comparing altitude to terrain altitude
bool LTFlightData::TryDeriveGrndStatus (positionTy& pos)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // what's the terrain altitude at that pos?
            double terrainAlt = YProbe_at_m(pos);
            if (std::isnan(terrainAlt))
                return false;
            
            // Now 2 options:
            // If position already says itself: I'm on the ground, then keep it like that
            // Otherwise decide based on altitude _if_ it's on the ground
            if (!pos.IsOnGnd() &&
                // say it's on the ground if below terrain+10m (or 20m in case of RealTraffic)
                pos.alt_m() < terrainAlt + (GetCurrChannel() == DR_CHANNEL_REAL_TRAFFIC_ONLINE ? FD_GND_AGL_EXT : FD_GND_AGL))
                pos.f.onGrnd = GND_ON;

            // if it was or now is on the ground correct the altitue to terrain altitude
            // (very slightly below to be sure to actually touch down even after rounding effects)
            if (pos.IsOnGnd())
                pos.alt_m() = terrainAlt - MDL_CLOSE_TO_GND;
            else
                // make sure it's either GND_ON or GND_OFF, nothing lese
                pos.f.onGrnd = GND_OFF;

            // successfully determined a status
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// determine terrain alt at pos
double LTFlightData::YProbe_at_m (const positionTy& pos)
{
    return ::YProbe_at_m(pos, probeRef);
}

// returns vector at timestamp (which has speed, direction and the like)
LTFlightData::tryResult LTFlightData::TryGetNextPos (double ts, positionTy& pos) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // find first posititon _after_ ts
            dequePositionTy::const_iterator i =
            std::find_if(posDeque.cbegin(),posDeque.cend(),
                         [ts](const positionTy& p){return p.ts() > ts;});
            
            // no positions found -> no data!
            if (i == posDeque.cend())
                return TRY_NO_DATA;
            
            // return the position
            pos = *i;
            return TRY_SUCCESS;
        }
        else
            return TRY_NO_LOCK;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return TRY_TECH_ERROR;
}

// stringify all position information - mainly for debugging purposes
std::string LTFlightData::Positions2String () const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // 0. current sim time
        time_t t = time_t(dataRefs.GetSimTime());
        struct tm tm;
        gmtime_s(&tm, &t);

        char szBuf[50];
        snprintf(szBuf,sizeof(szBuf),
                 "a/c %s %s SimTime: %.1f - ",
                 key().c_str(),
                 statData.acId("-").c_str(),
                 dataRefs.GetSimTime());
        std::string ret(szBuf);

        strftime(szBuf,
                 sizeof(szBuf) - 1,
                 "%Y-%m-%d %H:%M:%S",       // %F %T
                 &tm);
        ret += szBuf;
        ret += '\n';
        
        // 1. the data actually used by the a/c
        if(pAc) {
            ret += *pAc;
        } else {
            ret += "pAc == <null>\n";
        }
        
        // 2. flight data buffer with cleansed data
        ret += "posDeque:\n";
        ret += positionDeque2String(posDeque);
        
        // 3. buffer of new data to add as read from original source
        ret += "posToAdd:\n";
        ret += positionDeque2String(posToAdd);
        
        return ret;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // if we get here something's wrong
    return std::string();
}

// add dynamic data (if new one is more up-to-date)
void LTFlightData::AddDynData (const FDDynamicData& inDyn,
                               int _rcvr, int _sig,
                               positionTy* pos)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // We don't mix channels. They aren't in synch, mixing them leads
        // to planes jumping around and other weird behaviour.
        // We allow a change of channel only if the current channel seems
        // outdated and unresponsive.
        // We allow a change of channel if this prevents the aircraft from
        // disappearing, i.e. if this is the last update before a/c outdated period,
        // or in other words: new ts + refresh period > old ts + outdated period
        if (!dynDataDeque.empty()) {
            const FDDynamicData& last = dynDataDeque.back();
            if (inDyn.pChannel && last.pChannel != inDyn.pChannel)
            {
                // Firstly, if no a/c yet created, then we prioritize with
                // which channel an a/c is created. The higher the channel
                // number the better.
                if (!hasAc() &&
                    last.pChannel &&
                    inDyn.pChannel->GetChannel() <= last.pChannel->GetChannel())
                    // lower prio -> ignore data
                    return;
                
                // If there is an aircraft then we only switch if there are
                // no positions waiting any longer, ie. we run into danger to run out of positions
                if (hasAc() &&
                    !posDeque.empty() &&
                    // new position must be significantly _after_ current 'to' pos
                    // so that current channel _really_ had its chance to sent an update:
                    inDyn.ts > pAc->GetToPos().ts() + dataRefs.GetFdRefreshIntvl()*3/2)
                    // there still is data waiting -> ignore other channel's data
                    return;

                // We accept the channel switch...clear out any old channel's data
                // so we throw away the lower prio channel's data
                const LTChannel* pLstChn = last.pChannel;           // last is going to become invalid, save the ptr for the log message
                dynDataDeque.clear();
                posDeque.clear();
                LOG_MSG(logDEBUG, DBG_AC_CHANNEL_SWITCH,
                        keyDbg().c_str(),
                        pLstChn ? pLstChn->ChName() : "<null>",
                        inDyn.pChannel->ChName());
            }
        }
        
        // only need to bother adding data if it is newer than current data
        if (dynDataDeque.empty() || dynDataDeque.front() < inDyn)
        {
            // must not yet have similar timestamp in our list
            if (std::find_if(dynDataDeque.cbegin(),dynDataDeque.cend(),
                             [&inDyn](const FDDynamicData& i){return inDyn.similarTo(i);}) == dynDataDeque.cend())
            {
                // add to list and keep sorted
                dynDataDeque.emplace_back(inDyn);
                std::sort(dynDataDeque.begin(),dynDataDeque.end());
            }
            
            // either way: we 'like' this receiver
            rcvr = _rcvr;
            sig = _sig;
        }
            
        // also export and store the pos (lock is held recursively)
        if (pos) {
            ExportFD(inDyn, *pos);
            AddNewPos(*pos);
        }
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( FDDynamicData& outDyn ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outDyn = dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDDynamicData LTFlightData::WaitForSafeCopyDyn (bool bFirst) const
{
    LTFlightData::FDDynamicData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the the data under lock protection
        if (!dynDataDeque.empty())
            ret = bFirst ? dynDataDeque.front() : dynDataDeque.back();
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

LTFlightData::FDDynamicData LTFlightData::GetUnsafeDyn() const
{
    return dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
}

// returns false if there is no dynamic data
// returns true and set chn to the current channel if there is dynamic data
bool LTFlightData::GetCurrChannel (const LTChannel* &pChn) const
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
    if (dynDataDeque.empty()) {
        pChn = nullptr;
        return false;
    }

    pChn = dynDataDeque.front().pChannel;
    return pChn != nullptr;
}

// Current channel's id
dataRefsLT LTFlightData::GetCurrChannel () const
{
    const LTChannel* pChn = nullptr;
    if (GetCurrChannel(pChn) && pChn)
        return pChn->GetChannel();
    else
        return DR_AC_KEY;           // == 0
}


// find two positions around given timestamp ts
// pBefore and pAfter can come back NULL!
// if pbSimilar is not NULL then function also checks for 'similar' pos
// if a 'similar' pos (ts within SIMILAR_TS_INTVL) is found then
// *pbSimilar is set to true and pBefore points to that one.
// Calling function must own lock to ensure pointers remain valid
void LTFlightData::dequeFDDynFindAdjacentTS (double ts,
                                             LTFlightData::FDDynamicData*& pBefore,
                                             LTFlightData::FDDynamicData*& pAfter,
                                             bool* pbSimilar)
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // init
    pBefore = pAfter = nullptr;
    if (pbSimilar)
        *pbSimilar = false;
    
    // loop
    for (FDDynamicData& d: dynDataDeque) {
        
        // test for similarity
        if (pbSimilar) {
            if (std::abs(d.ts-ts) < SIMILAR_TS_INTVL) {
                *pbSimilar = true;
                pBefore = &d;
                return;
            }
        }
        
        // test for range before/after
        if (d.ts <= ts)
            pBefore = &d;           // while less than timestamp keep pBefore updated
        else {
            pAfter = &d;            // now found (first) data greater then ts
            return;                 // short-cut...ts in dynDataDeque would only further increase
        }
    }
}


//
//MARK: Flight Data - Mutex-Controlled access Static
//

// update static data
void LTFlightData::UpdateData (const LTFlightData::FDStaticData& inStat,
                               double distance,
                               bool bIsMasterChData)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // decide if we need more master data to be fetched by
        // master data channels. If statData is not initialized
        // from a master channel yet, or
        // if the callSign changes (which includes if it changes from empty to something)
        // as the callSign is the source for route information
        if ((!bIsMasterChData && !statData.hasMasterChData()) ||
            (!inStat.call.empty() && inStat.call != statData.call))
        {
            LTACMasterdataChannel::RequestMasterData (key(), inStat.call, distance);
        }
        
        // If no a/c type is yet known try if the call sign / operator looks like a ground vehicle
        bool bMdlInfoChange = false;
        if (statData.acTypeIcao.empty() && inStat.acTypeIcao.empty())
        {
            // Try operator first
            if (!inStat.op.empty()) {
                std::string op_u = inStat.op;
                str_toupper(op_u);
                if (op_u.find("AIRPORT") != std::string::npos) {
                    statData.op = inStat.op;
                    statData.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
                    LOG_MSG(logINFO, INFO_GND_VEHICLE_APT, key().c_str(), inStat.op.c_str());
                    bMdlInfoChange = true;
                }
            }
            
            // Try callsign next
            if (statData.acTypeIcao.empty() &&
                !inStat.call.empty() && inStat.call != statData.call &&
                LTAircraft::FlightModel::MatchesCar(inStat.call))
            {
                statData.call = inStat.call;
                statData.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
                LOG_MSG(logINFO, INFO_GND_VEHICLE_CALL, key().c_str(), inStat.call.c_str());
                bMdlInfoChange = true;
            }
        }
        
        // merge inStat into our statData and save if matching-relevant stuff changed
        if (statData.merge(inStat, bIsMasterChData))
            bMdlInfoChange = true;
        
        // Re-determine a/c model (only if it was determined before:
        // the very first determination shall be made as late as possible
        // in LTFlightData::CreateAircraft())
        if (pAc && DetermineAcModel())
            bMdlInfoChange = true;

        // Need to find a new model-match next time we need it
        if (bMdlInfoChange)
            pMdl = nullptr;
        
        if (pAc) {
            // if model-defining fields changed then (potentially) change the CSL model
            if (bMdlInfoChange)
                pAc->SetUpdateModel();
            // Make Aircraft send updated info texts
            pAc->SetSendNewInfoData();
        }
        
        // update the static parts of the label
        UpdateStaticLabel();
        
   } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( LTFlightData::FDStaticData& outStat ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outStat = statData;
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDStaticData LTFlightData::WaitForSafeCopyStat() const
{
    LTFlightData::FDStaticData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the data under lock protection
        ret = statData;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

//
//MARK: Flight Data - Aircraft Maintenance
//      (call from flight loop as possibly XPMP/XPLM calls are invoked)
//

// checks if initial position to be calculated or aircraft to be created
// returns if a/c is to be deleted
bool LTFlightData::AircraftMaintenance ( double simTime )
{
    try {
        // try to lock data access
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( !lock )                // we didn't get the lock, just return w/o deletion
            return false;
        
        // do we need to recalc the static part of the a/c label due to config change?
        if (dataRefs.GetLabelCfg() != labelCfg)
            UpdateStaticLabel();
        
        // general re-init necessary?
        if (dataRefs.IsReInitAll()) {
            SetInvalid(false);
            return true;
        }

        // Tests on an existing aircraft object
        if (hasAc())
        {
            // if the a/c became invalid or has flown out of sight
            // then remove the aircraft object,
            // but retain the remaining flight data
            if (!pAc->IsValid() ||
                pAc->GetVecView().dist > dataRefs.GetFdStdDistance_m())
                DestroyAircraft();
            else {
                // cover the special case of finishing landing and roll-out without live positions
                // i.e. during approach and landing we don't destroy the aircraft
                //      if it is approaching some runway
                //      until it finally stopped on the runway
                if ((pAc->GetFlightPhase() >= FPH_LANDING ||
                        (pAc->GetFlightPhase() >= FPH_APPROACH && posRwy.isNormal())) &&
                    pAc->GetFlightPhase() < FPH_STOPPED_ON_RWY)
                {
                    return false;
                }
            }
        }
        // Tests when not (yet) having an aircraft object
        else {
            // Remove position from the beginning for as long as there is past data,
            // i.e.: Only the .front pos may be in the past
            while (posDeque.size() >= 2 && posDeque[1].ts() < simTime)
                posDeque.pop_front();

            // Have at least two positions?
            if (posDeque.size() >= 2 ) {
                // is already valid for a/c creation?
                if ( validForAcCreate(simTime) )
                    // then do create the aircraft
                    CreateAircraft(simTime);
                else // not yet valid
                    // but the oldest position is at or before current simTime?
                    // then chances are good that we can calculate positions
                    if ( posDeque.front().ts() <= simTime)
                        // start thread for position calculation...next time we might be valid for creation
                        TriggerCalcNewPos(NAN);
            }
        }
            
        // youngestTS longer ago than allowed? -> remove the entire FD object
        if (youngestTS + dataRefs.GetAcOutdatedIntvl() <
            (std::isnan(simTime) ? dataRefs.GetSimTime() : simTime))
        {
            SetInvalid(false);
            return true;
        }

        // don't delete me
        return false;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    } catch(...) {
    }
    
    // in case of error return 'delete me'
    SetInvalid();
    return true;
}


// try interpreting model text or check for ground vehicle
bool LTFlightData::DetermineAcModel()
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    const std::string prevType = statData.acTypeIcao;

    // Debugging model matching: If the model is fixed, then it is what it is
    if (!dataRefs.cslFixAcIcaoType.empty()) {
        statData.acTypeIcao = dataRefs.cslFixAcIcaoType;
        return statData.acTypeIcao != prevType;
    }
    
    // We don't change the a/c type if it is already something reasonable
    if (!prevType.empty() &&
        prevType != dataRefs.GetDefaultCarIcaoType())
        return false;
    
    // Try finding a CSL model by interpreting the human-readable model text
    statData.acTypeIcao = ModelIcaoType::getIcaoType(statData.mdl);
    if ( !statData.acTypeIcao.empty() )
    {
        // yea, found something by mdl!
        if (prevType != statData.acTypeIcao) {
            LOG_MSG(logINFO,ERR_NO_AC_TYPE_BUT_MDL,
                    key().c_str(),
                    statData.man.c_str(), statData.mdl.c_str(),
                    statData.acTypeIcao.c_str());
            return true;
        }
        return false;
    }
    
    // Ground vehicle maybe? Shall be on the ground then with reasonable speed
    // (The info if this _could_ be a car is delivered by the channels via
    //  the acTypeIcao, here we just validate if the dynamic situation
    //  fits a car.)
    if (prevType == dataRefs.GetDefaultCarIcaoType())
    {
        if ((pAc &&                                 // plane exists?
             pAc->IsOnGrnd() &&                     // must be on ground with reasonable speed
             pAc->GetSpeed_kt() <= MDL_CAR_MAX_TAXI) ||
            (!pAc &&                                // no plane yet:
             posDeque.size() >= 2 &&                // analyse ground status of and speed between first two positions
             posDeque.front().IsOnGnd() && posDeque[1].IsOnGnd() &&
             posDeque.front().speed_kt(posDeque[1]) <= MDL_CAR_MAX_TAXI))
        {
            // We now decide for surface vehicle
            statData.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
            return statData.acTypeIcao != prevType;
        }
    }
            
    // we have no better idea than default
    statData.acTypeIcao.clear();
    return prevType != statData.acTypeIcao;
}

// checks if there is a slot available to create this a/c, tries to remove the farest a/c if too many a/c rendered
/// @warning Caller must own `mapFdMutex`!
bool LTFlightData::AcSlotAvailable ()
{
    // time we had shown the "Too many a/c" warning last:
    static float tTooManyAcMsgShown = 0.0;

    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // Have no positions? (Need one to determine distance to camera)
    if (posDeque.empty())
        return false;
    
    // If we have too many aircraft show message (at most every 5 minutes)
    if (dataRefs.GetNumAc() >= dataRefs.GetMaxNumAc()) {
        if (CheckEverySoOften(tTooManyAcMsgShown, 300.0f))
            SHOW_MSG(logWARN,MSG_TOO_MANY_AC,dataRefs.GetMaxNumAc());
    }

    // As long as there are too many a/c remove the ones farest away
    while (dataRefs.GetNumAc() >= dataRefs.GetMaxNumAc())
    {
        // Now we need to see if we are closer to the camera than other a/c.
        // If so remove the farest a/c to make room for us.
        LTFlightData* pFarestAc = nullptr;

        // NOTE: We can loop mapFd without lock only because we assume that
        //       calling function owns mapFdMutex already!
        // find the farest a/c...if it is further away than us:
        double farestDist = CoordDistance(dataRefs.GetViewPos(), posDeque.front());
        for (mapLTFlightDataTy::value_type& p: mapFd)
        {
            LTFlightData& fd = p.second;
            if (fd.hasAc() && fd.pAc->GetVecView().dist > farestDist) {
                farestDist = fd.pAc->GetVecView().dist;
                pFarestAc = &fd;
            }
        }
    
        // If we didn't find an active a/c farther away than us then bail
        if (!pFarestAc)
            return false;
    
        // We found the a/c farest away...remove it to make room for us!
        pFarestAc->DestroyAircraft();
    }
    
    // There is a slot now - either there was already or we made room
    return true;
}



// create (at most one) aircraft from this flight data
bool LTFlightData::CreateAircraft ( double simTime )
{
    // short-cut if exists already
    if ( hasAc() ) return true;
    
    // exit if too many a/c shown and this one wouldn't be one of the nearest ones
    if (!AcSlotAvailable())
        return false;
    
    try {
        // get the  mutex, not so much for protection,
        // but to speed up creation (which read-accesses lots of data and
        // thus makes many calls to the lock, which are now just quick recursive calls)
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // make sure positional data is up-to-date
        // (also does a last validation...and now with lock, so that state is secured)
        if ( !CalcNextPos(simTime) )
            return false;
        
        // This can have change data in the posDeque...let's see if we are still valid for a/c create
        // Remove outdated positions from posDeque, ie. all positions before simTime
        while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime)
            posDeque.pop_front();
        if ( !validForAcCreate(simTime) )
            return false;
        
        // There are yet unsolved errors where the subsequent aircraft creation failes with an
        // empty posDeque, though we just - while holding the dataAccessMutex - have verified
        // that we are valid for creation. See Issue #174.
        // Next time we see that bug we want to know what NOW is in posDeque:
        const std::string sPosDeque = positionDeque2String(posDeque);
        
        // Make sure we have a valid a/c model now
        DetermineAcModel();
        if (statData.acTypeIcao.empty()) {          // we don't...
            LOG_MSG(logWARN,ERR_NO_AC_TYPE,
                    key().c_str(),
                    statData.man.c_str(), statData.mdl.c_str(),
                    dataRefs.GetDefaultAcIcaoType().c_str());
        }

        // create the object (constructor will recursively re-access the lock)
        try {
            pAc = new LTAircraft(*this);
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_EXCEPTION_AC_CREATE,
                    key().c_str(), statData.acTypeIcao.c_str(),
                    e.what(), sPosDeque.c_str());
            pAc = nullptr;
        }
        catch(...) {
            LOG_MSG(logERR, ERR_UNKN_EXCP_AC_CREATE,
                    key().c_str(), statData.acTypeIcao.c_str(),
                    "<?>", sPosDeque.c_str());
            pAc = nullptr;
        }
        if (!pAc)
        {
            LOG_MSG(logERR,ERR_NEW_OBJECT,key().c_str());
            return false;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // success
    return true;
}

// remove the linked aircraft
void LTFlightData::DestroyAircraft ()
{
    // access guarded by a mutex
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
    if ( pAc )
        delete pAc;
    pAc = nullptr;
}



// static function to
// update the CSL model of all aircraft (e.g. after loading new CSL models)
void LTFlightData::UpdateAllModels ()
{
    try {
        // access guarded by the fd mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        
        // iterate all flight data
        for ( mapLTFlightDataTy::value_type& fdPair: mapFd )
        {
            // if there is an aircraft update it's flight model
            LTAircraft* pAc = fdPair.second.GetAircraft();
            if (pAc)
                pAc->SetUpdateModel();
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
}

// finds the closest a/c roughly in the given direction ('focus a/c')
const LTFlightData* LTFlightData::FindFocusAc (const double bearing)
{
    constexpr double maxDiff = 20;
    const LTFlightData* ret = nullptr;
    double bestRating = std::numeric_limits<double>::max();
    
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
    // walk the map of flight data
    for ( std::pair<const LTFlightData::FDKeyTy,LTFlightData>& fdPair: mapFd )
    {
        // no a/c? -> not relevant
        if (!fdPair.second.pAc)
            continue;
        
        // should be +/- 45° of bearing
        const vectorTy vecView = fdPair.second.pAc->GetVecView();
        double hDiff = std::abs(HeadingDiff(bearing, vecView.angle));
        if (hDiff > maxDiff)
            continue;
        
        // calculate a rating based on deviation from bearing plus distance
        // Reasoning: An a/c directly in front of us shall be prefered if
        //            it is less than twice as far away as an a/c 45° to the side.
        double rating = (1 + hDiff / maxDiff) * vecView.dist;
        
        // best one so far?
        if ( rating < bestRating ) {
            bestRating = rating;
            ret = &fdPair.second;
        }
    }
    
    // return what we thing is focus
    return ret;
}

#ifdef DEBUG
// This helps focusing on one aircraft and debug through the position calculation code
void LTFlightData::RemoveAllAcButSelected ()
{
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
    
    // hard and directly remove all other aircraft without any further ado
    for (mapLTFlightDataTy::iterator i = mapFd.begin();
         i != mapFd.end();)
    {
        if (!i->second.bIsSelected)
            i = mapFd.erase(i);
        else
            ++i;
    }
    
    // reduce allow a/c to 1 so no new aircraft gets created
    dataRefs.SetMaxNumAc(1);
}
#endif


//
// MARK: mapLTFlightDataTy
//

// Find "i-th" aircraft, i.e. the i-th flight data with assigned pAc
mapLTFlightDataTy::iterator mapFdAcByIdx (int idx)
{
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
    // let's find the i-th aircraft by looping over all flight data
    // and count those objects, which have an a/c
    int i = 0;
    for (mapLTFlightDataTy::iterator fdIter = mapFd.begin();
         fdIter != mapFd.end();
         ++fdIter)
    {
        if (fdIter->second.hasAc())         // has an a/c
            if ( ++i == idx )               // and it's the i-th!
                return fdIter;
    }
    
    // not found
    return mapFd.end();
}

// Find a/c by text input
mapLTFlightDataTy::iterator mapFdSearchAc (const std::string& _s)
{
    // is it a small integer number, i.e. used as index?
    if (_s.length() <= 3 &&
        _s.find_first_not_of("0123456789") == std::string::npos)
    {
        return mapFdAcByIdx(std::stoi(_s));
    }
    else
    {
        // search the map of flight data by text key
        return std::find_if(mapFd.begin(), mapFd.end(),
                            [&](const mapLTFlightDataTy::value_type& mfd)
                            { return mfd.second.IsMatch(_s); } );
    }
}

