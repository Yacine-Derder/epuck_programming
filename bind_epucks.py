#!/usr/bin/env python3
"""Best-effort pair/trust, then bind known e-pucks for epuckupload."""

from __future__ import annotations

import argparse
import os
import re
import select
import signal
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
AGENT_PATH = "/org/epuck/agent"
PIN_AGENT_NAME = "org.epuck.PinAgent"
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


def make_pairing_agent(dbus, bus, device_path: str | None, number: int | None,
                       on_release=None):
    """Supply PINs for one pairing, or known MACs in the persistent agent."""
    class Rejected(dbus.DBusException):
        _dbus_error_name = "org.bluez.Error.Rejected"

    class Agent(dbus.service.Object):
        def check_device(self, device):
            if device_path is not None:
                if str(device) != device_path:
                    raise Rejected("Unknown e-puck")
                return number
            properties = dbus.Interface(bus.get_object("org.bluez", device),
                                        "org.freedesktop.DBus.Properties")
            address = str(properties.Get("org.bluez.Device1", "Address", timeout=2)).upper()
            for known_number, mac in EPUCKS.items():
                if address == mac.upper():
                    return known_number
            raise Rejected("Unknown e-puck; stop the e-puck PIN agent to pair other devices")

        @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="s")
        def RequestPinCode(self, device):
            return f"{self.check_device(device):04d}"

        @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="u")
        def RequestPasskey(self, device):
            return dbus.UInt32(self.check_device(device))

        @dbus.service.method("org.bluez.Agent1", in_signature="os", out_signature="")
        def AuthorizeService(self, device, uuid):
            self.check_device(device)

        @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Cancel(self):
            pass

        @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Release(self):
            if on_release is not None:
                on_release()

    return Agent(bus, AGENT_PATH)


def run_pin_agent() -> None:
    """Stay alive as the default agent for kernel RFCOMM's later PIN requests.

    The bus name provides single-instance ownership without PID files. This
    process lasts until reboot, explicit stop, or BlueZ shutdown.
    """
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    if bus.name_has_owner(PIN_AGENT_NAME):
        # Re-activate the existing daemon if the desktop replaced its agent.
        dbus.Interface(bus.get_object(PIN_AGENT_NAME, AGENT_PATH + "/control"),
                       PIN_AGENT_NAME).Activate(timeout=2)
        print("READY", flush=True)
        return
    name = dbus.service.BusName(PIN_AGENT_NAME, bus, do_not_queue=True)
    # Keep the BusName alive for the entire event loop.
    manager = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"),
                             "org.bluez.AgentManager1")
    loop = GLib.MainLoop()
    agent = make_pairing_agent(dbus, bus, None, None, on_release=loop.quit)

    class Control(dbus.service.Object):
        @dbus.service.method(PIN_AGENT_NAME, in_signature="", out_signature="")
        def Activate(self):
            manager.RequestDefaultAgent(AGENT_PATH, timeout=2)

        @dbus.service.method(PIN_AGENT_NAME, in_signature="", out_signature="")
        def Stop(self):
            GLib.idle_add(loop.quit)

    control = Control(bus, AGENT_PATH + "/control")
    registered = False
    try:
        manager.RegisterAgent(AGENT_PATH, "KeyboardOnly", timeout=2)
        registered = True
        control.Activate()
        print("READY", flush=True)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, loop.quit)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, loop.quit)
        bus.add_signal_receiver(
            lambda owner, old, new: loop.quit() if not new else None,
            signal_name="NameOwnerChanged", dbus_interface="org.freedesktop.DBus",
            arg0="org.bluez")
        loop.run()
    finally:
        if registered:
            try:
                manager.UnregisterAgent(AGENT_PATH, timeout=2)
            except Exception:
                pass
        control.remove_from_connection()
        agent.remove_from_connection()
        bus.close()


def ensure_pin_agent(dry_run: bool) -> None:
    if dry_run:
        print("Would start/reuse the background e-puck PIN agent for later connections")
        return
    worker = None
    try:
        worker = subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "--pin-agent"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
            start_new_session=True,
        )
        ready, _, _ = select.select([worker.stdout], [], [], 5)
        message = worker.stdout.readline().strip() if ready else "Startup timed out"
        if message != "READY":
            raise RuntimeError(message or "Agent exited before becoming ready")
        print("Background PIN agent ready for uploads and serial monitoring")
    except Exception as error:
        if worker is not None and worker.poll() is None:
            worker.terminate()
            try:
                worker.wait(timeout=3)
            except subprocess.TimeoutExpired:
                worker.kill()
                worker.wait()
        print(f"Warning: background PIN agent unavailable ({error}); "
              "continuing the original binding workflow.", file=sys.stderr)
    finally:
        if worker is not None and worker.stdout is not None:
            worker.stdout.close()


def stop_pin_agent() -> None:
    import dbus
    bus = dbus.SystemBus()
    if bus.name_has_owner(PIN_AGENT_NAME):
        dbus.Interface(bus.get_object(PIN_AGENT_NAME, AGENT_PATH + "/control"),
                       PIN_AGENT_NAME).Stop(timeout=2)
    print("Background e-puck PIN agent stopped")


def pair_device(number: int, timeout: int) -> None:
    """Run inside an isolated worker so BlueZ cannot block RFCOMM binding.

    Pair must be asynchronous: the GLib loop services BlueZ PIN callbacks.
    Optional imports stay here so all original operations work without them.
    """
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    objects = dbus.Interface(bus.get_object("org.bluez", "/"),
                             "org.freedesktop.DBus.ObjectManager")
    loop = GLib.MainLoop()
    scans = []
    agent = None
    manager = None
    device = None
    pairing = False
    errors = []
    device_interface = "org.bluez.Device1"
    adapter_interface = "org.bluez.Adapter1"
    properties_interface = "org.freedesktop.DBus.Properties"

    def finish(error=None):
        if error is not None:
            errors.append(error)
        loop.quit()

    def paired():
        nonlocal pairing
        pairing = False
        try:
            properties = dbus.Interface(device, properties_interface)
            properties.Set(device_interface, "Trusted", dbus.Boolean(True), timeout=2)
            finish()
        except Exception as error:
            finish(error)

    def find_device():
        nonlocal agent, manager, device, pairing
        try:
            managed = objects.GetManagedObjects(timeout=2)
            matches = [(path, interfaces[device_interface])
                       for path, interfaces in managed.items()
                       if device_interface in interfaces
                       and str(interfaces[device_interface].get("Address", "")).upper()
                       == EPUCKS[number].upper()]
            # Prefer a saved pairing if more than one adapter knows this MAC.
            matches.sort(key=lambda item: not bool(item[1].get("Paired", False)))
            if not matches:
                return True
            path, properties = matches[0]
            device = bus.get_object("org.bluez", path)
            if properties.get("Paired", False):
                paired()
                return False
            agent = make_pairing_agent(dbus, bus, str(path), number)
            manager = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"),
                                     "org.bluez.AgentManager1")
            manager.RegisterAgent(AGENT_PATH, "KeyboardOnly", timeout=2)
            pairing = True
            dbus.Interface(device, device_interface).Pair(
                reply_handler=paired, error_handler=finish, timeout=timeout)
            return False
        except Exception as error:
            finish(error)
            return False

    def start():
        try:
            managed = objects.GetManagedObjects(timeout=2)
            # A cached device can be paired without discovery.
            if any(device_interface in interfaces
                   and str(interfaces[device_interface].get("Address", "")).upper()
                   == EPUCKS[number].upper() for interfaces in managed.values()):
                find_device()
                return False
            for path, interfaces in managed.items():
                if interfaces.get(adapter_interface, {}).get("Powered", False):
                    adapter = dbus.Interface(bus.get_object("org.bluez", path),
                                             adapter_interface)
                    adapter.StartDiscovery(timeout=2)
                    scans.append(adapter)
            if not scans:
                raise RuntimeError("No powered Bluetooth adapter; turn Bluetooth on")
            GLib.timeout_add(250, find_device)
        except Exception as error:
            finish(error)
        return False

    def expired():
        finish(TimeoutError(f"Pairing/discovery timed out after {timeout}s"))
        return False

    GLib.idle_add(start)
    GLib.timeout_add_seconds(timeout, expired)
    try:
        loop.run()
    finally:
        # Stop only this application's discovery sessions; preserve saved keys.
        if pairing and device is not None:
            try:
                dbus.Interface(device, device_interface).CancelPairing(timeout=2)
            except Exception:
                pass
        for adapter in scans:
            try:
                adapter.StopDiscovery(timeout=2)
            except Exception:
                pass
        if manager is not None and agent is not None:
            try:
                manager.UnregisterAgent(AGENT_PATH, timeout=2)
            except Exception:
                pass
    if errors:
        raise RuntimeError(str(errors[0]))


def try_pair_epuck(number: int, dry_run: bool, timeout: int) -> None:
    if dry_run:
        print(f"Would pair/trust e-puck {number} ({EPUCKS[number]}) if needed")
        return
    print(f"Pairing/trusting e-puck {number} if needed...", flush=True)
    try:
        result = subprocess.run(
            [sys.executable, os.path.abspath(__file__), "--pair-device", str(number),
             "--pair-timeout", str(timeout)],
            capture_output=True, text=True, timeout=timeout + 5, check=False,
        )
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or "Pairing worker failed")
        print(f"e-puck {number}: paired and trusted")
    except Exception as error:
        print(f"Warning: e-puck {number}: automatic pairing unavailable ({error}). "
              "Continuing with rfcomm binding; manual PIN entry may be needed. "
              "Use --no-pair to skip pairing.", file=sys.stderr)


def bind_epucks(epucks: dict[int, str], dry_run: bool, replace: bool,
                pair: bool = True, pair_timeout: int = 30) -> None:
    if pair:
        ensure_pin_agent(dry_run)
    for number, mac in epucks.items():
        if pair:
            try_pair_epuck(number, dry_run, pair_timeout)
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


def positive_seconds(value: str) -> int:
    seconds = int(value)
    if seconds <= 0:
        raise argparse.ArgumentTypeError("Timeout must be a positive number of seconds")
    return seconds


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
    parser.add_argument(
        "--no-pair", action="store_true",
        help="Skip automatic pairing/trusting and only run the original rfcomm workflow.",
    )
    parser.add_argument(
        "--pair-timeout", type=positive_seconds, default=30,
        help="Pairing/discovery timeout per robot in seconds (default: 30).",
    )
    parser.add_argument(
        "--stop-agent", action="store_true",
        help="Stop the background PIN agent (e.g. before pairing unrelated devices).",
    )
    parser.add_argument("--pin-agent", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--pair-device", type=int, help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    validate_epucks()

    if args.pin_agent:
        try:
            run_pin_agent()
        except Exception as error:
            print(f"PIN agent startup failed: {error}. Ensure python3-dbus and "
                  "python3-gi are installed and run with sudo.", flush=True)
            return 1
        return 0

    if args.stop_agent:
        if args.dry_run:
            print("Would stop the background e-puck PIN agent")
        else:
            require_root(sys.argv[1:])
            stop_pin_agent()
        return 0

    if args.pair_device is not None:
        selected_epucks([args.pair_device])
        try:
            pair_device(args.pair_device, args.pair_timeout)
        except ImportError:
            print("Install python3-dbus and python3-gi (sudo apt install "
                  "python3-dbus python3-gi), then use /usr/bin/python3", file=sys.stderr)
            return 1
        except Exception as error:
            print(str(error), file=sys.stderr)
            return 1
        return 0

    if args.list:
        list_epucks()
        return 0

    epucks = selected_epucks(args.epuck_numbers)

    if not args.dry_run:
        require_root(sys.argv[1:])

    if args.release:
        release_epucks(epucks, args.dry_run)
    else:
        bind_epucks(epucks, args.dry_run, args.replace,
                    pair=not args.no_pair, pair_timeout=args.pair_timeout)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
