import base64
import json
import sys
import traceback


def parse_jwt(token):
    try:
        # 1. Validierung des grundlegenden Token-Formats
        parts = token.split(".")
        if len(parts) != 3:
            print(
                f"Error: A valid JWT must have exactly 2 dots (.) but we found {len(parts) - 1}.",
                file=sys.stderr,
            )
            return None

        payload_b64 = parts[1]

        # 2. Replace Base64-Url-Safe chars
        payload_b64 = payload_b64.replace("-", "+").replace("_", "/")

        # 3. Add Base64-Padding 
        remainder = len(payload_b64) % 4
        if remainder > 0:
            payload_b64 += "=" * (4 - remainder)

        # 4. Base64 Decoding
        try:
            decoded_bytes = base64.b64decode(payload_b64)
        except Exception as b64_err:
            print(
                f"Error during Base64-Decoding: {b64_err}", file=sys.stderr
            )
            print(f"Attempted String: {payload_b64}", file=sys.stderr)
            return None

        # 5. String Conversion
        try:
            decoded_str = decoded_bytes.decode("utf-8")
        except UnicodeDecodeError as utf8_err:
            print(
                f"Error during UTF-8 conversion: {utf8_err}",
                file=sys.stderr,
            )
            return None

        # 6. JSON Parsing
        try:
            return json.loads(decoded_str)
        except json.JSONDecodeError as json_err:
            print(
                f"Error during JSON parsing: {json_err}",
                file=sys.stderr,
            )
            print(f"Decoded text: {decoded_str}", file=sys.stderr)
            return None

    except Exception as e:
        # Handles unexpected system errors and output full stack trace
        print(
            "Unexpected error:",
            file=sys.stderr,
        )
        traceback.print_exc(file=sys.stderr)
        return None


def main():
    if len(sys.argv) < 2:
        print("Error: No JWT-token passed in.", file=sys.stderr)
        print("Usage: python parse_jwt.py <JWT_TOKEN>", file=sys.stderr)
        sys.exit(1)

    jwt_token = sys.argv[1]
    result = parse_jwt(jwt_token)

    if result:
        print(json.dumps(result, indent=4))
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
