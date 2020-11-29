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
typedef int SOCKET;             ///< Windows defines SOCKET, so we define it for non-Windows manually
constexpr SOCKET INVALID_SOCKET = -1;
#endif
#include <stdexcept>

/// @brief Exception raised by XPMP2::SocketNetworking objects
/// @details This exception is raised when the address
///          and port combinaison cannot be resolved or if the socket cannot be
///          opened.
class NetRuntimeError : public std::runtime_error
{
public:
    std::string errTxt;             ///< OS text for what `errno` says, output of strerror_s()
    std::string fullWhat;           ///< combines `w` and `errTxt`
    NetRuntimeError(const char *w); ///< Constructor sets the above texts
    /// Return the full message, ie. `fullWhat`
    const char* what() const noexcept override { return fullWhat.c_str(); }
};

/// Base class for any socket-based networking
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
    /// Default constructor is not doing anything
    SocketNetworking() {}
    /// Constructor creates a socket and binds it to the given address
    SocketNetworking(const std::string& _addr, int _port, size_t _bufSize = 512,
                     unsigned _timeOut_ms = 0, bool _bBroadcast = false);
    /// Destructor makes sure the socket is closed
    virtual ~SocketNetworking();

    /// Creates a socket and binds it to the given local address
    virtual void Open(const std::string& _addr, int _port, size_t _bufSize = 512,
                      unsigned _timeOut_ms = 0, bool _bBroadcast = false);
    /// Creates a socket and connects it to the given remote server
    virtual void Connect(const std::string& _addr, int _port, size_t _bufSize,
                         unsigned _timeOut_ms = 0);
    /// Thread-safely close the connection(s) and frees the buffer
    virtual void Close();
    /// Is a socket open?
    inline bool isOpen() const { return (f_socket != INVALID_SOCKET); }
    /// (Re)Sets the buffer size (or clears it if `_bufSize==0`)
    void SetBufSize (size_t _bufSize);
    /// Returns a human-readable text for the last error
    static std::string GetLastErr();
    
    // attribute access
    SOCKET       getSocket() const   { return f_socket; }   ///< the socket
    int          getPort() const     { return f_port; }     ///< the port
    std::string  getAddr() const     { return f_addr; }     ///< the interface address

    /// returna the buffer
    const char* getBuf () const  { return buf ? buf : ""; }
    
    /// Waits to receive a message, ensures zero-termination in the buffer
    long                recv();
    /// Waits to receive a message with timeout, ensures zero-termination in the buffer
    long                timedRecv(int max_wait_ms);
    
    /// Sends a broadcast message
    bool broadcast (const char* msg);
    
    /// Convert addresses to string
    static std::string GetAddrString (const struct sockaddr* addr);
    
protected:
    /// Subclass to tell which addresses to look for
    virtual void GetAddrHints (struct addrinfo& hints) = 0;
};


/// Receives UDP messages
class UDPReceiver : public SocketNetworking
{
public:
    /// Default constructor is not doing anything
    UDPReceiver() : SocketNetworking() {}
    /// Constructor creates a socket and binds it to the given address
    UDPReceiver(const std::string& _addr, int _port, size_t _bufSize = 512,
                unsigned _timeOut_ms = 0) :
        SocketNetworking(_addr,_port,_bufSize,_timeOut_ms) {}
    
protected:
    /// Sets flags to AI_PASSIVE, AF_INET, SOCK_DGRAM, IPPROTO_UDP
    virtual void GetAddrHints (struct addrinfo& hints);
};


/// Listens to TCP connections and opens a session socket upon connect
class TCPConnection : public SocketNetworking
{
protected:
    SOCKET              f_session_socket = INVALID_SOCKET;  ///< session socket, ie. the socket opened when a counterparty connects
    struct sockaddr_in  f_session_addr;                     ///< address of the connecting counterparty
#if APL == 1 || LIN == 1
    /// the self-pipe to shut down the TCP listener gracefully
    SOCKET selfPipe[2] = { INVALID_SOCKET, INVALID_SOCKET };
#endif

public:
    /// Default constructor is not doing anything
    TCPConnection() : SocketNetworking() {}
    /// Constructor creates a socket and binds it to the given address
    TCPConnection(const std::string& _addr, int _port, size_t _bufSize = 512,
                  unsigned _timeOut_ms = 0) :
        SocketNetworking(_addr,_port,_bufSize,_timeOut_ms) {}
    
    virtual void Close();       ///< also close session connection
    void CloseListenerOnly();   ///< only closes the listening socket, but not a connected session

    void listen (int numConnections = 1);       ///< listen for incoming connections
    bool accept (bool bUnlisten = false);       ///< accept an incoming connections, optinally stop listening
    bool listenAccept (int numConnections = 1); ///< combines listening and accepting
    
    /// Connected to a counterparty?
    bool IsConnected () const { return f_session_socket != INVALID_SOCKET; };
    
    /// send messages on session connection
    bool send(const char* msg);

protected:
    virtual void GetAddrHints (struct addrinfo& hints);
};

#endif /* Network_h */
