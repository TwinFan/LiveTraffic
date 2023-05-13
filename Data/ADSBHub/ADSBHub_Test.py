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
    i = 0                       # Count received messages (with actual content)
    iNull = 0                   # Count received NULL messages
    incomplLn = ''              # incomplete beginning of a line from last network message
    while(1):
        r = sock.recv(4096)     # Receive data
        if (len(r) > 0):
            # Received something after a couple of NULLs?
            if (iNull > 0):
                print ("After {0} NULL messages, now receiving:\n".format(iNull))
                iNull = 0
            
            i += 1
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
#                    print (binascii.hexlify(incomplLn[0:l],' ').decode('ascii'))
                    # Print Header in 3 parts, showing ICAO in 6 digits, then add data after colon:
                    print (incomplLn[0:4].hex('-') + '|' + incomplLn[4:7].hex() + '|' + incomplLn[7:9].hex('-') + ': ' + incomplLn[9:l].hex(' '))
                    if len(incomplLn) > l:          # if buffer has _more_ than just the printed msg
                        incomplLn = incomplLn[l:]   # remove printed part (inefficient, makes copies...but well...)
                        l = incomplLn[0]            # and determine length of next msg
                    else:
                        incomplLn = b''             # msg fit exactly, clear buffer
        else:
            if (iNull == 0):
                print ("Receiving NULL...")
            iNull += 1
            

except OSError as err:
    print ("OS Error: {0}".format(err))

except Exception as err:
    # eat the exception (like Ctrl-C interrupt)
    print (repr(err))

finally:
    sock.close()
    print ("Socket closed")
