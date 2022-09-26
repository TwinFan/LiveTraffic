#!/usr/bin/python3
#
# Quick&dirty check if data can be received from ADSBHub.
# For preconditions see
#   https://www.adsbhub.org/howtofeed.php
# and
#   https://www.adsbhub.org/howtogetdata.php

import sys
import socket
import binascii

sock = socket.create_connection(('data.adsbhub.org', 5002)) # TCP
sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

print ("Socket connected")

try:
    msgFormat = ''              # Will become either 'SBS' or 'C-VRS'
    i = 0
    incomplLn = ''              # incomplete beginning of a line from last network message
    while(1):
        r = sock.recv(4096)     # Receive data
        i += 1
        if (len(r) > 0):
            # Determine which format we are listening to
            if msgFormat == '':
                if len(r) >= 30:
                    try:
                        s = r[0:3].decode('ascii')
                        if s == 'MSG' or s == 'SEL' or s == 'ID,' or s == 'AIR' or s == 'STA' or s == 'CLK':
                            msgFormat = 'SBS'
                    except:
                        msgFormat = ''
                if msgFormat == '':
                    msgFormat = 'C-VRS'
                    incomplLn = b''
                print ("Format probably is {0}.".format(msgFormat))
            
            # Number of messages received so far
            print ("{0}. Message:".format(i))
            
            if msgFormat == 'SBS':
                # add to a previously incomplete line, then split into list of lines, separated by new line
                lln = (incomplLn + r.decode('ascii')).splitlines(True)
                incomplLn = ''
                for ln in lln:      # for each line in the response
                    # it it is a complete line including NewLn then output it
                    if ln[-1] == '\n':
                        print(ln.rstrip('\n'))
                    # an incomplete line (can only be the last one) is saved to 'incomplLn'
                    else:
                        incomplLn = ln
            # C-VRS format
            else:
                incomplLn += r                      # add what's received to what's left over from last msg
                l = incomplLn[0]                    # length of individual msg is in first byte
                while(len(incomplLn) >= l):         # while buffer contains full msg print it
                    print (binascii.hexlify(incomplLn[0:l],' ').decode('ascii'))
                    if len(incomplLn) > l:          # if buffer has _more_ than just the printed msg
                        incomplLn = incomplLn[l:]   # remove printed part (inefficient, makes copies...but well...)
                        l = incomplLn[0]            # and determine length of next msg
                    else:
                        incomplLn = b''             # msg fit exactly, clear buffer
        else:
            print ("{0}. <NULL> {1}".format(i,r))
            

except OSError as err:
    print ("OS Error: {0}".format(err))

except Exception as err:
    # eat the exception (like Ctrl-C interrupt)
    print (repr(err))

finally:
    sock.close()
    print ("Socket closed")
