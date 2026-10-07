#!/usr/bin/env python3
"""Bind known e-pucks to matching /dev/rfcomm numbers for epuckupload."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys


# Add new robots here:
#   epuck_number: "Bluetooth MAC address",
EPUCKS = {
    99: "08:00:17:20:53:76",
    51: "08:00:17:20:6D:6C",
    76: "08:00:17:20:72:06",
    90: "08:00:17:20:62:72",
    97: "08:00:17:20:4E:6F",
    91: "08:00:17:20:4E:11",
    57: "08:00:17:20:72:D1",
}

RFCOMM_CHANNEL = "1"
MAC_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")


def rfcomm_device(epuck_number: int) -> str:
    return f"/dev/rfcomm{epuck_number}"


def require_root(args: list[str]) -> None:
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        print("Need root privileges for rfcomm; re-running with sudo...")
        os.execvp("sudo", ["sudo", sys.executable, os.path.abspath(__file__), *args])


def validate_epucks() -> None:
    bad = [
        f"e-puck {number}: {mac}"
        for number, mac in EPUCKS.items()
        if not MAC_RE.match(mac)
    ]
    if bad:
        raise SystemExit("Invalid MAC address in EPUCKS:\n  " + "\n  ".join(bad))


def selected_epucks(numbers: list[int]) -> dict[int, str]:
    if not numbers:
        return dict(sorted(EPUCKS.items()))

    unknown = [number for number in numbers if number not in EPUCKS]
    if unknown:
        known = ", ".join(str(number) for number in sorted(EPUCKS))
        raise SystemExit(f"Unknown e-puck(s): {unknown}. Known e-pucks: {known}")

    return {number: EPUCKS[number] for number in numbers}


def run_command(command: list[str], dry_run: bool) -> None:
    print(" ".join(command))
    if not dry_run:
        subprocess.run(command, check=True)


def bind_epucks(epucks: dict[int, str], dry_run: bool, replace: bool) -> None:
    for number, mac in epucks.items():
        device = rfcomm_device(number)

        if replace:
            release_command = ["rfcomm", "release", device]
            print(" ".join(release_command))
            if not dry_run:
                subprocess.run(release_command, check=False)

        run_command(["rfcomm", "bind", device, mac, RFCOMM_CHANNEL], dry_run)


def release_epucks(epucks: dict[int, str], dry_run: bool) -> None:
    for number in epucks:
        run_command(["rfcomm", "release", rfcomm_device(number)], dry_run)


def list_epucks() -> None:
    for number, mac in sorted(EPUCKS.items()):
        print(f"e-puck {number}: {mac} -> {rfcomm_device(number)}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bind e-pucks to /dev/rfcomm<number> for epuckupload.",
    )
    parser.add_argument(
        "epuck_numbers",
        nargs="*",
        type=int,
        help="Specific e-puck numbers to bind. Defaults to all known e-pucks.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Show the configured e-puck MAC addresses and exit.",
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Release the selected rfcomm devices instead of binding them.",
    )
    parser.add_argument(
        "--replace",
        action="store_true",
        help="Release each selected rfcomm device before binding it.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the rfcomm commands without running them.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    validate_epucks()

    if args.list:
        list_epucks()
        return 0

    epucks = selected_epucks(args.epuck_numbers)

    if not args.dry_run:
        require_root(sys.argv[1:])

    if args.release:
        release_epucks(epucks, args.dry_run)
    else:
        bind_epucks(epucks, args.dry_run, args.replace)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
