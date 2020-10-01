/// @file       Network.h
/// @brief      Low-level network communications, especially for TCP/UDP
/// @see        Some inital ideas and pieces of code take from
///             https://linux.m2osw.com/c-implementation-udp-clientserver
/// @details    SocketNetworking: Any network socket connection\n
///             UDPReceiver: listens to and receives UDP datagram\n
///             TCPConnection: receives incoming TCP connection\n
/// @author     Birger Hoppe
/// @copyright  (c) 2019-2020 Birger Hoppe
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

#ifndef Network_h
#define Network_h

#include <sys/types.h>
#if IBM
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netdb.h>
typedef int SOCKET;             // Windows defines SOCKET, so we define it for non-Windows manually
constexpr SOCKET INVALID_SOCKET = -1;
#endif
#include <stdexcept>

// error messages used in derived classes
#define ERR_TCP_LISTENACCEPT    "%s: Error opening the TCP port on %s:%s: %s"
#define ERR_SOCK_NOTCONNECTED   "%s: Cannot send position: not connected"
#define ERR_SOCK_INV_POS        "%s: Cannot send position: position not fully valid"
#define ERR_SOCK_SEND_FAILED    "%s: Could not send position: send operation failed"

#define ERR_UDP_RCVR_OPEN       "%s: Error creating UDP socket for %s:%d: %s"
#define ERR_UDP_RCVR_RCVR       "%s: Error receiving UDP: %s"


// The UDPRuntimeError exception is raised when the address
// and port combinaison cannot be resolved or if the socket cannot be
// opened.

class NetRuntimeError : public std::runtime_error
{
public:
    std::string errTxt;
    NetRuntimeError(const char *w);
};

// Base class for socket-based networking
class SocketNetworking
{
protected:
    SOCKET              f_socket        = INVALID_SOCKET;
    int                 f_port          = 0;
    std::string         f_addr;
    
    // the data receive buffer
    char*               buf             = NULL;
    size_t              bufSize         = 512;

public:
    // The address is a string and it can represent an IPv4 or IPv6 address.
    SocketNetworking() {}
    SocketNetworking(const std::string& _addr, int _port, size_t _bufSize,
                     unsigned _timeOut_ms = 0, bool _bBroadcast = false);
    virtual ~SocketNetworking();

    // Opens the socket for listening
    virtual void Open(const std::string& _addr, int _port, size_t _bufSize,
                      unsigned _timeOut_ms = 0, bool _bBroadcast = false);
    // Connect to a server
    virtual void Connect(const std::string& _addr, int _port, size_t _bufSize,
                         unsigned _timeOut_ms = 0);
    // Close the connection(s)
    virtual void Close();
    
    inline bool isOpen() const { return (f_socket != INVALID_SOCKET); }
    
    void SetBufSize (size_t _bufSize);
    std::string GetLastErr();
    
    // attribute access
    inline SOCKET       getSocket() const   { return f_socket; }
    inline int          getPort() const     { return f_port; }
    inline std::string  getAddr() const     { return f_addr; }

    // return the buffer
    const char* getBuf () const  { return buf ? buf : ""; }
    
    // receive messages
    long                recv();
    long                timedRecv(int max_wait_ms);
    
    // send broadcast message
    bool broadcast (const char* msg);
    
    // convert addresses to string
    static std::string GetAddrString (const struct sockaddr* addr);
    
protected:
    // subclass tell which addresses to look for
    virtual void GetAddrHints (struct addrinfo& hints) = 0;
};

// Receives UDP messages

class UDPReceiver : public SocketNetworking
{
public:
    // The address is a string and it can represent an IPv4 or IPv6 address.
    UDPReceiver() : SocketNetworking() {}
    UDPReceiver(const std::string& _addr, int _port, size_t _bufSize,
                unsigned _timeOut_ms = 0) :
        SocketNetworking(_addr,_port,_bufSize,_timeOut_ms) {}
    
protected:
    virtual void GetAddrHints (struct addrinfo& hints);
};

// Receives TCP connections

class TCPConnection : public SocketNetworking
{
protected:
    SOCKET              f_session_socket = INVALID_SOCKET;
    struct sockaddr_in  f_session_addr;
#if APL == 1 || LIN == 1
    // the self-pipe to shut down the TCP listener gracefully
    SOCKET selfPipe[2] = { INVALID_SOCKET, INVALID_SOCKET };
#endif

public:
    // The address is a string and it can represent an IPv4 or IPv6 address.
    TCPConnection() : SocketNetworking() {}
    TCPConnection(const std::string& _addr, int _port, size_t _bufSize,
                  unsigned _timeOut_ms = 0) :
        SocketNetworking(_addr,_port,_bufSize,_timeOut_ms) {}
    
    virtual void Close();       // also close session connection
    void CloseListenerOnly();   // only closes the listening socket, but not a session connection

    // Listen for and accept connections
    void listen (int numConnections = 1);
    bool accept (bool bUnlisten = false);
    bool listenAccept (int numConnections = 1);
    
    bool IsConnected () const { return f_session_socket != INVALID_SOCKET; };
    
    // send messages on session connection
    bool send(const char* msg);

protected:
    virtual void GetAddrHints (struct addrinfo& hints);
};

#endif /* Network_h */
