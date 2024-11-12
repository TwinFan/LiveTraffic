/// @file       LTSayIntentions.h
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

#ifndef LTSayIntentions_h
#define LTSayIntentions_h

#include "LTChannel.h"

//
//MARK: SayIntentions Constants
//
#define SI_CHECK_NAME           "SayIntentions Flight Tracker"
#define SI_CHECK_URL            "https://tracker.sayintentions.ai"
#define SI_CHECK_POPUP          "Check SayIntentions' coverage"

#define SI_NAME                 "SayIntentions"
#define SI_URL_ALL              "https://lambda.sayintentions.ai/tracker/map"

//
// MARK: SayIntentions connection class
//

/// Connection to SayIntentions
class SayIntentionsConnection : public LTFlightDataChannel
{
public:
    SayIntentionsConnection ();                             ///< Constructor
    std::string GetURL (const positionTy& pos) override;    ///< returns the constant URL to SayIntentions traffic
    bool ProcessFetchedData () override;                    ///< Process response, selecting traffic around us and forwarding to the processing queues
protected:
    void Main () override;                                  ///< virtual thread main function
};

#endif /* LTSayIntentions_h */
