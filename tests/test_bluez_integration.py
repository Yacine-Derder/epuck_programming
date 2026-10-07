"""Exercise real dbus-python/GLib against a private fake BlueZ, never hardware."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

try:
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib
except ImportError:
    dbus = None

import bind_epucks as script


@unittest.skipUnless(dbus is not None and shutil.which('dbus-daemon'),
                     'Needs dbus-daemon, python3-dbus and python3-gi')
class PrivateBlueZTests(unittest.TestCase):
    def exercise(self, saved=False, reject=False, absent=False, persistent=False, number=91, reuse=False):
        DBusGMainLoop(set_as_default=True)
        daemon = subprocess.Popen(
            ['dbus-daemon', '--session', '--nofork', '--print-address=1'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.addCleanup(daemon.stdout.close)
        self.addCleanup(daemon.wait)
        self.addCleanup(daemon.terminate)
        address = daemon.stdout.readline().strip()
        bus = dbus.bus.BusConnection(address)
        self.addCleanup(bus.close)
        name = dbus.service.BusName('org.bluez', bus)
        path = '/org/bluez/hci0/dev_' + script.EPUCKS[number].replace(':', '_')
        state = {'paired': saved, 'trusted': False, 'pin': None, 'registered': False,
                 'unregistered': False, 'pair_calls': 0, 'scanning': False,
                 'default_sender': None, 'later_pins': [], 'unknown_rejected': False,
                 'reuse_ready': False}

        class Root(dbus.service.Object):
            @dbus.service.method('org.freedesktop.DBus.ObjectManager',
                                 out_signature='a{oa{sa{sv}}}')
            def GetManagedObjects(self):
                objects = {'/org/bluez/hci0': {'org.bluez.Adapter1': {'Powered': True}}}
                if not absent:
                    objects[path] = {'org.bluez.Device1': {
                        'Address': script.EPUCKS[number], 'Paired': state['paired'],
                        'Trusted': state['trusted']}}
                return objects

        class Manager(dbus.service.Object):
            @dbus.service.method('org.bluez.AgentManager1', in_signature='os')
            def RegisterAgent(self, agent, capability):
                assert str(agent) == script.AGENT_PATH
                assert str(capability) == 'KeyboardOnly'
                state['registered'] = True

            @dbus.service.method('org.bluez.AgentManager1', in_signature='o', sender_keyword='sender')
            def RequestDefaultAgent(self, agent, sender=None):
                state['default_sender'] = sender

            @dbus.service.method('org.bluez.AgentManager1', in_signature='o')
            def UnregisterAgent(self, agent):
                state['unregistered'] = True

        class Device(dbus.service.Object):
            @dbus.service.method('org.bluez.Device1', sender_keyword='sender',
                                 async_callbacks=('reply', 'error'))
            def Pair(self, reply, error, sender=None):
                state['pair_calls'] += 1
                agent = dbus.Interface(bus.get_object(sender, script.AGENT_PATH),
                                       'org.bluez.Agent1')
                def got_pin(pin):
                    state['pin'] = str(pin)
                    if reject:
                        error(dbus.exceptions.DBusException(
                            'AuthenticationFailed', name='org.bluez.Error.AuthenticationFailed'))
                    else:
                        state['paired'] = True
                        reply()
                agent.RequestPinCode(path, reply_handler=got_pin, error_handler=error)

            @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
            def Get(self, interface, prop):
                return script.EPUCKS[number]

            @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ssv')
            def Set(self, interface, prop, value):
                assert str(interface) == 'org.bluez.Device1' and str(prop) == 'Trusted'
                state['trusted'] = bool(value)

            @dbus.service.method('org.bluez.Device1')
            def CancelPairing(self):
                pass

        class Adapter(dbus.service.Object):
            @dbus.service.method('org.bluez.Adapter1')
            def StartDiscovery(self):
                state['scanning'] = True

            @dbus.service.method('org.bluez.Adapter1')
            def StopDiscovery(self):
                state['scanning'] = False

        exports = [Root(bus, '/'), Manager(bus, '/org/bluez'),
                   Device(bus, path), Adapter(bus, '/org/bluez/hci0')]
        worker = subprocess.Popen(
            [sys.executable, str(Path(script.__file__).resolve()),
             *(['--pin-agent'] if persistent else ['--pair-device', str(number), '--pair-timeout', '1'])],
            env={**os.environ, 'DBUS_SYSTEM_BUS_ADDRESS': address},
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        loop = GLib.MainLoop()
        callback_started = False
        reuser = None
        def cleanup_reuser():
            if reuser is not None:
                if reuser.poll() is None:
                    reuser.kill()
                reuser.communicate()
        self.addCleanup(cleanup_reuser)
        def poll():
            nonlocal callback_started, reuser
            if persistent and reuse and state['default_sender']:
                if reuser is None:
                    reuser = subprocess.Popen(
                        [sys.executable, str(Path(script.__file__).resolve()), '--pin-agent'],
                        env={**os.environ, 'DBUS_SYSTEM_BUS_ADDRESS': address},
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    return True
                if reuser.poll() is None:
                    return True
                if not state['reuse_ready']:
                    output, error = reuser.communicate()
                    state['reuse_ready'] = reuser.returncode == 0 and output.strip() == 'READY'
            if persistent and state['default_sender'] and not callback_started:
                callback_started = True
                agent = dbus.Interface(bus.get_object(state['default_sender'], script.AGENT_PATH),
                                       'org.bluez.Agent1')
                def stop(error=None):
                    if error is not None:
                        state['unknown_rejected'] = True
                    dbus.Interface(bus.get_object(script.PIN_AGENT_NAME,
                                   script.AGENT_PATH + '/control'), script.PIN_AGENT_NAME).Stop(
                                       reply_handler=lambda: None, error_handler=lambda err: None)
                def second(pin):
                    state['later_pins'].append(str(pin))
                    # A non-device path cannot supply a known Address property.
                    agent.RequestPinCode('/unknown', reply_handler=lambda pin: stop(),
                                         error_handler=stop)
                def first(pin):
                    state['later_pins'].append(str(pin))
                    agent.RequestPinCode(path, reply_handler=second, error_handler=stop)
                agent.RequestPinCode(path, reply_handler=first, error_handler=stop)
            if worker.poll() is not None:
                loop.quit()
                return False
            return True
        poll_source = GLib.timeout_add(50, poll)
        def watchdog():
            worker.kill()
            loop.quit()
            return False
        watchdog_source = GLib.timeout_add_seconds(8, watchdog)
        try:
            loop.run()
            stdout, stderr = worker.communicate(timeout=2)
        finally:
            GLib.source_remove(watchdog_source)
            if worker.poll() is None:
                GLib.source_remove(poll_source)
                worker.kill()
                worker.communicate()
            for obj in exports:
                obj.remove_from_connection()
        return worker.returncode, stderr, state

    def test_persistent_default_agent_handles_later_requests(self):
        code, error, state = self.exercise(persistent=True, number=76)
        self.assertEqual(code, 0, error)
        self.assertEqual(state['pair_calls'], 0)
        self.assertEqual(state['later_pins'], ['0076', '0076'])
        self.assertTrue(state['unknown_rejected'])
        self.assertTrue(state['unregistered'])

    def test_agent_is_reused_on_second_run(self):
        code, error, state = self.exercise(persistent=True, number=76, reuse=True)
        self.assertEqual(code, 0, error)
        self.assertTrue(state['reuse_ready'])
        self.assertEqual(state['later_pins'], ['0076', '0076'])

    def test_real_agent_pin_callback_and_trust(self):
        code, error, state = self.exercise()
        self.assertEqual(code, 0, error)
        self.assertEqual(state['pin'], '0091')
        self.assertTrue(state['trusted'])
        self.assertTrue(state['unregistered'])

    def test_saved_pairing_no_pin_callback(self):
        code, error, state = self.exercise(saved=True)
        self.assertEqual(code, 0, error)
        self.assertEqual(state['pair_calls'], 0)
        self.assertTrue(state['trusted'])

    def test_authentication_failure_is_reported(self):
        code, error, state = self.exercise(reject=True)
        self.assertEqual(code, 1)
        self.assertIn('AuthenticationFailed', error)
        self.assertFalse(state['trusted'])
        self.assertTrue(state['unregistered'])

    def test_discovery_timeout_cleanup(self):
        code, error, state = self.exercise(absent=True)
        self.assertEqual(code, 1)
        self.assertIn('timed out', error)
        self.assertFalse(state['scanning'])


if __name__ == '__main__':
    unittest.main()
