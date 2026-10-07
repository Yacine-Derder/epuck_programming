#!/usr/bin/env python3
"""Open serial monitors for e-pucks bound on matching /dev/rfcomm numbers."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from bind_epucks import EPUCKS, rfcomm_device, selected_epucks, validate_epucks


DEFAULT_BAUD = "115200"
DEFAULT_MONITOR = "minicom"
MONITOR_PROGRAMS = ("minicom", "picocom", "screen")
TERMINAL_PROGRAMS = (
    "gnome-terminal",
    "x-terminal-emulator",
    "xterm",
    "konsole",
)
BIND_SCRIPT = Path(__file__).resolve().with_name("bind_epucks.py")


def choose_program(requested: str) -> str:
    if requested != "auto":
        if shutil.which(requested) is None:
            raise SystemExit(f"Could not find '{requested}' in PATH.")
        return requested

    for program in (DEFAULT_MONITOR, "picocom", "screen"):
        if shutil.which(program):
            return program

    raise SystemExit(
        "Could not find a serial monitor. Install minicom, picocom, or screen."
    )


def monitor_command(program: str, device: str, baud: str) -> list[str]:
    if program == "minicom":
        return ["minicom", "-D", device, "-b", baud]
    if program == "picocom":
        return ["picocom", "-b", baud, device]
    if program == "screen":
        return ["screen", device, baud]
    raise SystemExit(f"Unsupported monitor program: {program}")


def exit_hint(program: str) -> str:
    if program == "minicom":
        return "Exit minicom with Ctrl-A, then X."
    if program == "picocom":
        return "Exit picocom with Ctrl-A, then Ctrl-X."
    if program == "screen":
        return "Exit screen with Ctrl-A, then K."
    return "Use the monitor program's exit command to close."


def choose_terminal(requested: str) -> str:
    if requested != "auto":
        if shutil.which(requested) is None:
            raise SystemExit(f"Could not find terminal '{requested}' in PATH.")
        return requested

    for terminal in TERMINAL_PROGRAMS:
        if shutil.which(terminal):
            return terminal

    raise SystemExit(
        "Could not find a terminal emulator. Run one e-puck at a time, or install "
        "gnome-terminal, xterm, or another supported terminal."
    )


def terminal_shell_command(
    number: int,
    mac: str,
    device: str,
    program: str,
    command: list[str],
) -> str:
    lines = [
        f"printf '%s\\n' {shlex.quote(f'e-puck {number} ({mac}) on {device}')}",
        f"printf '%s\\n' {shlex.quote(exit_hint(program))}",
        "printf '\\n'",
        shlex.join(command),
        "status=$?",
        "printf '\\nMonitor exited with status %s.\\n' \"$status\"",
        "read -r -p 'Press Enter to close this window...'",
    ]
    return "; ".join(lines)


def terminal_command(terminal: str, title: str, shell_command: str) -> list[str]:
    if terminal == "gnome-terminal":
        return [terminal, "--title", title, "--", "bash", "-lc", shell_command]
    if terminal == "x-terminal-emulator":
        return [terminal, "-T", title, "-e", "bash", "-lc", shell_command]
    if terminal == "xterm":
        return [terminal, "-T", title, "-e", "bash", "-lc", shell_command]
    if terminal == "konsole":
        return [terminal, "-p", f"tabtitle={title}", "-e", "bash", "-lc", shell_command]
    raise SystemExit(f"Unsupported terminal: {terminal}")


def list_epucks() -> None:
    for number, mac in sorted(EPUCKS.items()):
        print(f"e-puck {number}: {mac} -> {rfcomm_device(number)}")


def ensure_devices(epucks: dict[int, str]) -> None:
    missing = [
        f"e-puck {number}: {rfcomm_device(number)}"
        for number in sorted(epucks)
        if not os.path.exists(rfcomm_device(number))
    ]
    if missing:
        raise SystemExit(
            "These rfcomm devices do not exist yet:\n  "
            + "\n  ".join(missing)
            + "\nRun ./bind_epucks.py first, or use ./monitor_epucks.py --bind."
        )


def bind_first(epucks: dict[int, str]) -> None:
    command = [sys.executable, str(BIND_SCRIPT), *[str(number) for number in epucks]]
    subprocess.run(command, check=True)


def run_current_terminal(
    number: int,
    mac: str,
    program: str,
    baud: str,
    dry_run: bool,
) -> int:
    device = rfcomm_device(number)
    command = monitor_command(program, device, baud)
    print(f"Opening e-puck {number} ({mac}) on {device}")
    print(exit_hint(program))

    if dry_run:
        print(shlex.join(command))
        return 0

    return subprocess.run(command).returncode


def open_terminal_windows(
    epucks: dict[int, str],
    terminal: str,
    program: str,
    baud: str,
    dry_run: bool,
) -> int:
    for number, mac in epucks.items():
        device = rfcomm_device(number)
        command = monitor_command(program, device, baud)
        shell_command = terminal_shell_command(number, mac, device, program, command)
        title = f"e-puck {number}"
        full_command = terminal_command(terminal, title, shell_command)

        if dry_run:
            print(shlex.join(full_command))
        else:
            subprocess.Popen(full_command)

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Open bidirectional serial monitors for e-pucks over RFCOMM.",
    )
    parser.add_argument(
        "epuck_numbers",
        nargs="*",
        type=int,
        help="Specific e-puck numbers to monitor. Defaults to all known e-pucks.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Show configured e-pucks and exit.",
    )
    parser.add_argument(
        "--bind",
        action="store_true",
        help="Run bind_epucks.py for the selected e-pucks before monitoring.",
    )
    parser.add_argument(
        "--program",
        choices=("auto", *MONITOR_PROGRAMS),
        default="auto",
        help="Serial monitor to use. Default: minicom when available.",
    )
    parser.add_argument(
        "--baud",
        default=DEFAULT_BAUD,
        help=f"Baud rate passed to the monitor program. Default: {DEFAULT_BAUD}.",
    )
    parser.add_argument(
        "--new-windows",
        action="store_true",
        help="Open a new terminal window even when monitoring only one e-puck.",
    )
    parser.add_argument(
        "--terminal",
        choices=("auto", *TERMINAL_PROGRAMS),
        default="auto",
        help="Terminal emulator for multi-robot monitoring.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the commands without running them.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    validate_epucks()

    if args.list:
        list_epucks()
        return 0

    epucks = selected_epucks(args.epuck_numbers)
    program = choose_program(args.program)

    if args.bind and not args.dry_run:
        bind_first(epucks)

    if not args.dry_run:
        ensure_devices(epucks)

    if len(epucks) == 1 and not args.new_windows:
        number, mac = next(iter(epucks.items()))
        return run_current_terminal(number, mac, program, args.baud, args.dry_run)

    terminal = choose_terminal(args.terminal)
    return open_terminal_windows(epucks, terminal, program, args.baud, args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
