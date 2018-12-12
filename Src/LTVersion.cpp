//
//  LTVersion.cpp
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

#include "LiveTraffic.h"

//
// MARK: Version Information (CHANGE VERSION HERE)
//
const float VERSION_NR = 0.83f;
const bool VERSION_BETA = true;

//
// MARK: global variables referred to via extern declarations in Constants.h
//
char LT_VERSION[10] = "";
char LT_VERSION_FULL[30] = "";
char HTTP_USER_AGENT[50] = "";

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
    
    struct tm tm = {0, 0, 0, 0, 0, 0, 0, 0};
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
    // Limit is: build date plus 30 days
    LT_BETA_VER_LIMIT = mktime(&tm) + 30 * SEC_per_D;
    localtime_s(&tm, &LT_BETA_VER_LIMIT);
    
    // tell the world we're limited
    strftime(LT_BETA_VER_LIMIT_TXT,sizeof(LT_BETA_VER_LIMIT_TXT),"%d-%b-%Y",&tm);
    // still within limit time frame?
    if (time(NULL) > LT_BETA_VER_LIMIT) {
        LOG_MSG(logFATAL, BETA_LIMITED_EXPIRED, LT_BETA_VER_LIMIT_TXT);
        return false;
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
    snprintf(LT_VERSION, sizeof(LT_VERSION), "%0.2f", VERSION_NR);
    snprintf(HTTP_USER_AGENT, sizeof(HTTP_USER_AGENT), "%s/%s", LIVE_TRAFFIC, LT_VERSION);
    
    // Example of __DATE__ string: "Nov 12 2018"
    //                              01234567890
    char buildDate[12] = __DATE__;
    buildDate[3]=0;                                     // separate elements
    buildDate[6]=0;
    
    // if day's first digit is space make it a '0', ie. change ' 5' to '05'
    if (buildDate[4] == ' ')
        buildDate[4] = '0';
    
    snprintf(LT_VERSION_FULL, sizeof(LT_VERSION_FULL), "%s.%s%s%s",
             LT_VERSION,
             // year (last 2 digits)
             buildDate + 9,
             // month converted to digits
             strcmp(buildDate,"Jan") == 0 ? "01" :
             strcmp(buildDate,"Feb") == 0 ? "02" :
             strcmp(buildDate,"Mar") == 0 ? "03" :
             strcmp(buildDate,"Apr") == 0 ? "04" :
             strcmp(buildDate,"May") == 0 ? "05" :
             strcmp(buildDate,"Jun") == 0 ? "06" :
             strcmp(buildDate,"Jul") == 0 ? "07" :
             strcmp(buildDate,"Aug") == 0 ? "08" :
             strcmp(buildDate,"Sep") == 0 ? "09" :
             strcmp(buildDate,"Oct") == 0 ? "10" :
             strcmp(buildDate,"Nov") == 0 ? "11" :
             strcmp(buildDate,"Dec") == 0 ? "12" : "??",
             // day
             buildDate + 4
           );
    
    if (VERSION_BETA && !CalcBetaVerTimeLimit())
        return false;

    return true;
}
