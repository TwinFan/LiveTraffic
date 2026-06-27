/// @file       LTMsgPack.h
/// @brief      Minimalistic and incomplete implementation of the MessagePack format
/// @details    The implementation focuses on de-serialization
///             as far as needed for LiveTraffic, especially Navigraph.
/// @see        https://github.com/msgpack/msgpack/blob/master/spec.md
/// @author     Birger Hoppe
/// @copyright  (c) 2026 Birger Hoppe
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

/// @brief Object represents a Parsing context
/// @details The reading function may assume the next object is of a certain
///          type. If it is not the reading point is not moved and an
///          LTError exception is thrown.
class MsgPack
{
protected:
    const uint8_t* const data;          ///< the data buffer with the serialized MessagPack data
    const size_t         len;           ///< length of data buffer
    size_t               pos = 0;       ///< current reading position
    
public:
    /// Initializes the parsing context
    MsgPack (const uint8_t* _data, size_t _len) : data(_data), len(_len) {}
    
    /// Assumes an array, returns its size
    size_t GetArraySize ();
    
    /// Assumes a map, returns the number of key/value pairs
    size_t GetMapSize ();
    
    /// Assumes a string and returns it
    std::string GetString ();
    
    /// Assumes an integer and returns it (will throw exception for a float!)
    long long GetLong ();
    
    /// Assumes a float or an int and returns it
    double GetDouble ();
    
    /// Assumes a boolean and returns it
    bool GetBool ();
    
    /// Ignores the next values and moves on
    void Skip ();
    
protected:
    /// Process and return an 8 bit size from pos+1, pos is incremented
    size_t GetSize8 (const char* szType);
    /// Process and return a 16 bit size from pos+1, pos is incremented
    size_t GetSize16 (const char* szType);
    /// Process and return a 32 bit size from pos+1, pos is incremented
    size_t GetSize32 (const char* szType);
};
