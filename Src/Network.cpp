//
// Network.cpp
// Low-level network stuff, e.g. to communicate raw TCP/UDP with
// channels like RealTraffic
//
//  Some inital ideas and pieces of code take from these sources:
//  https://linux.m2osw.com/c-implementation-udp-clientserver
//

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

// All includes are collected in one header
#include "LiveTraffic.h"
#include <fcntl.h>
#if IBM
#include <winsock2.h>
#include <ws2tcpip.h>
#undef errno
#define errno WSAGetLastError()     // https://docs.microsoft.com/en-us/windows/desktop/WinSock/error-codes-errno-h-errno-and-wsagetlasterror-2
#define close closesocket
typedef USHORT in_port_t;
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

//
// MARK: SocketNetworking
//

NetRuntimeError::NetRuntimeError(const char *w) :
std::runtime_error(w)
{
    // make network error available
    char sErr[SERR_LEN];
    strerror_s(sErr, sizeof(sErr), errno);
    errTxt = sErr;              // copy
}

SocketNetworking::SocketNetworking(const std::string& _addr, int _port,
                                   size_t _bufSize, unsigned _timeOut_ms,
                                   bool _bBroadcast) :
f_port(_port), f_addr(_addr)
{
    // open the socket
    Open(_addr, _port, _bufSize, _timeOut_ms, _bBroadcast);
}

// cleanup: make sure the socket is closed and all memory cleanup up
SocketNetworking::~SocketNetworking()
{
    Close();
}

void SocketNetworking::Open(const std::string& _addr, int _port,
                       size_t _bufSize, unsigned _timeOut_ms, bool _bBroadcast)
{
    struct addrinfo *   addrinfo      = NULL;
    try {
        // store member values
        f_port = _port;
        f_addr = _addr;
        const std::string decimal_port(std::to_string(f_port));

        // get a valid address based on inAddr/port
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        GetAddrHints(hints);            // ask subclasses
        
        int r = getaddrinfo(f_addr.c_str(), decimal_port.c_str(), &hints, &addrinfo);
        if(r != 0 || addrinfo == NULL)
            throw NetRuntimeError(("invalid address or port for socket: \"" + f_addr + ":" + decimal_port + "\"").c_str());
        
        // get a socket
        f_socket = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
        if(f_socket == INVALID_SOCKET)
            throw NetRuntimeError(("could not create socket for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
        
        // Reuse address and port to allow others to connect, too
        int setToVal = 1;
#if IBM
        if (setsockopt(f_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&setToVal, sizeof(setToVal)) < 0)
            throw NetRuntimeError(("could not setsockopt SO_REUSEADDR for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#else
        if (setsockopt(f_socket, SOL_SOCKET, SO_REUSEADDR, &setToVal, sizeof(setToVal)) < 0)
            throw NetRuntimeError(("could not setsockopt SO_REUSEADDR for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
        if (setsockopt(f_socket, SOL_SOCKET, SO_REUSEPORT, &setToVal, sizeof(setToVal)) < 0)
            throw NetRuntimeError(("could not setsockopt SO_REUSEPORT for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#endif

        // define receive timeout
#if IBM
        DWORD wsTimeout = _timeOut_ms;
        if (setsockopt(f_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&wsTimeout, sizeof(wsTimeout)) < 0)
            throw NetRuntimeError(("could not setsockopt SO_RCVTIMEO for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#else
        struct timeval timeout;
        timeout.tv_sec = _timeOut_ms / 1000;
        timeout.tv_usec = (_timeOut_ms % 1000) * 1000;
        if (setsockopt(f_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            throw NetRuntimeError(("could not setsockopt SO_RCVTIMEO for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#endif
        
        // if requested allow for sending broadcasts
        if (_bBroadcast) {
            setToVal = 1;
#if IBM
            if (setsockopt(f_socket, SOL_SOCKET, SO_BROADCAST, (char*)&setToVal, sizeof(setToVal)) < 0)
                throw NetRuntimeError(("could not setsockopt SO_BROADCAST for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#else
            if (setsockopt(f_socket, SOL_SOCKET, SO_BROADCAST, &setToVal, sizeof(setToVal)) < 0)
                throw NetRuntimeError(("could not setsockopt SO_BROADCAST for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#endif
        }

        // bind the socket to the address:port
        r = bind(f_socket, addrinfo->ai_addr, (int)addrinfo->ai_addrlen);
        if(r != 0)
            throw NetRuntimeError(("could not bind UDP socket with: \"" + f_addr + ":" + decimal_port + "\"").c_str());

        // free adress info
        freeaddrinfo(addrinfo);
        addrinfo = NULL;

        // reserve receive buffer
        SetBufSize(_bufSize);
    }
    catch (...) {
        // free adress info
        if (addrinfo) {
            freeaddrinfo(addrinfo);
            addrinfo = NULL;
        }
        // make sure everything is closed
        Close();
        // re-throw
        throw;
    }
}

/** \brief Clean up the UDP server.
 *
 // Close: This function frees the address info structures and close the socket.
 */
void SocketNetworking::Close()
{
    // cleanup
    if (f_socket != INVALID_SOCKET) {
        close(f_socket);
        f_socket = INVALID_SOCKET;
    }
    
    // release buffer
    SetBufSize(0);
}

// allocates the receiving buffer
void SocketNetworking::SetBufSize(size_t _bufSize)
{
    // remove existing buffer
    if (buf) {
        delete[] buf;
        buf = NULL;
        bufSize = 0;
    }
    
    // create a new one
    if (_bufSize > 0) {
        buf = new char[bufSize=_bufSize];
        memset(buf, 0, bufSize);
    }
}

// updates the error text and returns it
std::string SocketNetworking::GetLastErr()
{
    char sErr[SERR_LEN];
#if IBM
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,   // flags
        NULL,                                                                   // lpsource
        WSAGetLastError(),                                                      // message id
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),                              // languageid
        sErr,                                                                   // output buffer
        sizeof(sErr),                                                           // size of msgbuf, bytes
        NULL);
#else
    strerror_s(sErr, sizeof(sErr), errno);
#endif
    return std::string(sErr);
}


/** \brief Wait on a message.
 *
 * This function waits until a message is received on this UDP server.
 * There are no means to return from this function except by receiving
 * a message. Remember that UDP does not have a connect state so whether
 * another process quits does not change the status of this UDP server
 * and thus it continues to wait forever.
 *
 * Note that you may change the type of socket by making it non-blocking
 * (use the get_socket() to retrieve the socket identifier) in which
 * case this function will not block if no message is available. Instead
 * it returns immediately.
 *
 * \param[in] max_size  The maximum size the message (i.e. size of the \p msg buffer.) If 0 then no change.
 *
 * \return The number of bytes read or -1 if an error occurs.
 */
long SocketNetworking::recv()
{
    if (!buf) {
#if IBM
        WSASetLastError(WSA_NOT_ENOUGH_MEMORY);
#else
        errno = ENOMEM;
#endif
        return -1;
    }
    
    long ret = ::recv(f_socket, buf, (int)bufSize-1, 0);
    if (ret >= 0)  {                    // we did receive something
        buf[ret] = 0;                   // zero-termination
    } else {
        buf[0] = 0;                     // empty string
    }
    return ret;
}

/** \brief Wait for data to come in.
 *
 * This function waits for a given amount of time for data to come in. If
 * no data comes in after max_wait_ms, the function returns with -1 and
 * errno set to EAGAIN.
 *
 * The socket is expected to be a blocking socket (the default,) although
 * it is possible to setup the socket as non-blocking if necessary for
 * some other reason.
 *
 * This function blocks for a maximum amount of time as defined by
 * max_wait_ms. It may return sooner with an error or a message.
 *
 * \param[in] max_wait_ms  The maximum number of milliseconds to wait for a message.
 *
 * \return -1 if an error occurs or the function timed out, the number of bytes received otherwise.
 */
long SocketNetworking::timedRecv(int max_wait_ms)
{
    fd_set sRead, sErr;
    struct timeval timeout;

    FD_ZERO(&sRead);
    FD_SET(f_socket, &sRead);           // check our socket
    FD_ZERO(&sErr);                     // also for errors
    FD_SET(f_socket, &sErr);

    timeout.tv_sec = max_wait_ms / 1000;
    timeout.tv_usec = (max_wait_ms % 1000) * 1000;
    int retval = select((int)f_socket + 1, &sRead, NULL, &sErr, &timeout);
    if(retval == -1)
    {
        // select() set errno accordingly
        buf[0] = 0;                     // empty string
        return -1;
    }
    if(retval > 0)
    {
        // was it an error that triggered?
        if (FD_ISSET(f_socket,&sErr)) {
            return -1;
        }
        
        // our socket has data
        if (FD_ISSET(f_socket, &sRead))
            return recv();
    }
    
    // our socket has no data
    buf[0] = 0;                     // empty string
#if IBM
    WSASetLastError(WSAEWOULDBLOCK);
#else
    errno = EAGAIN;
#endif
    return -1;
}

// sends the message as a broadcast
bool SocketNetworking::broadcast (const char* msg)
{
    int index=0;
    int length = (int)strlen(msg);
    
    struct sockaddr_in s;
    memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons((in_port_t)f_port);
    s.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    
    while (index<length) {
        int count = (int)::sendto(f_socket, msg + index, (int)(length - index), 0,
                                  (struct sockaddr *)&s, sizeof(s));
        if (count<0) {
            if (errno==EINTR) continue;
            LOG_MSG(logERR, "%s (%s)",
                    ("sendto failed: \"" + f_addr + ":" + std::to_string(f_port) + "\"").c_str(),
                    GetLastErr().c_str());
            return false;
        } else {
            index+=count;
        }
    }
    return true;
}


// return a string for a IPv4 and IPv6 address
std::string SocketNetworking::GetAddrString (const struct sockaddr* addr)
{
    std::string s (std::max(INET_ADDRSTRLEN,INET6_ADDRSTRLEN), '\0');
    
    switch(addr->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
            inet_ntop(AF_INET, &(addr_in->sin_addr), s.data(), (socklen_t)s.length());
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), s.data(), (socklen_t)s.length());
            break;
        }
        default:
            break;
    }
    
    return s;
}

//
// MARK: UDPReceiver
//

// UDP only allows UDP
void UDPReceiver::GetAddrHints (struct addrinfo& hints)
{
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
}

//
// MARK: TCPConnection
//

void TCPConnection::Close()
{
    // also close session connection
    if (f_session_socket != INVALID_SOCKET) {
        close(f_session_socket);
        f_session_socket = INVALID_SOCKET;
    }
    
    // pass on to base class
    SocketNetworking::Close();
}

// only closes the listening socket, but not a session connection
void TCPConnection::CloseListenerOnly()
{
    // just call the base class Close, bypassing our own virtual function
    SocketNetworking::Close();
}

// Listen for and accept connections
void TCPConnection::listen (int numConnections)
{
    if (::listen(f_socket, numConnections) < 0)
        throw NetRuntimeError(("can't listen on socket: \"" + f_addr + ":" + std::to_string(f_port) + "\"").c_str());
}

bool TCPConnection::accept (bool bUnlisten)
{
    socklen_t addrLen = sizeof(f_session_addr);
    memset (&f_session_addr, 0, sizeof(f_session_addr));
    
    // potentially blocking call
    f_session_socket = ::accept (f_socket, (struct sockaddr*)&f_session_addr, &addrLen);
    
    // if we are to "unlisten" then we close the listening socket
    if (f_session_socket != INVALID_SOCKET && bUnlisten) {
        CloseListenerOnly();
    }
    
    // successful?
    return f_session_socket != INVALID_SOCKET;
}

// just combines the above
bool TCPConnection::listenAccept (int numConnections)
{
    try {
        listen(numConnections);
        // if we wait for exactly one connection then we "unlisten" once we accepted that one connection:
        return accept(numConnections == 1);
    }
    catch (NetRuntimeError e) {
        LOG_MSG(logERR, "%s (%s)", e.what(), e.errTxt.c_str());
    }
    return false;
}

// write a message out
bool TCPConnection::send(const char* msg)
{
    int index=0;
    int length = (int)strlen(msg);
    while (index<length) {
        int count = (int)::send(f_session_socket, msg + index, (int)(length - index), 0);
        if (count<0) {
            if (errno==EINTR) continue;
            LOG_MSG(logERR, "%s (%s)",
                    ("send failed: \"" + f_addr + ":" + std::to_string(f_port) + "\"").c_str(),
                    GetLastErr().c_str());
            return false;
        } else {
            index+=count;
        }
    }
    return true;
}

// TCP only allows TCP
void TCPConnection::GetAddrHints (struct addrinfo& hints)
{
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
}

