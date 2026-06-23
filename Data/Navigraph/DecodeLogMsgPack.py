#!/usr/bin/env python3
# Courtesy of ChatGPT

import re
import sys
import msgpack


HEX_LINE_RE = re.compile(r'^[0-9A-Fa-f\s]+$')

FIELDS = [
    "flightId",
    "timestamp",
    "aircraftId",
    "radarId",
    "type",
    "registration",
    "origin",
    "destination",
    "flight",
    "squawk",
    "latitude",
    "longitude",
    "track",
    "altitude",
    "speed",
    "onGround",
    "vSpeed",
    "callsign",
    "sourceType",
    "eta",
    "operatingAs",
    "paintedAs",
]

def print_csv_header():
    #
    # Header
    #
    print(
        "msg,idx," +
        ",".join(FIELDS)
    )

def print_csv(msg_line, data):
    #
    # One CSV row per aircraft
    #
    for idx, obj in enumerate(data):
        row = [msg_line,str(idx)]               # Starts with msg info and idx within message

        for field in FIELDS:
            value = obj.get(field)

            if value is None:
                row.append("<NIL>")

            elif isinstance(value, bool):
                row.append("1" if value else "0")

            else:
                text = str(value)

                #
                # Minimal CSV escaping
                #
                if "," in text or '"' in text:
                    text = '"' + text.replace('"', '""') + '"'

                row.append(text)

        print(",".join(row))

def decode_message(hex_text, msg_line):
    try:
        raw = bytes.fromhex(hex_text)

        data = msgpack.unpackb(
            raw,
            raw=False,
            strict_map_key=False
        )
        print_csv(msg_line, data)

    except Exception as ex:
        print()
        print("=" * 80)
        print(f"MESSAGE '{msg_line}' - DECODE FAILED")
        print("=" * 80)
        print(ex)


def process_log(filename):
    collecting = False
    regTime = r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z"
    msg_line = ""
    hex_lines = []

    print_csv_header()

    with open(filename, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip()

            #
            # Start of a received message
            #
            if "Navigraph/FR24 RECEIVED HTTP_OK:" in line:
                collecting = True
                match = re.search(regTime, line)            # Find the timestamp in the line
                if match:
                    msg_line = match.group()
                else:
                    msg_line = line
                hex_lines = []
                continue

            if collecting:
                stripped = line.strip()

                #
                # Blank line => end of dump
                #
                if not stripped:
                    if hex_lines:
                        decode_message(" ".join(hex_lines), msg_line)

                    collecting = False
                    hex_lines = []
                    continue

                #
                # Hex dump line?
                #
                if HEX_LINE_RE.match(stripped):
                    hex_lines.append(stripped)
                    continue

                #
                # Any other line => dump finished
                #
                if hex_lines:
                    decode_message(" ".join(hex_lines), msg_line)

                collecting = False
                hex_lines = []

        #
        # EOF while collecting
        #
        if collecting and hex_lines:
            decode_message(" ".join(hex_lines), msg_line)


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} logfile.txt")
        sys.exit(1)

    process_log(sys.argv[1])


if __name__ == "__main__":
    main()