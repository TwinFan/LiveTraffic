//
//  Network.h
//  LiveTraffic
//  Low-level network stuff, e.g. to communicate raw TCP/UDP with
//  channels like RealTraffic
//
//  Some inital ideas and pieces of code take from these sources:
//  https://linux.m2osw.com/c-implementation-udp-clientserver
//

/*
 * Copyright (c) 2019, Birger Hoppe
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


#ifndef Network_h
#define Network_h

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>

// The UDPRuntimeError exception is raised when the address
// and port combinaison cannot be resolved or if the socket cannot be
// opened.

class UDPRuntimeError : public std::runtime_error
{
public:
    UDPRuntimeError(const char *w) : std::runtime_error(w) {}
};

// Receives UDP messages

class UDPReceiver
{
private:
    int                 f_socket        = -1;
    int                 f_port          = 0;
    std::string         f_addr;
    struct addrinfo *   f_addrinfo      = NULL;
    
    // the data receive buffer
    char*               buf             = NULL;
    size_t              bufSize         = 512;

public:
    // The address is a string and it can represent an IPv4 or IPv6 address.
    UDPReceiver() {}
    UDPReceiver(const std::string& addr, int port, size_t bufSize,
                unsigned timeOut_ms = 0);
    virtual ~UDPReceiver();
    // opens/closes the socket
    void Open(const std::string& addr, int port, size_t bufSize,
              unsigned timeOut_ms = 0);
    void Close();
    inline bool isOpen() const { return (f_socket >= 0); }
    
    void SetBufSize (size_t _bufSize);
    
    // attribute access
    inline int          getSocket() const   { return f_socket; }
    inline int          getPort() const     { return f_port; }
    inline std::string  getAddr() const     { return f_addr; }
    
    // receive a UDP datagram
    long                recv();
    long                timedRecv(int max_wait_ms);
    
    // return the buffer
    const char* getBuf () const  { return buf ? buf : ""; }
};

#endif /* Network_h */
