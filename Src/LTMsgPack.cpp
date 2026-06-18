/// @file       LTMsgPack.cpp
/// @brief      Minimalistic and incomplete implementation of the MessagePack format
/// @details    The implementation focuses on de-serialization
///             as far as needed for LiveTraffic, especially Navigraph.
///             Throws LTError in case of errors like unexpected types.
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

#include "LiveTraffic.h"

/// Already reached end-of-data?
#define TEST_EOD \
if (pos >= len) { THROW_ERROR(logERR, "Reached end of data already"); }

/// Has enough bytes left for data type?
#define HAS_DATA(num,type) \
if (pos+(num) > len) { THROW_ERROR(logERR, "MsgPack too short for %s", type); }

/// Last statement in functions that assume a type: Throws exception on unexpected token
#define UNEXPECTED(expected) \
THROW_ERROR(logERR, "Expected " expected " but found %#hhx at position %zu", data[pos], pos)

/// Assumes an array, returns its size
size_t MsgPack::GetArraySize ()
{
    TEST_EOD;
    if (0x90 <= data[pos] && data[pos] <= 0x9f)         // fixarray
        return size_t(data[pos++] - 0x90);
    if (data[pos] == 0xdc)                              // Array 16
        return GetSize16("Array16");
    if (data[pos] == 0xdd)                              // Array 32
        return GetSize32("Array32");
    UNEXPECTED("Array");
}

/// Assumes a map, returns the number of key/value pairs
size_t MsgPack::GetMapSize ()
{
    TEST_EOD;
    if (0x80 <= data[pos] && data[pos] <= 0x8f) {       // fixmap
        return size_t(data[pos++] - 0x80);
    }
    if (data[pos] == 0xde)                              // Map 16
        return GetSize16("Map16");
    if (data[pos] == 0xdf)                              // Map 32
        return GetSize32("Map32");
    UNEXPECTED("Map");
}

/// Assumes a string and returns it
std::string MsgPack::GetString ()
{
    TEST_EOD;
    if (data[pos] == 0xc0) { ++pos; return ""; }        // Nil

    // Determine String length first
    size_t size = 0;
    if (0xa0 <= data[pos] && data[pos] <= 0xbf)         // fixstr
        size = size_t(data[pos++]) - 0xa0;
    else if (data[pos] == 0xd9)                         // str8
        size = GetSize8("Str8");
    else if (data[pos] == 0xda)                         // str16
        size = GetSize16("Str16");
    else if (data[pos] == 0xdb)                         // str32
        size = GetSize32("Str32");
    else {
        UNEXPECTED("String");
    }
    
    // Enough data to read the actual string?
    HAS_DATA(size, "String of given size");
    
    // return the string
    const char* ret = reinterpret_cast<const char*>(data+pos);
    pos += size;
    return std::string(ret, size);
}

/// Assumes an integer and returns it
long long MsgPack::GetLong ()
{
    TEST_EOD;
    size_t bytes = 0;
    bool bUnsigned = true;                              // unsigned?
    
    if (data[pos] == 0xc0) { ++pos; return 0; }         // Nil
    
    if (data[pos] <= 0x7f)                              // fixint
        return long(unsigned(data[pos++]));
    if (data[pos] >= 0xe0)                              // negative fixint
        return *reinterpret_cast<const int8_t*>(data+(pos++));
    
    switch (data[pos]) {
        case 0xcc: bytes=1; break;                      // uint 8
        case 0xcd: bytes=2; break;                      // uint 16
        case 0xce: bytes=4; break;                      // uint 32
        case 0xcf: bytes=8; break;                      // uint 64
        case 0xd0: bytes=1; bUnsigned = false; break;   // int 8
        case 0xd1: bytes=2; bUnsigned = false; break;   // int 16
        case 0xd2: bytes=4; bUnsigned = false; break;   // int 32
        case 0xd3: bytes=8; bUnsigned = false; break;   // int 64
        default:
            UNEXPECTED("Integer");
    }
    ++pos;                                              // eat the type info
    
    HAS_DATA(bytes, "integer");                         // enough room for the actual number?
    pos += bytes;                                       // move the pointer already beyond the long

    uint64_t ret = 0;
    uint8_t* out = reinterpret_cast<uint8_t*>(&ret);    // points to first byte of ret (least significant)
    const uint8_t* in = data + pos - 1;                 // points to last byte of data (also least significant because it is big endian)
    for (size_t i = 0; i < bytes; ++i)                  // copy the bytes in reverse oder
        *(out++) = *(in--);
    
    if (bUnsigned)
        return (long long)ret;
    else {
        switch (bytes) {
            case 1: return *reinterpret_cast<int8_t*>(&ret);
            case 2: return *reinterpret_cast<int16_t*>(&ret);
            case 4: return *reinterpret_cast<int32_t*>(&ret);
            case 8: return *reinterpret_cast<int64_t*>(&ret);
        }
    }
    THROW_ERROR(logERR, "Shouldn't get here! pos = %zu, bytes = %zu",
                pos, bytes);
}


/// Assume a float or an int and returns it
double MsgPack::GetDouble ()
{
    size_t bytes = 0;
    TEST_EOD;
    if (data[pos] == 0xc0) { ++pos; return NAN; }       // Nil
    
    switch (data[pos]) {
        case 0xca: bytes=4; break;                      // float 32
        case 0xcb: bytes=8; break;                      // float 64 (double)
        default:
            return double(GetLong());                   // try integer, can still throw exception
    }
    
    ++pos;                                              // eat the type info
    HAS_DATA(bytes, "float");                           // enough room for the actual number?
    pos += bytes;                                       // move the pointer already beyond the float
    
    const bool bSingle = bytes == 4;
    float fret = 0.0f;
    double dret = 0.0;
    uint8_t* out =
        bSingle ? reinterpret_cast<uint8_t*>(&fret) :   // points to first byte of float (least significant)
                  reinterpret_cast<uint8_t*>(&dret);    // points to first byte of double (least significant)
    const uint8_t* in = data + pos - 1;                 // points to last byte of data (also least significant because it is big endian)
    for (; bytes>0; --bytes)                            // copy the bytes in reverse oder
        *(out++) = *(in--);
    
    return bSingle ? double(fret) : dret;
}


/// Assumes a boolean and returns it
bool MsgPack::GetBool ()
{
    TEST_EOD;
    if (data[pos] == 0xc0) { ++pos; return false; }     // Nil
    if (data[pos] == 0xc2) { ++pos; return false; }     // False
    if (data[pos] == 0xc3) { ++pos; return true;  }     // True
    UNEXPECTED("Boolean");
}


/// Ignores the next values and moves on
void MsgPack::Skip ()
{
    size_t bytes = 0;
    const uint8_t v = data[pos];
    if (v <= 0x7f)          bytes = 1;                  // fixint
    else if (v >= 0xe0)     bytes = 1;                  // negative fixint
    else if (0xa0 <= v && v <= 0xbf) { GetString(); return; }   // fixstr
    else switch (v) {
        case 0xc0:                                      // nil
        case 0xc2:                                      // false
        case 0xc3:                                      // true
            bytes = 1;
            break;
            
        case 0xc4:                                      // bin 8
        case 0xcc:                                      // uint 8
        case 0xd0:                                      // int 8
            bytes = 2;
            break;
            
        case 0xc5:                                      // bin 16
        case 0xcd:                                      // uint 16
        case 0xd1:                                      // int 16
            bytes = 3;
            break;
            
        case 0xc6:                                      // bin 32
        case 0xce:                                      // uint 32
        case 0xd2:                                      // int 32
        case 0xca:                                      // float 32
            bytes = 5;
            break;
            
        case 0xcf:                                      // uint 64
        case 0xd3:                                      // int 64
        case 0xcb:                                      // float 53
            bytes = 9;
            break;
            
        case 0xd9:                                      // str 8
        case 0xda:                                      // str 16
        case 0xdb:                                      // str 32
            GetString();
            return;
            
        default:
            UNEXPECTED("'Skip single value'");
    }
    
    pos += bytes;
}

/// Process and return an 8 bit size from pos+1, pos is incremented
size_t MsgPack::GetSize8 (const char* szType)
{
    HAS_DATA(2, szType);
    const size_t ret = size_t(data[pos+1]);
    pos += 2;
    return ret;
}

/// Process and return a 16 bit size from pos+1, pos is incremented
size_t MsgPack::GetSize16 (const char* szType)
{
    HAS_DATA(3, szType);
    const size_t ret =
        (size_t(data[pos+1]) << 8) +
         size_t(data[pos+2]);
    pos += 3;
    return ret;
}

/// Process and return a 32 bit size from pos+1, pos is incremented
size_t MsgPack::GetSize32 (const char* szType)
{
    HAS_DATA(5, szType);
    const size_t ret =
        (size_t(data[pos+1]) << 24) +
        (size_t(data[pos+2]) << 16) +
        (size_t(data[pos+3]) <<  8) +
         size_t(data[pos+4]);
    pos += 5;
    return ret;
}
