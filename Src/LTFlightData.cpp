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
                                        DatRequTy masterDataType)
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
            masterDataType == DATREQU_AC_MASTER)
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
        // or what a proper master data channel delivers
        (masterDataType == DATREQU_AC_MASTER && !other.mdl.empty()))
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
        masterDataType == DATREQU_ROUTE ||
        // or no flight number info at all...
        (other.flight.empty() && flight.empty()))
    {
        if (!other.stops.empty()) stops = other.stops;
        if (!other.flight.empty()) flight = other.flight;
    }
    
    // operator / Airline
    if (!other.op.empty()) op = other.op;
    // operator ICAO: we only accept a change from nothing to something,
    //                or the data of a proper master data channel
    if ((opIcao.empty() || masterDataType == DATREQU_AC_MASTER) &&
        !other.opIcao.empty() && opIcao != other.opIcao)
    {
        opIcao = other.opIcao;
        bRet = true;
    }
    
    // registration: we only accept a change from nothing to something,
    //               or the data of a proper master data channel
    if ((reg.empty() || masterDataType == DATREQU_AC_MASTER) &&
        !other.reg.empty() && reg != other.reg)
    {
        reg = other.reg;
        bRet = true;
    }

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
    std::for_each(stops.begin(), stops.end(), trim_ws);
    trim(flight);
    trim(op);
    trim(opIcao);
    
    // Flag if this was master data channel delivery
    switch (masterDataType) {
        case DATREQU_NONE:                          break;
        case DATREQU_AC_MASTER: bDataMaster = true; break;
        case DATREQU_ROUTE:     bDataRoute = true;  break;
    }
    
    return bRet;
}

// Fill stops from given origin/dest
void LTFlightData::FDStaticData::setOrigDest (const std::string& o, const std::string& d)
{
    stops.clear();
    if (!o.empty())
        stops.push_back(o);
    if (!d.empty()) {
        if (stops.empty()) stops.push_back("?");    // if we have a destination but no origin, then we put in ? as the origin
        stops.push_back(d);
    }
}

// route (this is "originAp - destAp", but considers emoty txt)
std::string LTFlightData::FDStaticData::route () const
{
    // keep it an empty string if there is no info at all
    if (stops.empty())
        return std::string();
    
    // add all stops separated by dashes
    std::string s(stops.front());
    for (auto i = std::next(stops.begin()); i != stops.end(); i++) {
        s += '-';
        s += *i;
    }
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

// is this a static object?
bool LTFlightData::FDStaticData::isStaticObject() const
{
    return acTypeIcao == STATIC_OBJECT_TYPE;
}

// is critical info for model matching available?
bool LTFlightData::FDStaticData::hasMdlMatchInfo() const
{
    return
        !acTypeIcao.empty() &&                      // a/c type designator
        (!opIcao.empty() || call.length() >= 3);    // some operator info
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
        case KEY_ADSBEX:
        case KEY_FLARM:
        case KEY_FSC:
            snprintf(buf, sizeof(buf), "%06lX", _num);
            break;
        case KEY_OGN:
        case KEY_RT:
        case KEY_PRIVATE:
            snprintf(buf, sizeof(buf), "%08lX", _num);
            break;
        case KEY_SAYINTENTIONS:
        case KEY_AUTOATC:
            snprintf(buf, sizeof(buf), "%lu", _num);
            break;
        case KEY_UNKNOWN:
        case KEY_ORG_SPECIFIC:
            // must not happen
            LOG_ASSERT(eKeyType!=KEY_UNKNOWN && eKeyType!=KEY_ORG_SPECIFIC);
            break;
    }
    LOG_ASSERT(buf[0]);
    return key = buf;
}

std::string LTFlightData::FDKeyTy::SetKey (FDKeyType _eType, const std::string _key, int base)
{
    return SetKey(_eType, std::stoul(_key, nullptr, base));
}


/// Equality: number must match, key types either both "interchangeable" or also match
bool LTFlightData::FDKeyTy::operator== (const FDKeyTy& o) const
{
    return
    num != o.num ? false :                          // if the numbers don't match, it's unequal in any case
    // so...numbers are equal, if both key types are "interchangeable", then it is now considered equal
    (eKeyType < KEY_ORG_SPECIFIC && o.eKeyType < KEY_ORG_SPECIFIC) ? true :
    // otherwise, also the key types must match
    eKeyType == o.eKeyType;
}

/// Less than: in case of "interchangeable" key types only depends on the number, else both type and number
bool LTFlightData::FDKeyTy::operator<  (const FDKeyTy& o) const
{
    return
    // both sides "interchangeable" key types: only depends on the number
    (eKeyType < KEY_ORG_SPECIFIC && o.eKeyType < KEY_ORG_SPECIFIC) ? num < o.num :
    // else the key type has higher priority
    eKeyType == o.eKeyType ? num < o.num : eKeyType < o.eKeyType;
}



// return the type of key (as string)
const char* LTFlightData::FDKeyTy::GetKeyTypeText () const
{
    switch (eKeyType) {
        case KEY_UNKNOWN:   return "unknown";
        case KEY_ICAO:      return "ICAO";
        case KEY_FLARM:     return "FLARM";
        case KEY_ADSBEX:    return "ADSBEx";
        case KEY_RT:        return "RealTraffic";
        case KEY_OGN:       return "OGN";
        case KEY_ORG_SPECIFIC: return "org-specific";   // not actually used...just to please compiler warnings
        case KEY_FSC:       return "FSCharter";
        case KEY_SAYINTENTIONS: return "SI";
        case KEY_AUTOATC:   return "AutoATC";
        case KEY_PRIVATE:   return "private";
    }
    return "unknown";
}


//
//MARK: Flight Data
//

/// Question mark for static returns
std::string LTFlightData::FDStaticData::emptyStr;

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


// Search support: icao, registration, call sign, flight number matches?
bool LTFlightData::IsMatch (const std::string t) const
{
    // we can compare key without lock
    if (acKey == t)
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
    
    // We don't think about creation if we would be hidden immediately
    // So if we are static but user doesn't want static...let's not even create
    if (statData.isStaticObject() && dataRefs.GetHideStaticTwr())
        return false;

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
        ADD_LABEL(cfg.bTranspCode,  std::string(key()));
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
            // If aircraft is parked and we shall not show labels for parked a/c, then return nothing
            if (!dataRefs.LabelShowForParked() &&
                pAc->GetFlightPhase() == FPH_PARKED)
                return "";
            
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
            if (cfg.bChannel) {
                const LTChannel* pChn = nullptr;
                if (GetCurrChannel(pChn) && pChn) {
                    label += pChn->ChName();
                    label += ' ';
                }
            }
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

    // Skip processing if not reasonable:
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0 ||      // Snap-to-taxiway not enabled
        posDeque.empty() ||                         // no aircraft positions available to process
        statData.isGrndVehicle() ||                 // ground vehicle
        (pAc && pAc->IsGroundVehicle()))
        return;

    // Skip snap-to-taxiway entirely while the aircraft is in ground-holding.
    //
    // bGroundHolding is set by AddNewPos after a sustained stationary streak
    // (see GND_HOLDING_TIMEOUT_S). It is our positive assertion that this
    // aircraft is parked. Real-feed data for parked aircraft can occasionally
    // produce isolated large position jumps (observed: ACA34 at YSSY, 105 m
    // jump while the RT app showed the aircraft stationary). Such jumps
    // exceed our 15 m trivial-drop threshold and end up in posDeque, but
    // they are almost always feed glitches rather than real motion.
    //
    // If we let SnapToTaxiways run on a glitched 100m+ jump, it computes a
    // shortest path through the airport's taxi graph and inserts a sequence
    // of intermediate waypoints with NaN heading. CalcHeading then derives
    // heading from the vector between those synthesized waypoints — which
    // reflects the taxiway geometry, not the aircraft's nose direction —
    // and the rendered aircraft visually dances through the phantom path.
    //
    // By suppressing snap during holding, we let the glitched jump pass
    // through the deque as a single linear interpolation (a one-time visual
    // wobble at worst, no waypoint procession). When the aircraft genuinely
    // begins to taxi, AddNewPos's GND_HOLDING_EXIT_CONSEC counter clears
    // bGroundHolding and snap-to-taxiway resumes for subsequent slots.
    if (bGroundHolding)
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
            // Run the EHS-staleness cross-check (and the rest of the
            // on-ground heading filter chain) on this slot BEFORE
            // handing it to LTAptSnap.
            //
            // Why: `LTAptSnap` uses `pos.heading()` to decide which
            // direction along a taxi edge to route the aircraft (see
            // TaxiEdge::startByHeading / endByHeading in LTApt.cpp).
            // The feed-supplied heading comes from Mode S Enhanced
            // Surveillance, which updates roughly every 10 s and lags
            // during turns. If snap reads a stale value the synthesised
            // taxi path can be routed BACKWARD along the edge — the
            // rendered aircraft visibly moves the wrong way along its
            // taxiway between feed samples. Observed for DAL973 and
            // RPA5716 at YSSY.
            //
            // `CalcHeading` (since commit d861869) applies the feed-vs-
            // track cross-check that catches exactly this case: if the
            // feed heading disagrees with the actual motion track by
            // 30-150° it falls through to the track-derived value
            // instead. Running it here means snap sees the corrected
            // heading and routes the right way.
            //
            // The existing post-snap CalcHeading loop in CalcNextPos
            // remains responsible for filling in the heading of the
            // intermediate waypoints that snap itself synthesises
            // (those are inserted with heading=NaN).
            CalcHeading(iter);

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
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_CalcPos", LC_ALL_MASK);

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
                    LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION " - on aircraft %s", "(unknown)", pair.first.c_str());
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
    // access guarded by a mutex
    //
    // NOTE: the `bHeadFixed` early-return is intentionally NOT here.
    // The pushback state machine below mutates per-flight persistent
    // state (`pbState`, `pbHeldNose`, `bGateParked`) that must be
    // updated consistently across deque re-evaluations performed by
    // `CalcNextPos` (the `bChanged` recompute loop in CalcNextPos
    // re-runs CalcHeading on every slot in posDeque). If we early-
    // return on `bHeadFixed`, the state machine misses the state
    // transitions encoded in already-overridden slots and later slots
    // see the wrong `pbState`. The `bHeadFixed` guard is therefore
    // moved AFTER the pushback section: the state machine always
    // runs and updates state, but the override block only WRITES the
    // heading on slots whose heading has not already been fixed.
    std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

    // ----------------------------------------------------------------------
    // Pushback state machine (simplified).
    //
    // Premise: we know an aircraft is parked at a gate when it has had a
    // SPOS_STARTUP slot in its deque (the `bGateParked` flag is set in
    // AddNewPos when such a slot is added). When a gate-parked aircraft
    // starts to move, we assume it is being pushed back. The push remains
    // active until the aircraft becomes stationary AND then resumes
    // motion in the opposite direction (i.e. forward, taxi). While in
    // pushback the rendered heading is forced to `track + 180°` so the
    // tail leads the direction of motion — naturally handling rotating
    // pushes because the nose is recomputed every motion slot.
    //
    // States:
    //   PB_NONE   : not in pushback. The only way to enter is from
    //               `bGateParked && bMotion`. Falls through to normal
    //               heading logic.
    //   PB_ACTIVE : being pushed. Heading override active.
    //   PB_PAUSED : was being pushed, currently stationary. Heading
    //               held at `pbHeldNose`. On next motion slot:
    //                 - motion forward of held nose → exit to PB_NONE
    //                 - motion still against held nose → back to PB_ACTIVE
    // ----------------------------------------------------------------------
    if (!it->IsOnGnd()) {
        // Airborne — any pushback is long over; clear the state defensively.
        pbState        = PB_NONE;
        pbHeldNose     = NAN;
        pbUseFeedNose  = false;
        bGateParked    = false;
    }
    else {
        // Resolve a predecessor position for the PB state machine.
        //
        // Normal case: the slot before `it` in posDeque is the
        // predecessor. But after a long stationary period at a gate,
        // CalcNextPos has drained the deque (each rendered position is
        // popped from the front), and `it` arrives as the ONLY element
        // in posDeque (or at posDeque.begin()) when GATE_RELEASE finally
        // lets a slot through. In that state there is no in-deque
        // predecessor — the state machine would skip its entry test
        // entirely and fall through to the FEEDHDG cross-check, which
        // for a post-rotation aircraft mistakes "feedHdg ≈ track" for
        // forward taxi and assigns the motion-direction heading,
        // rendering the aircraft facing forward into the push.
        //
        // Fix: when there is no in-deque predecessor, fall back to the
        // aircraft's last-rendered position via `pAc->GetToPos()`. That
        // is the parked position with the parked heading — exactly the
        // reference we need to detect "motion is rearward of the held
        // nose" and enter PB_ACTIVE on the first accepted slot.
        const positionTy* pPrePos = nullptr;
        if (it != posDeque.cbegin()) {
            pPrePos = &*std::prev(it);
        } else if (pAc) {
            const positionTy& toPos = pAc->GetToPos();
            if (toPos.isNormal(true) && toPos.IsOnGnd() &&
                !std::isnan(toPos.ts()) && it->ts() > toPos.ts())
            {
                pPrePos = &toPos;
            }
        }

        if (pPrePos) {
            const positionTy& prePosPb = *pPrePos;
            if (prePosPb.IsOnGnd() && it->ts() > prePosPb.ts()) {
            // -------------------------------------------------------------
            // Robust 4-slot track filter.
            //
            // At slow ground speeds (a few knots), the 1Hz GPS deltas are
            // dominated by per-fix noise (~3 m typical). A single 2-point
            // delta can swing the derived track angle by tens of degrees,
            // which then drives the rendered nose (track + 180° during a
            // pushback) into visible spinning even when the aircraft is
            // moving in a steady direction.
            //
            // Procedure (per user direction):
            //   1. Collect the latest 4 ground positions ending at *it.
            //   2. Convert to a local east/north metres frame around the
            //      newest sample (Lat2Dist / Lon2Dist).
            //   3. Compute the equal-weight centroid of the 4 points.
            //   4. Reject the 2 points with the largest residual from
            //      the centroid as outliers.
            //   5. Derive the motion vector from the remaining 2 inliers,
            //      oldest-to-newest in time. The angle of that vector is
            //      the filtered track; its length is the filtered chord.
            //
            // The aircraft's facing direction is derived elsewhere from
            // this track (track + 180° during a push, see computeNose()
            // below). So both motion and rendered nose come from the
            // same robust estimate.
            //
            // If fewer than 4 ground positions are available (early in
            // a flight, after a non-ground gap), fall back to the simple
            // 2-point between() vector.
            // -------------------------------------------------------------
            vectorTy pbTrack = prePosPb.between(*it);
            {
                std::array<dequePositionTy::const_iterator, 4> samples;
                size_t n = 0;
                auto walk = it;
                while (n < samples.size()) {
                    if (!walk->IsOnGnd()) break;
                    samples[n++] = walk;
                    if (walk == posDeque.cbegin()) break;
                    if (n < samples.size()) --walk;
                }
                if (n == samples.size()) {
                    // samples[0] is newest, samples[3] is oldest — reverse
                    // to oldest-to-newest temporal order.
                    std::reverse(samples.begin(), samples.end());

                    // Local east/north metres around the newest sample.
                    const positionTy& refPos = *samples[3];
                    std::array<std::pair<double,double>, 4> xy;
                    for (size_t i = 0; i < 4; ++i) {
                        xy[i].first  = Lon2Dist(samples[i]->lon() - refPos.lon(),
                                                refPos.lat());
                        xy[i].second = Lat2Dist(samples[i]->lat() - refPos.lat());
                    }

                    // Equal-weight centroid. Equal weights are deliberate:
                    // a recency-weighted mean would bias the centroid
                    // toward recent positions and then preferentially flag
                    // older positions as outliers even when they aren't
                    // noisy — the wrong thing for outlier detection.
                    double cx = 0.0, cy = 0.0;
                    for (const auto& p : xy) { cx += p.first; cy += p.second; }
                    cx *= 0.25; cy *= 0.25;

                    // Residual magnitude per sample.
                    std::array<std::pair<double,size_t>, 4> resid;
                    for (size_t i = 0; i < 4; ++i) {
                        const double dx = xy[i].first  - cx;
                        const double dy = xy[i].second - cy;
                        resid[i] = { std::sqrt(dx*dx + dy*dy), i };
                    }
                    // Sort residuals descending; first two are the outliers.
                    std::sort(resid.begin(), resid.end(),
                              [](const std::pair<double,size_t>& a,
                                 const std::pair<double,size_t>& b)
                              { return a.first > b.first; });
                    const size_t out1 = resid[0].second;
                    const size_t out2 = resid[1].second;

                    // Inliers in original (oldest→newest) order.
                    size_t i0 = SIZE_MAX, i1 = SIZE_MAX;
                    for (size_t i = 0; i < 4; ++i) {
                        if (i == out1 || i == out2) continue;
                        if (i0 == SIZE_MAX) i0 = i;
                        else                i1 = i;
                    }

                    if (i0 != SIZE_MAX && i1 != SIZE_MAX) {
                        const double dx = xy[i1].first  - xy[i0].first;
                        const double dy = xy[i1].second - xy[i0].second;
                        const double dist = std::sqrt(dx*dx + dy*dy);
                        if (dist >= SIMILAR_POS_DIST) {
                            // Replace the 2-point estimate with the
                            // filtered chord. CoordAngle returns a bearing
                            // in degrees (0..360, north=0, clockwise) — the
                            // same convention as positionTy::between().
                            pbTrack.angle = CoordAngle(samples[i0]->lat(),
                                                       samples[i0]->lon(),
                                                       samples[i1]->lat(),
                                                       samples[i1]->lon());
                            pbTrack.dist  = dist;
                        }
                        // If the filtered chord is below noise floor,
                        // leave the 2-point pbTrack alone; bMotion below
                        // will then treat it as stationary.
                    }
                }
            }

            const double   pbGs_kt   = prePosPb.speed_kt(*it);
            // Capture the feed-reported heading on THIS slot BEFORE we
            // override it. The state machine's nose-source decision (see
            // `pbUseFeedNose`) reads this value at PB_NONE→PB_ACTIVE
            // entry, and PB_ACTIVE refreshes pbHeldNose from this same
            // value on every motion slot when the feed is the chosen
            // source. Reading from `it->heading()` AFTER the override
            // block would observe our own previously-written value, not
            // the feed's actual report.
            const double   pbFeedHdg = it->heading();

            // bMotion: is this slot's motion meaningful for state
            // machine purposes? Gate on groundspeed and a defined
            // track angle ONLY — do NOT require per-slot chord length
            // ≥ SIMILAR_POS_DIST. A real pushback at 1–3 kt produces
            // 3–6 m of motion per 3–5 s slot, which is below 7 m and
            // would falsely flip the machine to PAUSED on every slot
            // even though motion is continuous. The looser test keeps
            // the machine in ACTIVE for the whole push so the nose
            // is refreshed each slot.
            //
            // Use `PB_MOTION_GS_KT` (0.3 kt), NOT the global
            // `GND_STATIONARY_GS_KT` (1.5 kt). Real pushbacks roll at
            // 0.4–1.4 kt — entirely below the global stationary
            // threshold — so gating on `gs > GND_STATIONARY_GS_KT`
            // here would prevent the state machine from EVER entering
            // PB_ACTIVE for a real slow push. The aircraft would never
            // get a heading override, the renderer's spline branch
            // would fall back to the motion tangent (≈ direction of
            // travel), and the rendered nose would face FORWARD into
            // the push instead of tail-first. Combined with the
            // upstream distance-based GATE_HOLD in AddNewPos
            // (`GATE_HOLD_MIN_ACCEPT_M`), there is no realistic noise
            // path that can trip the state machine at 0.3 kt — any
            // noise that produces ≥0.3 kt for one slot is filtered out
            // upstream by the 30 m gate.
            const bool     bMotion   = !std::isnan(pbGs_kt) &&
                                       pbGs_kt > PB_MOTION_GS_KT &&
                                       !std::isnan(pbTrack.angle);

            // Nose source for the entire push: either the FEED heading
            // (when the feed is reporting true compass nose — rotates
            // accurately during a rotating push) or `track + 180°` (when
            // the feed is reporting course-over-ground — useless as a
            // nose reference during a push because COG ≈ motion direction
            // ≈ 180° away from where the nose is actually pointing).
            //
            // The choice is made ONCE at PB_NONE→PB_ACTIVE entry by
            // comparing the first-motion feedHdg against the prior parked
            // heading — see the entry block below — and persisted in
            // `pbUseFeedNose` for the rest of the push. NOT re-evaluated
            // mid-push; per-slot re-evaluation produced the visible
            // spinning bug in earlier revisions when the source flipped
            // every slot.
            auto computeNose = [&]() -> double {
                if (pbUseFeedNose && !std::isnan(pbFeedHdg))
                    return pbFeedHdg;
                return HeadingNormalize(pbTrack.angle + 180.0);
            };

            switch (pbState) {
                case PB_NONE:
                    // Enter pushback only when:
                    //   (a) the aircraft was parked at a gate
                    //       (bGateParked, set in AddNewPos for slots
                    //       carrying SPOS_STARTUP or for aircraft
                    //       whose apt.dat lookup found a startup-loc
                    //       within GATE_DETECT_MAX_DIST_M when
                    //       bGroundHolding flipped true), AND
                    //   (b) this slot carries real motion (gs above
                    //       stationary threshold; no chord requirement),
                    //       AND
                    //   (c) the motion is REARWARD of the prior held
                    //       heading — i.e. moving in the opposite
                    //       half-plane to where the nose was last facing.
                    //
                    // Condition (c) is a one-shot entry test. Without it,
                    // a forward taxi resuming from a brief stop near an
                    // apt.dat startup-loc would false-positive as a push.
                    // 90° splits the half-planes.
                    //
                    // On entry we ALSO decide the nose source for the
                    // entire push. If the first-motion feed heading is
                    // within PB_FEED_NOSE_AGREE_DEG of the prior parked
                    // heading, the feed is reporting true compass nose
                    // (rotates accurately during the push) — lock onto
                    // feed for the duration. Otherwise the feed is
                    // course-over-ground; derive the nose from track
                    // instead.
                    if (bGateParked && bMotion &&
                        !std::isnan(prePosPb.heading()) &&
                        std::abs(HeadingDiff(prePosPb.heading(), pbTrack.angle))
                            > PB_EXIT_FORWARD_DIFF_DEG)
                    {
                        pbState = PB_ACTIVE;
                        pbUseFeedNose =
                            !std::isnan(pbFeedHdg) &&
                            std::abs(HeadingDiff(prePosPb.heading(),
                                                 pbFeedHdg))
                                <= PB_FEED_NOSE_AGREE_DEG;
                        pbHeldNose = computeNose();

                        // Retroactively pin the predecessor slot's heading
                        // so the renderer interpolates across the entry
                        // leg instead of using the motion tangent.
                        //
                        // Why this matters: the ground-rendering spline in
                        // LTAircraft::CalcAcPos has two heading branches
                        // (see `LTAircraft.cpp:2143`):
                        //   * `from.f.bHeadFixed == true`  → interpolate
                        //     `from.heading()` and `to.heading()` across
                        //     the leg (preserve slot headings).
                        //   * `from.f.bHeadFixed == false` → use the
                        //     spline tangent, i.e. the direction of motion.
                        //
                        // Slots written by the pushback override block
                        // below already have `bHeadFixed=true`. But the
                        // PREDECESSOR slot — the last parked slot — was
                        // produced by the live-feed path that DOES NOT
                        // set `bHeadFixed`. So on the leg from "last
                        // parked slot" to "first pushback slot", the
                        // renderer falls into the spline-tangent branch
                        // and points the rendered nose along the motion
                        // direction (≈ 180° opposite to where we want
                        // it). The aircraft visually pivots to face the
                        // push direction at the gate — the exact symptom
                        // we are trying to avoid.
                        //
                        // The fix: stamp `bHeadFixed=true` onto prePosPb.
                        // Its heading value is already the parked heading
                        // (unchanged), so this only flips the flag; the
                        // renderer then takes the interpolation branch
                        // and the rendered nose swings smoothly from the
                        // parked heading to `pbHeldNose` over the leg.
                        //
                        // Guard against the `pPrePos` fallback case
                        // (when prePos was sourced from pAc->GetToPos()
                        // because posDeque had no in-deque predecessor).
                        // In that branch `std::prev(it)` would be UB
                        // (walks past `posDeque.cbegin()`). The renderer-
                        // side change in LTAircraft::CalcAcPos that also
                        // checks `to.f.bHeadFixed` covers this case from
                        // the other direction.
                        if (it != posDeque.cbegin())
                            std::prev(it)->f.bHeadFixed = true;

                        LOG_MSG(logDEBUG,
                                "PUSHBACK_DIAG %s ENTRY src=%s parkedHdg=%.1f"
                                " firstFeedHdg=%.1f firstTrack=%.1f"
                                " heldNose=%.1f (predBHF pinned)",
                                key().c_str(),
                                pbUseFeedNose ? "FEED" : "TRACK+180",
                                prePosPb.heading(),
                                std::isnan(pbFeedHdg) ? -1.0 : pbFeedHdg,
                                pbTrack.angle,
                                pbHeldNose);
                    } else if (bGateParked && bMotion) {
                        // Forward motion from a gate-parked aircraft —
                        // this is not a push. Clear bGateParked so the
                        // permissive flag doesn't keep triggering the
                        // entry test on every subsequent motion slot.
                        bGateParked = false;
                    }
                    break;

                case PB_ACTIVE:
                    if (bMotion) {
                        // Refresh nose on every motion slot. The source
                        // (feed or track+180°) was locked at entry and
                        // does not change here — only the underlying
                        // value evolves with new feed/motion data.
                        pbHeldNose = computeNose();
                    } else {
                        // Truly stationary slot (gs below threshold).
                        // Don't clear pbHeldNose — it is the reference
                        // for the next motion-direction test.
                        pbState = PB_PAUSED;
                    }
                    break;

                case PB_PAUSED:
                    if (bMotion) {
                        // Direction-of-resumed-motion test. During the
                        // prior push the motion was `pbHeldNose ± 180°`
                        // (tail-leading). "Opposite direction now" means
                        // new motion track is aligned with `pbHeldNose`
                        // (nose-leading taxi). Within 90° of pbHeldNose
                        // → forward taxi, EXIT. Otherwise the tug is
                        // still pushing → back to ACTIVE.
                        if (!std::isnan(pbHeldNose) &&
                            std::abs(HeadingDiff(pbHeldNose, pbTrack.angle))
                                < PB_EXIT_FORWARD_DIFF_DEG)
                        {
                            pbState        = PB_NONE;
                            pbHeldNose     = NAN;
                            pbUseFeedNose  = false;
                            bGateParked    = false;
                            // Fall through to normal heading logic below.
                        } else {
                            pbState    = PB_ACTIVE;
                            pbHeldNose = computeNose();
                        }
                    }
                    break;
            }

            // Heading override while in PB_ACTIVE or PB_PAUSED.
            if (pbState == PB_ACTIVE || pbState == PB_PAUSED) {
                // Only WRITE the heading on slots that haven't already
                // had their heading fixed. On a deque re-evaluation
                // (CalcNextPos recompute loop) the state machine above
                // has already run and updated `pbState`; rewriting an
                // already-fixed heading would either be a no-op (same
                // value) or worse, an inconsistency if the state
                // machine now disagrees with the prior decision.
                if (!it->f.bHeadFixed) {
                    // Always write pbHeldNose — it has just been refreshed
                    // by computeNose() (feed nose if it disagrees with
                    // track, otherwise derived track+180°). This keeps
                    // rotating pushbacks visually correct: the body
                    // tracks the actual nose direction reported by the
                    // feed, not the position-delta chord which is wrong
                    // when the aircraft is rotating through the push.
                    if (!std::isnan(pbHeldNose)) {
                        it->heading() = pbHeldNose;
                    } else if (!std::isnan(prePosPb.heading())) {
                        it->heading() = prePosPb.heading();
                    }
                    it->f.bHeadFixed = true;

                    // Per-slot diagnostic line — emitted only on the
                    // initial override pass (when bHeadFixed flips
                    // false→true), not on every re-eval.
                    LOG_MSG(logDEBUG,
                            "PUSHBACK_DIAG %s state=%s gs=%.2fkt track=%.1f"
                            " heldNose=%.1f assigned=%.1f bGateParked=%d",
                            key().c_str(),
                            pbState == PB_ACTIVE ? "ACTIVE" : "PAUSED",
                            std::isnan(pbGs_kt) ? -1.0 : pbGs_kt,
                            std::isnan(pbTrack.angle) ? -1.0 : pbTrack.angle,
                            std::isnan(pbHeldNose) ? -1.0 : pbHeldNose,
                            it->heading(),
                            int(bGateParked));
                }
                return;
            }
        }
        }   // close if (pPrePos)
    }

    // Honour the per-slot `bHeadFixed` flag for everything below this
    // point (feed-heading, position-derived, etc.). It is intentional
    // that the pushback section above runs BEFORE this gate so that
    // its persistent state machine stays consistent across deque
    // re-evaluations — see the note at the top of this function.
    if (it->f.bHeadFixed)
        return;

    // ----------------------------------------------------------------------
    // Trust feed-provided heading at slow ground speed (with staleness check).
    //
    // The position-from-track logic below derives heading by taking
    // atan2 over consecutive lat/lon pairs. At parked, slow-taxi, and
    // pushback speeds that math produces wildly wrong answers:
    //   * parked aircraft: positional jitter IS the apparent motion vector
    //   * pushback: the track points OPPOSITE to the nose (tail-first),
    //     so atan2 gives a heading 180° off
    //
    // RealTraffic and most ADS-B feeds provide an explicit heading field
    // sourced from Mode S Enhanced Surveillance (EHS) — the aircraft's
    // own reported nose direction. We prefer it for the slow-ground cases.
    //
    // *Staleness*: EHS heading typically updates only every 10 s, and is
    // unavailable entirely in regions without enhanced interrogation
    // coverage. Between EHS updates the feed value is held constant by
    // the receiver. During a taxi turn at 5–10 kn that 10 s freshness
    // window is long enough for the aircraft to change direction by 60°
    // or more — if we blindly trust the held value the rendered nose
    // visibly lags the actual motion and the aircraft appears to slide
    // sideways through the turn.
    //
    // Defence: cross-check the feed heading against the *current* track
    // (bearing from the predecessor slot to this one). Three regimes,
    // gated by `GND_FEED_TRACK_AGREE_DEG` (see Constants.h):
    //   * |Δ| < 30°:    feed agrees with track — fresh, or aircraft is
    //                   going straight. Trust feed.
    //   * 30° ≤ |Δ| ≤ 150°: feed has lagged during a turn. Fall through
    //                   to the position-derived branch — track wins.
    //   * |Δ| > 150°:   track is roughly opposite the feed — pushback.
    //                   Trust feed (nose stays at gate).
    //
    // Limitations:
    //   * Feed heading exactly 0.0 may be a channel "no data" sentinel
    //     rather than a real reading. We accept that risk — the agree-
    //     window check filters out the worst cases (a stale 0.0 paired
    //     with non-zero motion will fall outside the agree window).
    //   * Above `GND_USE_FEED_HEADING_MAX_KT` (10 kn) the track-derived
    //     heading is always preferred (real taxi / rollout / takeoff).
    // ----------------------------------------------------------------------
    if (it->IsOnGnd() && !std::isnan(it->heading())) {
        // Derive groundspeed + track angle from the predecessor pair when
        // possible. A missing predecessor (head of deque) means we have
        // no track to cross-check against — feed is the best we have.
        double gsDerived_kt = NAN;
        double trackAngle   = NAN;
        if (it != posDeque.cbegin()) {
            const positionTy& prePos = *std::prev(it);
            if (prePos.IsOnGnd() && it->ts() > prePos.ts()) {
                gsDerived_kt = prePos.speed_kt(*it);
                // Only compute a track angle when motion is non-trivial;
                // for jitter-only displacement the bearing is meaningless
                // and would force us into the disagreement band by noise
                // alone.
                if (gsDerived_kt > GND_STATIONARY_GS_KT) {
                    const vectorTy vec = prePos.between(*it);
                    trackAngle = vec.angle;
                }
            }
        }

        // Decide whether the feed heading is the right source for this
        // slot. The reason string is purely for the diagnostic log line
        // that follows — it lets us see WHY a feed value won (or lost)
        // when investigating regressions from a Log.txt capture.
        bool trustFeed = false;
        const char* reason = "";
        if (std::isnan(gsDerived_kt)) {
            // No predecessor — no track to compare. Feed is the only
            // reliable source we have.
            trustFeed = true;
            reason = "no predecessor";
        } else if (gsDerived_kt < GND_STATIONARY_GS_KT) {
            // Stationary: positional jitter dominates any track we could
            // compute, so it would be garbage. Feed value wins.
            trustFeed = true;
            reason = "stationary";
        } else if (gsDerived_kt < GND_USE_FEED_HEADING_MAX_KT) {
            // Slow motion in the band where feed could be used — apply
            // the cross-check against the track angle.
            if (!std::isnan(trackAngle)) {
                const double delta =
                    std::abs(HeadingDiff(it->heading(), trackAngle));
                if (delta < GND_FEED_TRACK_AGREE_DEG) {
                    trustFeed = true;
                    reason = "agrees with track";
                } else if (delta > (180.0 - GND_FEED_TRACK_AGREE_DEG)) {
                    trustFeed = true;
                    reason = "track reversed (pushback)";
                }
                // else: feed has gone stale during a turn — fall through
                // to the position-derived heading branch below.
            } else {
                // No track to compare (shouldn't happen if gsDerived_kt
                // is non-NaN and above stationary, but be defensive).
                trustFeed = true;
                reason = "no track to compare";
            }
        }
        // Above GND_USE_FEED_HEADING_MAX_KT we never trust feed — the
        // outer-loop track-heading regime in LTAircraft::CalcAcPos owns
        // that range. Leave `trustFeed=false` so we fall through.

        if (trustFeed) {
            LOG_MSG(logDEBUG,
                    "GND_DIAG_FEEDHDG %s ts=%.1f feedHdg=%.1f gs=%.2fkt"
                    " track=%.1f (%s)",
                    key().c_str(), it->ts(), it->heading(),
                    std::isnan(gsDerived_kt) ? 0.0 : gsDerived_kt,
                    std::isnan(trackAngle)   ? -1.0 : trackAngle,
                    reason);
            return;
        }
    }

    // ----------------------------------------------------------------------
    // Ground-stationary freeze.
    //
    // Purpose: when an aircraft is on the ground and not really moving, the
    // raw position samples from a 1 Hz feed carry a few metres of jitter.
    // If we let the normal vector-between-positions math compute a heading
    // from that jitter, the rendered nose will swing wildly — the "dance"
    // symptom users see at gates and slow taxi. This branch detects the
    // stationary case (low derived groundspeed between this slot and at
    // least one neighbour) and reuses the previously trusted heading from
    // the predecessor in the deque, which has already been filtered by the
    // earlier `CalcHeading` calls that produced it. Threshold:
    // `GND_STATIONARY_GS_KT` (see `Constants.h` for the rationale).
    // ----------------------------------------------------------------------
    if (it->IsOnGnd()) {
        // Derived groundspeed FROM the predecessor (if any) to this slot,
        // in knots. We use the position-pair speed helper rather than the
        // dynamic-data feed value because the feed value is what we are
        // trying to filter — the derived value tells us whether this slot
        // is "moving" relative to its neighbour regardless of what the feed
        // claims.
        double gsFromPrev_kt = NAN;
        if (it != posDeque.cbegin()) {
            const positionTy& prePos = *std::prev(it);
            if (prePos.IsOnGnd() && it->ts() > prePos.ts())
                gsFromPrev_kt = prePos.speed_kt(*it);
        }
        // Derived groundspeed FROM this slot to the successor (if any)
        double gsToNext_kt = NAN;
        if (std::next(it) != posDeque.cend()) {
            const positionTy& nextPos = *std::next(it);
            if (nextPos.IsOnGnd() && nextPos.ts() > it->ts())
                gsToNext_kt = it->speed_kt(nextPos);
        }

        // Classify each adjacent segment. We require BOTH segments (or the
        // only available one at the ends of the deque) to be stationary
        // before we lock the heading. Requiring two consecutive zero-ish
        // slots avoids reacting to a single isolated tight cluster that
        // can occur briefly during normal taxi.
        const bool prevStationary = !std::isnan(gsFromPrev_kt) &&
                                    gsFromPrev_kt <= GND_STATIONARY_GS_KT;
        const bool nextStationary = !std::isnan(gsToNext_kt)  &&
                                    gsToNext_kt  <= GND_STATIONARY_GS_KT;
        const bool isolated       =  std::isnan(gsFromPrev_kt) ||
                                     std::isnan(gsToNext_kt);

        // ----- TEMPORARY GROUND DIAGNOSTIC LOGGING (tag: GND_DIAG_CHD) -----
        // Captures the per-slot inputs that drive the stationary-freeze
        // decision in CalcHeading. Fires unconditionally for every ground
        // slot. Search the log for "GND_DIAG_CHD" to filter.
        LOG_MSG(logDEBUG,
                "GND_DIAG_CHD %s ts=%.1f gsPrev=%.2fkt gsNext=%.2fkt"
                " hdg_in=%.1f prevHdg=%.1f nextHdg=%.1f stationary={p=%d,n=%d,iso=%d}",
                key().c_str(), it->ts(),
                gsFromPrev_kt, gsToNext_kt,
                it->heading(),
                it != posDeque.cbegin() ? std::prev(it)->heading() : NAN,
                std::next(it) != posDeque.cend() ? std::next(it)->heading() : NAN,
                prevStationary ? 1 : 0,
                nextStationary ? 1 : 0,
                isolated ? 1 : 0);

        if ((prevStationary && nextStationary) ||
            (isolated && (prevStationary || nextStationary)))
        {
            // Prefer the predecessor's heading — it's the most recent
            // value that already passed through this filter chain.
            if (it != posDeque.cbegin()) {
                const double prevHead = std::prev(it)->heading();
                if (!std::isnan(prevHead)) {
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_FREEZE %s ts=%.1f kept prevHdg=%.1f",
                            key().c_str(), it->ts(), prevHead);
                    it->heading() = prevHead;
                    return;
                }
            }
            // No usable predecessor: if the slot already carries a
            // heading (e.g., a feed channel like RealTraffic provides
            // one directly), keep it — anything is better than the
            // jitter-derived value we would otherwise produce.
            if (!std::isnan(it->heading()))
                return;
            // Otherwise fall through to the normal computation below;
            // the existing `SIMILAR_POS_DIST` short-circuit will likely
            // still kick in and stabilise this slot from the predecessor.
        }
    }

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
    
    // ----------------------------------------------------------------------
    // Ground heading hysteresis.
    //
    // After the heading for this slot has been (re)computed by the logic
    // above, snap it back to the predecessor's heading if the difference
    // is below `GND_HEADING_HYSTERESIS_DEG`. The reasoning: real-feed
    // ADS-B/MLAT data routinely produces sub-degree variations in the
    // track-from-pos-delta even when the aircraft is genuinely moving
    // in a straight line. Those sub-degree changes do not represent
    // physical reality and, if propagated, accumulate frame-by-frame
    // into visible nose-wobble at slow ground speeds. The dead-band
    // matches the convention used for the rendered heading rate-limit
    // in `LTAircraft::CalcAcPos`, so the two layers reinforce each
    // other rather than fighting.
    //
    // We only do this on the ground — in the air, small heading changes
    // are usually meaningful (drift, gentle course corrections) and
    // suppressing them would make en-route tracks look "stairstepped".
    // ----------------------------------------------------------------------
    if (it->IsOnGnd() && it != posDeque.cbegin()) {
        const double prevHead = std::prev(it)->heading();
        if (!std::isnan(prevHead) && !std::isnan(it->heading())) {
            const double dHead = std::abs(HeadingDiff(prevHead, it->heading()));
            // ----- TEMPORARY GROUND DIAGNOSTIC LOGGING (tag: GND_DIAG_HYST) -
            LOG_MSG(logDEBUG,
                    "GND_DIAG_HYST %s ts=%.1f prevHdg=%.1f newHdg=%.1f"
                    " |delta|=%.2f%s",
                    key().c_str(), it->ts(), prevHead, it->heading(), dHead,
                    dHead < GND_HEADING_HYSTERESIS_DEG ? " -> SNAP" : "");
            if (dHead < GND_HEADING_HYSTERESIS_DEG)
            {
                it->heading() = prevHead;
            }
        }
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
                     nanToZero(GeoAltToBaroAlt_ft(pos.alt_ft(), dataRefs.GetPressureHPA())),   // alt
                     inDyn.vsi,                                                 // vs
                     (pos.IsOnGnd() ? '0' : '1'),                               // airborne
                     inDyn.heading, inDyn.spd,                                  // hdg,spd
                     statData.call.c_str(),                                     // cs
                     statData.acTypeIcao.c_str(),                               // type
                     statData.reg.c_str(),                                      // tail
                     statData.origin().c_str(),                                 // from
                     statData.dest().c_str(),                                   // to
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
                     nanToZero(GeoAltToBaroAlt_ft(pos.alt_ft(), dataRefs.GetPressureHPA())),   // baro_alt
                     inDyn.vsi,                                                 // baro_rate
                     (pos.IsOnGnd() ? '0' : '1'),                               // airborne
                     inDyn.heading, inDyn.spd,                                  // track, gsp
                     statData.call.c_str(),                                     // cs_icao
                     statData.acTypeIcao.c_str(),                               // ac_type
                     statData.reg.c_str(),                                      // ac_tailno
                     statData.origin().c_str(),                                 // from_iata
                     statData.dest().c_str(),                                   // to_iata
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

            // --------------------------------------------------------------
            // Ground holding state machine + trivial-update suppression.
            //
            // Purpose: an aircraft that has been parked for longer than
            // `GND_HOLDING_TIMEOUT_S` should ignore the small-amplitude
            // position jitter that real feeds keep producing for a
            // stationary target. Once we enter "holding", we silently
            // drop incoming slots whose distance from the latest known
            // position is below `GND_HOLDING_TRIVIAL_DIST_M` AND whose
            // derived groundspeed (from the time/distance pair) is at or
            // below `GND_STATIONARY_GS_KT`. That is the data-layer half
            // of the dance fix: the rendered aircraft never even sees
            // these updates so it cannot react to them.
            //
            // The state machine is driven entirely by `pos.ts()` (the
            // feed-reported wall-clock timestamp), not by sim time —
            // that way pause/resume and rate-changes in X-Plane don't
            // influence the streak length.
            // --------------------------------------------------------------
            const bool   bothOnGround = pos.IsOnGnd() && pLatestPos->IsOnGnd();
            const double dtTs         = pos.ts() - pLatestPos->ts();
            const double dist_m       = pLatestPos->dist(pos);
            const double gs_kt        = (dtTs > 0)
                                      ? pLatestPos->speed_kt(pos)
                                      : NAN;
            const bool   isStationary = bothOnGround &&
                                        !std::isnan(gs_kt) &&
                                        gs_kt <= GND_STATIONARY_GS_KT;

            // ----- TEMPORARY GROUND DIAGNOSTIC LOGGING (tag: GND_DIAG_ADD) -
            // Unconditional, fires once per on-ground feed update.
            // Captures the parameters used by the stationary / holding
            // decision so thresholds can be tuned from real data. Search
            // the log for "GND_DIAG_ADD" to see only these lines.
            // To remove later: delete this block.
            if (bothOnGround) {
                LOG_MSG(logDEBUG,
                        "GND_DIAG_ADD %s ts=%.1f dt=%.2fs dist=%.2fm gs=%.2fkt"
                        " hdg_prev=%.1f hdg_in=%.1f holdingSince=%.1f holding=%d",
                        key().c_str(), pos.ts(), dtTs, dist_m, gs_kt,
                        pLatestPos->heading(), pos.heading(),
                        groundHoldingSinceTs, bGroundHolding ? 1 : 0);
            }

            if (isStationary) {
                // Stationary slot: reset the "consecutive non-stationary"
                // counter — we just saw a moving slot streak interrupted.
                groundNonStationaryCnt = 0;

                // Either continue an existing streak or start a fresh one.
                // The streak start is the timestamp of the LATEST already-
                // known position so the elapsed time below is "how long has
                // the aircraft been frozen at this point in space".
                if (groundHoldingSinceTs <= 0.0)
                    groundHoldingSinceTs = pLatestPos->ts();

                // Promote to holding once the streak exceeds the timeout.
                // Threshold lives in `Constants.h` (`GND_HOLDING_TIMEOUT_S`).
                if (!bGroundHolding &&
                    (pos.ts() - groundHoldingSinceTs) >= GND_HOLDING_TIMEOUT_S)
                {
                    bGroundHolding = true;
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_HOLDIN %s entering ground-holding"
                            " (stationary for %.1fs)",
                            key().c_str(),
                            pos.ts() - groundHoldingSinceTs);

                    // ------------------------------------------------------
                    // Third gate-detection path (apt.dat startup-locations).
                    //
                    // Background: `bGateParked` previously had two sources —
                    // SPOS_STARTUP slots (from RT's parked-traffic snapshot
                    // or the apt.dat snap path), and the indiscriminate
                    // bGroundHolding flag itself (set at line ~2697 below).
                    // The second source is too permissive: ANY extended
                    // stationary period flags the aircraft as gate-parked,
                    // including runway hold-shorts, taxi pauses, and
                    // maintenance pads. The pushback state machine then
                    // false-positives forward taxi as a push and renders
                    // the aircraft tail-first (visible spinning).
                    //
                    // Fix: at the moment we promote to holding, run a one-
                    // shot apt.dat lookup. If a startup-location (gate /
                    // stand / ramp) is within GATE_DETECT_MAX_DIST_M of the
                    // held position, the aircraft is really at a gate.
                    // Set bGateParked = true here; do NOT rely on the
                    // permissive bGroundHolding-only path below for live-
                    // tracked aircraft. SPOS_STARTUP slots still set the
                    // flag through the existing path independently.
                    //
                    // Why only at the flip moment: the lookup is O(stands-
                    // per-airport) on a mutex-guarded structure. Running it
                    // on every subsequent stationary slot is wasteful, and
                    // the answer cannot change for a non-moving aircraft.
                    // If LTApt is not yet available at this exact moment
                    // (asynchronous reload, far-from-camera airport),
                    // LTAptFindStartupLoc returns an empty positionTy and
                    // bGateParked stays false — acceptable, because the
                    // aircraft was likely not visible to the user either.
                    // The lookup is performed twice: once with the tight
                    // GATE_DETECT_MAX_DIST_M threshold (the actual decision
                    // gate), and once with a much larger probe radius
                    // (10×) so the diagnostic log can report the distance
                    // to the *nearest* startup-loc even when the tight
                    // gate rejects it. That way a log review immediately
                    // shows "we missed AAL1408 because the closest
                    // startup-loc was 47 m away" versus "no apt.dat
                    // startup-locs at this airport at all" — two very
                    // different failure modes.
                    // Lookups are performed twice: once with the tight
                    // GATE_DETECT_MAX_DIST_M threshold (the actual gate),
                    // once with a 10× probe radius so the diagnostic can
                    // report the distance to the nearest startup-loc even
                    // when the tight check rejects it.
                    //
                    // IMPORTANT: success/failure is determined by the
                    // `outDist` parameter, NOT by `positionTy::isNormal()`
                    // on the returned position. The returned positionTy
                    // has `ts=NaN, alt=NaN` (gates have no inherent time
                    // or altitude — the caller supplies those), and
                    // `isNormal()` requires both to be set. Using
                    // `isNormal()` as the success signal silently rejects
                    // EVERY valid startup-loc match. `outDist` is set to
                    // a finite distance only when a match was found
                    // within the search radius (see `FindStartupLoc` at
                    // `Src/LTApt.cpp:1814`), so it is the right gate.
                    const bool aptAvail = LTAptAvailable();
                    double gateDist  = NAN;
                    const positionTy gatePos =
                        LTAptFindStartupLoc(pos, GATE_DETECT_MAX_DIST_M,
                                            &gateDist);
                    if (!std::isnan(gateDist) &&
                        gateDist <= GATE_DETECT_MAX_DIST_M)
                    {
                        bGateParked = true;
                        LOG_MSG(logDEBUG,
                                "GND_DIAG_GATE %s HIT lat=%.6f lon=%.6f"
                                " startup-loc at %.1fm (hdg=%.1f)"
                                " — bGateParked=true",
                                key().c_str(),
                                pos.lat(), pos.lon(),
                                gateDist, gatePos.heading());
                    } else {
                        // Tight lookup failed. Probe a wide radius and
                        // log the position so the failure mode can be
                        // distinguished (apt unavailable / no airport /
                        // closest startup-loc just outside threshold).
                        double probeDist  = NAN;
                        (void)LTAptFindStartupLoc(pos,
                                                  GATE_DETECT_MAX_DIST_M * 10.0,
                                                  &probeDist);
                        const char* mode;
                        if (!aptAvail)                  mode = "APT_UNAVAIL";
                        else if (!std::isnan(probeDist)) mode = "NEAR";
                        else                            mode = "NOAPT";
                        LOG_MSG(logDEBUG,
                                "GND_DIAG_GATE %s %s lat=%.6f lon=%.6f"
                                " (tight %.0fm, probe %.0fm: nearest=%.1fm)"
                                " — bGateParked stays false",
                                key().c_str(), mode,
                                pos.lat(), pos.lon(),
                                GATE_DETECT_MAX_DIST_M,
                                GATE_DETECT_MAX_DIST_M * 10.0,
                                std::isnan(probeDist) ? -1.0 : probeDist);
                    }
                }

                // While in holding, drop trivial jitter outright. We still
                // allow through anything that moves more than the trivial
                // distance, because that may signal a genuine push-back or
                // taxi start that we must not miss.
                //
                // EXCEPTION: never drop a position flagged SPOS_STARTUP.
                // Those are not feed jitter — they are *intentional*
                // placements: the RealTraffic parked-feed bootstrap seeds
                // (4 identical positions used to bring a parked aircraft
                // into existence) and the Synthetic channel's keep-alive
                // re-feeds (which hold an adopted parked aircraft alive).
                // Both arrive at dist≈0 from the held position, so the
                // plain trivial-drop would eat them — starving the parked
                // aircraft of the very positions it needs to exist and to
                // persist, which is exactly the "no parked traffic at all"
                // symptom. Raw jittery LIVE-feed positions are NOT
                // SPOS_STARTUP at this point (taxiway snapping runs later
                // in the pipeline), so genuine jitter is still suppressed.
                if (bGroundHolding && dist_m < GND_HOLDING_TRIVIAL_DIST_M &&
                    pos.f.specialPos != SPOS_STARTUP)
                {
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_DROP %s dropping trivial update"
                            " (dist=%.2fm, gs=%.2fkt)",
                            key().c_str(), dist_m, gs_kt);
                    // Update `youngestTS` to the dropped slot's feed ts.
                    // Without this, an aircraft that sits at the gate
                    // receiving valid feed updates (all trivial-dropped)
                    // looks "stale" to the outdate check at the bottom
                    // of CalcNextPos — `youngestTS + GetAcOutdatedIntvl()
                    // < simTime` fires after ~180 s and the aircraft is
                    // removed even though the feed is alive. We keep the
                    // deque content unchanged (the drop is the whole
                    // point) but advance the freshness timestamp so the
                    // outdate check sees the aircraft as live.
                    if (pos.ts() > youngestTS)
                        youngestTS = pos.ts();
                    return;
                }
            } else {
                // Non-stationary slot: increment the consecutive counter.
                // We do not exit holding on the first one — feed jitter can
                // briefly produce a single 2 kt sample for a truly parked
                // aircraft. Only after `GND_HOLDING_EXIT_CONSEC` consecutive
                // non-stationary slots do we trust that the aircraft is
                // really moving and break the suppression.
                groundNonStationaryCnt++;
                if (bGroundHolding &&
                    groundNonStationaryCnt >= GND_HOLDING_EXIT_CONSEC)
                {
                    bGroundHolding = false;
                    // Reset the streak start to "now" so that if motion
                    // ceases again immediately, the next holding promotion
                    // is timed from the resumption of stationarity (not
                    // from the moment the original streak began long ago).
                    groundHoldingSinceTs = pos.ts();
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_HOLDOUT %s exiting ground-holding"
                            " (consec=%d, dist=%.2fm, gs=%.2fkt)",
                            key().c_str(), groundNonStationaryCnt,
                            dist_m, gs_kt);
                }
            }

            // ----------------------------------------------------------
            // Gate-parked motion suppression — DISTANCE-based.
            //
            // For an aircraft we have positively identified as parked at
            // a gate (`bGateParked == true`), suppress any slot whose
            // displacement from the latest accepted position is less than
            // `GATE_HOLD_MIN_ACCEPT_M` (30 m). Slots that exceed the
            // threshold are taken as evidence of real motion, are
            // accepted into the deque, and trigger an immediate release
            // of `bGroundHolding` so subsequent (now much smaller)
            // per-slot deltas during the push are not trivial-dropped.
            //
            // Why distance and not a counter of non-stationary slots:
            // real pushbacks roll at 0.4-1.4 kt — below
            // `GND_STATIONARY_GS_KT` (1.5 kt). The non-stationary
            // counter therefore never advances during a slow push, and
            // a counter-based gate would suppress the entire push
            // (observed with AAL1408: every slot dropped, aircraft never
            // rendered any motion and was eventually outdated and
            // removed). Distance-based gating succeeds the moment the
            // aircraft has moved far enough from the gate to rule out
            // noise — typically 2-3 slots into a real push.
            //
            // For the noise case (AAL2501, AAL2761 — single or paired
            // ~18 m anomalies that return to the gate), every individual
            // slot sits inside the 30 m envelope and is correctly
            // dropped; the held position never advances.
            //
            // `pbState == PB_NONE` is essential: once the state machine
            // has entered PB_ACTIVE/PB_PAUSED we are committed to the
            // push and must let every slot through (including pause
            // slots that would otherwise be filtered).
            //
            // `SPOS_STARTUP` exempt — same rationale as the trivial-drop
            // above (intentional placements).
            if (bGateParked && pbState == PB_NONE &&
                pos.f.specialPos != SPOS_STARTUP)
            {
                if (dist_m < GATE_HOLD_MIN_ACCEPT_M) {
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_GATE_HOLD %s dropping motion at gate"
                            " (dist=%.2fm/%.0fm, gs=%.2fkt, isStat=%d)",
                            key().c_str(), dist_m,
                            GATE_HOLD_MIN_ACCEPT_M, gs_kt,
                            isStationary ? 1 : 0);
                    // Advance freshness timestamp even on drop — see the
                    // matching update in the trivial-drop branch above.
                    if (pos.ts() > youngestTS)
                        youngestTS = pos.ts();
                    return;
                }
                // Slot is far enough from the held position to be real
                // motion. Accept it AND release bGroundHolding so the
                // subsequent in-push slots (which are typically only
                // 3-6 m from the previous accepted slot, and would
                // therefore be trivial-dropped if holding stayed true)
                // can flow through and continue the rendered push.
                if (bGroundHolding) {
                    bGroundHolding = false;
                    groundHoldingSinceTs = pos.ts();
                    LOG_MSG(logDEBUG,
                            "GND_DIAG_GATE_RELEASE %s accepting motion at"
                            " gate (dist=%.2fm >= %.0fm, gs=%.2fkt) —"
                            " bGroundHolding cleared",
                            key().c_str(), dist_m,
                            GATE_HOLD_MIN_ACCEPT_M, gs_kt);
                }
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
                LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unkown)");
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

            // Pushback gate signal. `bGateParked` is the persistent
            // assertion that THIS aircraft is currently parked at a real
            // gate (as opposed to merely "stationary on the airport
            // surface"). Three sources set it true:
            //
            //   (a) The slot was placed at an apt.dat startup location
            //       (SPOS_STARTUP) — from RT's parked-traffic snapshot
            //       or from the Synthetic channel's keep-alive seeds.
            //       Handled here, per-slot, because each incoming RT
            //       parked-feed re-fetch lands a fresh SPOS_STARTUP
            //       position and must (re-)assert the flag.
            //
            //   (b) The aircraft entered ground-holding AND apt.dat
            //       confirms a startup-location within
            //       GATE_DETECT_MAX_DIST_M of the held position.
            //       Handled above at the bGroundHolding-flip site —
            //       one-shot lookup, not per-slot. This is the third
            //       path described in `GATE_DETECT_MAX_DIST_M`'s
            //       comment in Constants.h. It catches live-tracked
            //       aircraft that were never in RT's parked snapshot
            //       (UAL466, AAL1771 etc.) without false-positiving
            //       runway hold-shorts as gates.
            //
            //   (c) Implicit: a previously-set bGateParked persists
            //       until the pushback state machine in CalcHeading
            //       exits to PB_NONE (push complete) or the aircraft
            //       goes airborne.
            //
            // The OLD code also set bGateParked from `bGroundHolding`
            // alone — that was too permissive (any stationary period
            // anywhere on the airport flagged a "gate") and produced
            // the visible spin when the pushback state machine then
            // mistook a forward taxi resumption for a push. The
            // apt.dat-confirmed path above replaces it.
            if (pos.f.specialPos == SPOS_STARTUP)
                bGateParked = true;

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
            const LTChannel* pLstChn = last.pChannel;           // last is going to become invalid, save the ptr for the log message
            if (inDyn.pChannel && pLstChn != inDyn.pChannel)
            {
                // Firstly, if no a/c yet created, then we prioritize with
                // which channel an a/c is created. The higher the channel
                // number the better.
                if (!hasAc()) {
                    if (pLstChn && inDyn.pChannel->GetChannel() <= pLstChn->GetChannel())
                        // lower prio -> ignore data
                        return;
                }
                // has already an aircraft
                else {
                    // Synthetic channels are to be kicked out as soon as any other channel has data,
                    // ie. only for other types we still consider skipping the data:
                    if (pLstChn && pLstChn->GetChType() != LTChannel::CHT_SYNTHETIC_DATA)
                    {
                        // If there still are position to be processed we don't switch channel
                        if (!posDeque.empty())
                            return;
                        
                        // new position must be significantly _after_ current 'to' pos
                        // so that current channel _really_ had its chance to sent an update:
                        const double tsCutOff = GetYoungestTS() + dataRefs.GetFdRefreshIntvl()*3/2;
                        if (inDyn.ts < tsCutOff)
                            return;
                    }
                }

                // We accept the channel switch...clear out any old channel's data
                // so we throw away the lower prio channel's data
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
                // add to list
                dynDataDeque.emplace_back(inDyn);
                // Potentially upgrade to Mode S transponder usage based on plane size
                DetermineTransponderMode(dynDataDeque.back().radar.mode);
                // and keep sorted
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


// In case of "larger" aircraft, upgrade to use Mode S
// returns if the value has been modified
bool LTFlightData::DetermineTransponderMode (XPMPTransponderMode& mode)
{
    // The only modes we potentially 'upgrade' is A and C:
    if (mode == xpmpTransponderMode_ModeA ||
        mode == xpmpTransponderMode_ModeC)
    {
        // Consider if this is a 'larger' aircraft based on
        // wake turbulence category being 'M' or more
        if (statData.pDoc8643 && statData.pDoc8643->GetWakeCat() >= 1) {
            mode = xpmpTransponderMode_ModeS_TAOnly;
            return true;
        }
    }

    // nothing changed
    return false;
}


//
//MARK: Flight Data - Mutex-Controlled access Static
//

// update static data
void LTFlightData::UpdateData (const LTFlightData::FDStaticData& inStat,
                               double distance,
                               DatRequTy masterDataType)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
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
        if (statData.merge(inStat, masterDataType))
            bMdlInfoChange = true;
        
        // Re-determine a/c model (only if it was determined before:
        // the very first determination shall be made as late as possible
        // in LTFlightData::CreateAircraft())
        if (pAc && DetermineAcModel())
            bMdlInfoChange = true;

        // Now that our data is updated:
        // If this call is not with data from a maste data channel we can think about
        // asking a master data channel for more details
        if (masterDataType == DATREQU_NONE) {
            // A/c master data: Not yet requested master data and
            //                  critical elements missing?
            if (!statData.bDataMaster && !statData.hasMdlMatchInfo()) {
                LTACMasterdataChannel::RequestMasterData(key(), distance);
            }
            // Route Info missing?
            if (!statData.bDataRoute && !statData.hasRouteInfo()) {
                LTACMasterdataChannel::RequestRouteInfo(key(), statData.call, distance);
            }
        }
        
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
        const double ourDist = CoordDistance(dataRefs.GetViewPos(), posDeque.front());
        double farestDist = ourDist * 1.1;      // add 10% to avoid quick switching back-and-forth between planes at the outer edge
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
        LOG_MSG(logDEBUG, "Removing %s as it is the most distant a/c at %.0fm"
                " to make room for %s at %.0fm",
                pFarestAc->keyDbg().c_str(), farestDist,
                keyDbg().c_str(), ourDist);
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
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
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

/// Return aircraft with given key (optionally: if it has an active aircraft)
LTFlightData* mapFdAc (const LTFlightData::FDKeyTy& key,
                       bool bMustHaveAc)
{
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
    try {
        LTFlightData& fd = mapFd.at(key);
        if (!bMustHaveAc || fd.hasAc())
            return &fd;
    }
    // not found
    catch (...)
    {}
    return nullptr;
}
