/// @file       LTVersion.cpp
/// @brief      Returns current version, checks online for updates
/// @details    Return current version as text\n
///             Query latest version from X-Plane forum's download page to check for updates\n
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
// MARK: global variables referred to via extern declarations in Constants.h
//
char LT_VERSION[10] = "";
char LT_VERSION_FULL[30] = "";
int verBuildDate = 0;
char HTTP_USER_AGENT[50] = "";

// version availble on X-Plane.org
unsigned verXPlaneOrg = 0;

// BETA versions are limited for 30 days...people shall use release versions!
time_t LT_BETA_VER_LIMIT = 0;
char LT_BETA_VER_LIMIT_TXT[12] = "";

bool CalcBetaVerTimeLimit()
{
    // Example of __DATE__ string: "Nov 12 2018"
    //                              01234567890
    char buildDate[12] = __DATE__;
    buildDate[3]=0;                                     // separate elements
    buildDate[6]=0;
    
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_mday = atoi(buildDate+4);
    tm.tm_year = atoi(buildDate+7) - 1900;
    tm.tm_mon = strcmp(buildDate,"Jan") == 0 ?  0 :
                strcmp(buildDate,"Feb") == 0 ?  1 :
                strcmp(buildDate,"Mar") == 0 ?  2 :
                strcmp(buildDate,"Apr") == 0 ?  3 :
                strcmp(buildDate,"May") == 0 ?  4 :
                strcmp(buildDate,"Jun") == 0 ?  5 :
                strcmp(buildDate,"Jul") == 0 ?  6 :
                strcmp(buildDate,"Aug") == 0 ?  7 :
                strcmp(buildDate,"Sep") == 0 ?  8 :
                strcmp(buildDate,"Oct") == 0 ?  9 :
                strcmp(buildDate,"Nov") == 0 ? 10 : 11;
    // Save the build date in a form to be offered via dataRef, like 20200430 for 30-APR-2020
    verBuildDate = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;

    if constexpr (LIVETRAFFIC_VERSION_BETA) {
        // Limit is: build date plus 30 days
        LT_BETA_VER_LIMIT = mktime(&tm) + 30 * SEC_per_D;
        localtime_s(&tm, &LT_BETA_VER_LIMIT);

        // tell the world we're limited
        strftime(LT_BETA_VER_LIMIT_TXT, sizeof(LT_BETA_VER_LIMIT_TXT), "%d-%b-%Y", &tm);
        // still within limit time frame?
        if (time(NULL) > LT_BETA_VER_LIMIT) {
            LOG_MSG(logFATAL, BETA_LIMITED_EXPIRED, LT_BETA_VER_LIMIT_TXT);
            return false;
        }
    }

    return true;
}

//
// As __DATE__ has weird format
// we fill the internal buffer once...need to rely on being called, though
//

bool InitFullVersion ()
{
    // fill char arrays
    snprintf(LT_VERSION, sizeof(LT_VERSION), "%u.%u.%u",
             LIVETRAFFIC_VER_MAJOR, LIVETRAFFIC_VER_MINOR, LIVETRAFFIC_VER_PATCH);
    snprintf(HTTP_USER_AGENT, sizeof(HTTP_USER_AGENT), "%s/%s", LIVE_TRAFFIC, LT_VERSION);
    
    // Example of __DATE__ string: "Nov 12 2018"
    //                              01234567890
    char buildDate[12] = __DATE__;
    buildDate[3]=0;                                     // separate elements
    buildDate[6]=0;
    
    // if day's first digit is space make it a '0', ie. change ' 5' to '05'
    if (buildDate[4] == ' ')
        buildDate[4] = '0';
    
    snprintf(LT_VERSION_FULL, sizeof(LT_VERSION_FULL), "%s (%s-%s-%s)",
             LT_VERSION,
             // day
             buildDate + 4,
             // month
             buildDate,
             // year
             buildDate + 7
           );
    
    // tell the world we are trying to start up
    LOG_MSG(logMSG, MSG_STARTUP, LT_VERSION_FULL);

    // in case of a BETA version this is the place to check for its time limit
    if (!CalcBetaVerTimeLimit())
        return false;

    return true;
}

// LiveTraffic's version number as pure integer for returning in a dataRef, like 201 for v2.01
int GetLTVerNum(void*)
{
    return (int)LT_VER_NO;
}

/// LiveTraffic's build date as pure integer for returning in a dataRef, like 20200430 for 30-APR-2020
int GetLTVerDate(void*)
{
    return verBuildDate;
}

//
// MARK: Fetch X-Plane.org's version
//

// Using CURL, we simply download from LT_DOWNLOAD_URL, looking for softwareVersion
// Example:
//      "softwareVersion": "0.93.190224"
// The tricky part is that the chunks returned by CURL may break delivery
// of that text inbetween, that's why we need to buffer a bit
size_t FetchVersionCB(char *ptr, size_t, size_t nmemb, void* userdata)
{
    constexpr size_t bufSizeToKeep = 100;
    
    // Have we seen the version number already? Then just return
    if (verXPlaneOrg > 0)
        return nmemb;
    
    // copy buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    
    // quick search first
    if (readBuf.find("\"softwareVersion\":") != std::string::npos) {
        // now the more expensive regex search
        // for the version number in the buffer
        std::regex re_ver("\"softwareVersion\": \"(\\d+)\\.(\\d+)\\.(\\d+)\"");
        std::smatch m;
        std::regex_search(readBuf, m, re_ver);
        
        // 3 matches expected
        if (m.size() == 4) {
            int major = std::stoi(m[1]), minor = std::stoi(m[2]), patch = std::stoi(m[3]);
            verXPlaneOrg = unsigned(10000*major + 100*minor + patch);
        }
    }
    
    // We don't need to keep all the buffer,
    // reduce it to some reasonable size
    if (readBuf.size() > bufSizeToKeep)
        readBuf.erase(0, readBuf.size() - bufSizeToKeep);
    
    // all consumed
    return nmemb;
}

// check on X-Plane.org what version's available there
// This function would block. Idea is to call it in a thread like with std::async
bool FetchXPlaneOrgVersion ()
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_Version");

    char curl_errtxt[CURL_ERROR_SIZE];
    std::string readBuf;
    
    // initialize the CURL handle
    CURL *pCurl = curl_easy_init();
    if (!pCurl) {
        LOG_MSG(logERR,ERR_CURL_EASY_INIT);
        return false;
    }
    
    // prepare the handle with the right options
    verXPlaneOrg = 0;
    readBuf.reserve(CURL_MAX_WRITE_SIZE);
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeout());
    curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, FetchVersionCB);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_URL, LT_DOWNLOAD_URL);

    // perform the HTTP get request
    CURLcode cc = CURLE_OK;
    if ( (cc=curl_easy_perform(pCurl)) != CURLE_OK )
    {
        // problem with querying revocation list?
        if (LTOnlineChannel::IsRevocationError(curl_errtxt)) {
            // try not to query revoke list
            curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
            LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, LT_DOWNLOAD_CH);
            // and just give it another try
            cc = curl_easy_perform(pCurl);
        }
        
        // if (still) error, then log error
        if (cc != CURLE_OK)
            LOG_MSG(logERR, ERR_CURL_NOVERCHECK, cc, curl_errtxt);
    }
    
    if (cc == CURLE_OK)
    {
        // CURL was OK, now check HTTP response code
        long httpResponse = 0;
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
        
        // not HTTP_OK?
        if (httpResponse != HTTP_OK)
            LOG_MSG(logERR, ERR_CURL_NOVERCHECK, (int)httpResponse, ERR_HTTP_NOT_OK)
        else if (!verXPlaneOrg)
            // all OK but still no version number?
            LOG_MSG(logERR, ERR_CURL_NOVERCHECK, -1, ERR_FOUND_NO_VER_INFO)
    }
    
    // cleanup CURL handle
    curl_easy_cleanup(pCurl);
    
    // return if we found something
    return verXPlaneOrg > 0;
}
