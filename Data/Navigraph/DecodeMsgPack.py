#!/usr/bin/env python3
# Courtesy of ChatGPT

import sys
import msgpack


def describe(obj, indent=0):
    prefix = " " * indent

    if isinstance(obj, dict):
        print(f"{prefix}MAP ({len(obj)} entries)")
        for key, value in obj.items():
            print(f"{prefix}  {key:15} = ", end="")
            describe(value, 0)

    elif isinstance(obj, list):
        print(f"{prefix}ARRAY ({len(obj)} elements)")
        for i, value in enumerate(obj):
            print(f"{prefix}  [{i}]")
            describe(value, indent + 4)

    elif isinstance(obj, str):
        print(f"{prefix}STRING: {obj!r}")

    elif isinstance(obj, bool):
        print(f"{prefix}BOOL:   {obj}")

    elif obj is None:
        print(f"{prefix}NIL")

    elif isinstance(obj, int):
        print(f"{prefix}INT:    {obj}")

    elif isinstance(obj, float):
        print(f"{prefix}FLOAT:  {obj}")

    else:
        print(f"{prefix}{type(obj).__name__}: {obj!r}")


def load_hexdump(filename):
    with open(filename, "r") as f:
        text = f.read()

    # Remove whitespace/newlines and convert hex -> bytes
    hex_string = "".join(text.split())
    return bytes.fromhex(hex_string)


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} dump.txt")
        sys.exit(1)

    raw = load_hexdump(sys.argv[1])

    data = msgpack.unpackb(
        raw,
        raw=False,      # decode strings as UTF-8
        strict_map_key=False
    )

    print("Decoded structure:")
    print("=" * 60)
    describe(data)


if __name__ == "__main__":
    main()