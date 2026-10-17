"""Create private ESPHome secrets without echoing passwords or overwriting a file."""

import argparse
import base64
from getpass import getpass
import json
from pathlib import Path
import re
import secrets


def public_mac(value):
    # The measured BLE address type must be public; spelling alone cannot prove that.
    value = value.upper()
    if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", value):
        raise argparse.ArgumentTypeError("Use a measured public BLE MAC in AA:BB:CC:DD:EE:FF form")
    if value in ("00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"):
        raise argparse.ArgumentTypeError("Zero and broadcast addresses are invalid")
    return value


def write_secrets(path, ssid, password, remote_mac, speaker_mac):
    remote_mac, speaker_mac = public_mac(remote_mac), public_mac(speaker_mac)
    if remote_mac == speaker_mac:
        raise ValueError("Remote and speaker MACs must differ")
    if not ssid or not password:
        raise ValueError("Wi-Fi SSID and password must not be empty")
    values = {
        "wifi_ssid": ssid,
        "wifi_password": password,
        "jbl4305p_api_key": base64.b64encode(secrets.token_bytes(32)).decode("ascii"),
        "jbl4305p_ota_password": secrets.token_urlsafe(32),
        "jbl4305p_remote_mac": remote_mac,
        "jbl4305p_speaker_mac": speaker_mac,
    }
    # JSON string literals are valid YAML strings; x mode refuses existing secrets.
    content = "# Private deployment secrets. Never commit or publish this file.\n"
    content += "".join(f"{key}: {json.dumps(value, ensure_ascii=False)}\n" for key, value in values.items())
    with path.open("x", encoding="utf-8", newline="\n") as output:
        output.write(content)


def self_test():
    from tempfile import TemporaryDirectory

    for invalid in ("00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "not-a-mac"):
        try:
            public_mac(invalid)
        except argparse.ArgumentTypeError:
            continue
        raise AssertionError("Invalid MAC accepted")
    with TemporaryDirectory() as folder:
        target = Path(folder) / "secrets.yaml"
        args = (target, 'test " Wi-Fi', "test\npassword", "02:00:00:00:00:01", "02:00:00:00:00:02")
        write_secrets(*args)
        values = {key: json.loads(value) for key, value in
                  (line.split(": ", 1) for line in target.read_text(encoding="utf-8").splitlines() if not line.startswith("#"))}
        assert len(base64.b64decode(values["jbl4305p_api_key"], validate=True)) == 32
        assert values["wifi_password"] == "test\npassword"
        assert len(values["jbl4305p_ota_password"]) >= 32
        try:
            write_secrets(*args)
        except FileExistsError:
            pass
        else:
            raise AssertionError("Existing file overwritten")
    print("PASS: MAC validation, credential generation, YAML escaping and overwrite refusal")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote-mac", type=public_mac, help="Measured public BLE identity of the original remote")
    parser.add_argument("--speaker-mac", type=public_mac, help="Measured public BLE identity of the speaker")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "firmware/esphome/secrets.yaml")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.remote_mac or not args.speaker_mac:
        parser.error("--remote-mac and --speaker-mac are required")
    if args.output.exists():
        parser.error("Output already exists; refusing to read or overwrite existing secrets")
    try:
        write_secrets(args.output, input("Wi-Fi SSID: "), getpass("Wi-Fi password (hidden): "),
                      args.remote_mac, args.speaker_mac)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Could not create secrets: {error}\n")
    print(f"Private secrets written to {args.output}. Credentials were not printed.")


if __name__ == "__main__":
    main()
