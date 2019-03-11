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
#include <unistd.h>

UDPReceiver::UDPReceiver(const std::string& addr, int port,
                         size_t bufSize, unsigned timeOut_ms) :
f_port(port), f_addr(addr)
{
    // open the socket
    Open(addr, port, bufSize, timeOut_ms);
}

// cleanup: make sure the socket is closed
UDPReceiver::~UDPReceiver()
{
    Close();
}

void UDPReceiver::Open(const std::string& addr, int port,
                       size_t _bufSize, unsigned timeOut_ms)
{
    try {
        f_port = port;
        f_addr = addr;

        const std::string decimal_port(std::to_string(f_port));

        // get a socket
        f_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if(f_socket == -1)
            throw UDPRuntimeError(("could not create UDP socket for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
        
        // Reuse address and port to allow others to connect, too
        int setToVal = 1;
        if (setsockopt(f_socket, SOL_SOCKET, SO_REUSEADDR, &setToVal, sizeof(setToVal)) < 0)
            throw UDPRuntimeError(("could not setsockopt SO_REUSEADDR for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
        if (setsockopt(f_socket, SOL_SOCKET, SO_REUSEPORT, &setToVal, sizeof(setToVal)) < 0)
            throw UDPRuntimeError(("could not setsockopt SO_REUSEPORT for: \"" + f_addr + ":" + decimal_port + "\"").c_str());


        // define receive timeout
#if IBM
#error Timeout is passed in a DWORD???
#else
        struct timeval timeout;
        timeout.tv_sec = timeOut_ms / 1000;
        timeout.tv_usec = (timeOut_ms % 1000) * 1000;
        if (setsockopt(f_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            throw UDPRuntimeError(("could not setsockopt SO_RCVTIMEO for: \"" + f_addr + ":" + decimal_port + "\"").c_str());
#endif

        // get a valid address based on inAddr/port
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        
        int r = getaddrinfo(f_addr.c_str(), decimal_port.c_str(), &hints, &f_addrinfo);
        if(r != 0 || f_addrinfo == NULL)
            throw UDPRuntimeError(("invalid address or port for UDP socket: \"" + f_addr + ":" + decimal_port + "\"").c_str());

        // bind the socket to the address:port
        r = bind(f_socket, f_addrinfo->ai_addr, f_addrinfo->ai_addrlen);
        if(r != 0)
            throw UDPRuntimeError(("could not bind UDP socket with: \"" + f_addr + ":" + decimal_port + "\"").c_str());

        // reserve buffer
        SetBufSize(_bufSize);
    }
    catch (...) {
        Close();
        // re-throw
        throw;
    }
}

/** \brief Clean up the UDP server.
 *
 // Close: This function frees the address info structures and close the socket.
 */
void UDPReceiver::Close()
{
    // cleanup
    if (f_socket >= 0) {
        close(f_socket);
        f_socket = -1;
    }
    
    if (f_addrinfo) {
        freeaddrinfo(f_addrinfo);
        f_addrinfo = NULL;
    }
    
    // release buffer
    SetBufSize(0);
}

// allocates the receiving buffer
void UDPReceiver::SetBufSize(size_t _bufSize)
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
long UDPReceiver::recv()
{
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    
    long ret = ::recv(f_socket, buf, bufSize-1, 0);
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
 * \param[in] msg  The buffer where the message will be saved.
 * \param[in] max_size  The size of the \p msg buffer in bytes.
 * \param[in] max_wait_ms  The maximum number of milliseconds to wait for a message.
 *
 * \return -1 if an error occurs or the function timed out, the number of bytes received otherwise.
 */
long UDPReceiver::timedRecv(int max_wait_ms)
{
    fd_set sRead, sErr;
    struct timeval timeout;

    FD_ZERO(&sRead);
    FD_SET(f_socket, &sRead);           // check our socket
    FD_COPY(&sRead, &sErr);             // also for errors
    
    timeout.tv_sec = max_wait_ms / 1000;
    timeout.tv_usec = (max_wait_ms % 1000) * 1000;
    int retval = select(f_socket + 1, &sRead, NULL, &sErr, &timeout);
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
    errno = EAGAIN;
    return -1;
}
