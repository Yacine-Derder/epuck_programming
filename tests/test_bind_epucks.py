"""Hardware-free regression tests: python3 -m unittest discover -s tests -v."""
import contextlib
import io
import subprocess
import sys
import types
import unittest
from unittest.mock import Mock, patch, call

import bind_epucks as script


class FakeBlueZ:
    """Simulate the D-Bus callbacks, discovery and event loop without Bluetooth."""
    def __init__(self, paired=False, present=True, discover=False, powered=True,
                 pair_error=None, trust_error=None):
        self.path = '/org/bluez/hci0/dev_' + script.EPUCKS[91].replace(':', '_')
        self.device = Mock()
        self.device.Set.side_effect = trust_error
        self.adapter = Mock()
        self.manager = Mock()
        self.objects = Mock()
        self.bus = Mock()
        self.ready = []
        self.polls = []
        self.deadlines = []
        self.stopped = False
        self.agent = None
        self.calls = 0

        def managed(**kwargs):
            self.calls += 1
            result = {'/org/bluez/hci0': {'org.bluez.Adapter1': {'Powered': powered}}}
            if present or (discover and self.calls >= 3):
                result[self.path] = {'org.bluez.Device1': {
                    'Address': script.EPUCKS[91].lower(), 'Paired': paired}}
            return result
        self.objects.GetManagedObjects.side_effect = managed

        def pair(**kwargs):
            # Emulate a PIN request arriving while asynchronous Pair is pending.
            def complete():
                if pair_error:
                    kwargs['error_handler'](pair_error)
                else:
                    assert self.agent.RequestPinCode(self.path) == '0091'
                    kwargs['reply_handler']()
                return False
            self.ready.append(complete)
        self.device.Pair.side_effect = pair
        self.bus.get_object.side_effect = lambda name, path: path
        def interface(obj, name):
            return {'org.freedesktop.DBus.ObjectManager': self.objects,
                    'org.bluez.AgentManager1': self.manager,
                    'org.bluez.Adapter1': self.adapter,
                    'org.bluez.Device1': self.device,
                    'org.freedesktop.DBus.Properties': self.device}[name]
        owner = self
        class Object:
            def __init__(self, bus, path):
                owner.agent = self
        dbus = types.ModuleType('dbus')
        dbus.__path__ = []
        dbus.DBusException = type('DBusException', (Exception,), {})
        dbus.UInt32 = int
        dbus.Boolean = bool
        dbus.SystemBus = lambda: self.bus
        dbus.Interface = interface
        service = types.ModuleType('dbus.service')
        service.Object = Object
        service.method = lambda *a, **kw: lambda f: f
        dbus.service = service
        mainloop = types.ModuleType('dbus.mainloop')
        mainloop.__path__ = []
        glib = types.ModuleType('dbus.mainloop.glib')
        glib.DBusGMainLoop = Mock()
        class Loop:
            def quit(self):
                owner.stopped = True
            def run(self):
                for _ in range(10):
                    if owner.stopped:
                        return
                    if owner.ready:
                        owner.ready.pop(0)()
                    elif owner.polls:
                        cb = owner.polls.pop(0)
                        if cb():
                            owner.polls.append(cb)
                        if owner.calls >= 5 and not owner.stopped:
                            owner.deadlines.pop(0)()
                    else:
                        owner.deadlines.pop(0)()
                raise AssertionError('Event loop did not finish')
        GLib = types.SimpleNamespace(
            MainLoop=Loop,
            idle_add=lambda cb: self.ready.append(cb),
            timeout_add=lambda ms, cb: self.polls.append(cb),
            timeout_add_seconds=lambda sec, cb: self.deadlines.append(cb))
        gi = types.ModuleType('gi')
        gi.__path__ = []
        repository = types.ModuleType('gi.repository')
        repository.GLib = GLib
        self.modules = {'dbus': dbus, 'dbus.service': service,
                        'dbus.mainloop': mainloop, 'dbus.mainloop.glib': glib,
                        'gi': gi, 'gi.repository': repository}

    def run(self):
        with patch.dict(sys.modules, self.modules):
            script.pair_device(91, 1)


class PairingTests(unittest.TestCase):
    def test_pin_and_unknown_device_rejection(self):
        fake = FakeBlueZ()
        for number in script.EPUCKS:
            agent = script.make_pairing_agent(fake.modules['dbus'], fake.bus,
                                               fake.path, number)
            self.assertEqual(agent.RequestPinCode(fake.path), f'{number:04d}')
            self.assertEqual(agent.RequestPasskey(fake.path), number)
            with self.assertRaises(Exception):
                agent.RequestPinCode('/unknown')
            with self.assertRaises(Exception):
                agent.AuthorizeService('/unknown', 'uuid')

    def test_pair_then_trust_and_unregister(self):
        fake = FakeBlueZ()
        fake.run()
        fake.device.Pair.assert_called_once()
        fake.device.Set.assert_called_once_with('org.bluez.Device1', 'Trusted', True, timeout=2)
        fake.manager.RegisterAgent.assert_called_once_with(script.AGENT_PATH, 'KeyboardOnly', timeout=2)
        fake.manager.UnregisterAgent.assert_called_once_with(script.AGENT_PATH, timeout=2)
        fake.manager.RequestDefaultAgent.assert_not_called()
        fake.device.CancelPairing.assert_not_called()
        fake.adapter.StartDiscovery.assert_not_called()

    def test_saved_pairing_is_reused(self):
        fake = FakeBlueZ(paired=True)
        fake.run()
        fake.device.Pair.assert_not_called()
        fake.manager.RegisterAgent.assert_not_called()
        fake.device.Set.assert_called_once()

    def test_discover_pair_cleanup(self):
        fake = FakeBlueZ(present=False, discover=True)
        fake.run()
        fake.adapter.StartDiscovery.assert_called_once()
        fake.adapter.StopDiscovery.assert_called_once()
        fake.device.Pair.assert_called_once()

    def test_discovery_timeout_stops_scan(self):
        fake = FakeBlueZ(present=False)
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            fake.run()
        fake.adapter.StopDiscovery.assert_called_once()
        fake.device.Pair.assert_not_called()

    def test_powered_off_fails_without_changes(self):
        fake = FakeBlueZ(present=False, powered=False)
        with self.assertRaisesRegex(RuntimeError, 'No powered Bluetooth adapter'):
            fake.run()
        fake.adapter.StartDiscovery.assert_not_called()
        fake.device.Set.assert_not_called()

    def test_pair_failure_does_not_trust(self):
        fake = FakeBlueZ(pair_error=RuntimeError('AuthenticationFailed'))
        with self.assertRaisesRegex(RuntimeError, 'AuthenticationFailed'):
            fake.run()
        fake.device.Set.assert_not_called()
        fake.device.CancelPairing.assert_called_once()
        fake.manager.UnregisterAgent.assert_called_once()

    def test_trust_failure_cleans_up(self):
        fake = FakeBlueZ(trust_error=RuntimeError('trust failed'))
        with self.assertRaisesRegex(RuntimeError, 'trust failed'):
            fake.run()
        fake.manager.UnregisterAgent.assert_called_once()


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.output = contextlib.ExitStack()
        self.output.enter_context(contextlib.redirect_stdout(io.StringIO()))
        self.output.enter_context(contextlib.redirect_stderr(io.StringIO()))
        self.addCleanup(self.output.close)

    def main(self, *args):
        with patch.object(sys, 'argv', ['bind_epucks.py', *args]):
            return script.main()

    def test_pairing_failures_still_bind_each_robot(self):
        failures = [subprocess.CompletedProcess([], 1, '', 'missing dependency'),
                    subprocess.TimeoutExpired('pair', 1), OSError('worker unavailable')]
        for failure in failures:
            with self.subTest(failure=failure), patch.object(script.subprocess, 'run') as run:
                run.side_effect = [failure, None, failure, None]
                script.bind_epucks(script.selected_epucks([91, 76]), False, False)
                self.assertEqual(run.call_args_list[1], call(
                    ['rfcomm', 'bind', '/dev/rfcomm91', script.EPUCKS[91], '1'], check=True))
                self.assertEqual(run.call_args_list[3], call(
                    ['rfcomm', 'bind', '/dev/rfcomm76', script.EPUCKS[76], '1'], check=True))

    def test_successful_worker_is_bounded(self):
        with patch.object(script.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            script.try_pair_epuck(91, False, 30)
            self.assertEqual(run.call_args.kwargs['timeout'], 35)

    def test_replace_even_after_pairing_failure_and_release_failure(self):
        with patch.object(script.subprocess, 'run') as run:
            run.side_effect = [OSError('pair failed'), subprocess.CompletedProcess([], 1), None]
            script.bind_epucks(script.selected_epucks([91]), False, True)
            self.assertEqual(run.call_args_list[1], call(['rfcomm', 'release', '/dev/rfcomm91'], check=False))
            self.assertEqual(run.call_args_list[2].kwargs, {'check': True})

    def test_rfcomm_failure_still_propagates(self):
        with patch.object(script.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'rfcomm')):
            with self.assertRaises(subprocess.CalledProcessError):
                script.bind_epucks(script.selected_epucks([91]), False, False, pair=False)

    def test_dry_run_has_no_side_effects(self):
        with patch.object(script.subprocess, 'run') as run, patch.object(script, 'require_root') as root:
            for args in [('--dry-run',), ('--dry-run', '--replace', '91'),
                         ('--dry-run', '--release', '91'), ('--dry-run', '--no-pair', '76')]:
                self.assertEqual(self.main(*args), 0)
            run.assert_not_called()
            root.assert_not_called()

    def test_list_without_root_pairing_or_subprocess(self):
        with patch.object(script, 'try_pair_epuck') as pair, patch.object(script, 'require_root') as root:
            self.assertEqual(self.main('--list'), 0)
            pair.assert_not_called()
            root.assert_not_called()

    def test_release_does_not_pair(self):
        with patch.object(script, 'require_root'), patch.object(script, 'try_pair_epuck') as pair, patch.object(script.subprocess, 'run') as run:
            self.main('--release', '91')
            pair.assert_not_called()
            run.assert_called_once_with(['rfcomm', 'release', '/dev/rfcomm91'], check=True)

    def test_no_pair_preserves_binding(self):
        with patch.object(script, 'require_root'), patch.object(script, 'try_pair_epuck') as pair, patch.object(script.subprocess, 'run') as run:
            self.main('--no-pair', '91')
            pair.assert_not_called()
            run.assert_called_once_with(['rfcomm', 'bind', '/dev/rfcomm91', script.EPUCKS[91], '1'], check=True)

    def test_invalid_input_before_side_effects(self):
        with patch.object(script.subprocess, 'run') as run, patch.object(script, 'require_root') as root:
            for args in [('12345',), ('--pair-timeout', '0'), ('--pair-timeout', '-1')]:
                with self.assertRaises(SystemExit):
                    self.main(*args)
            with patch.dict(script.EPUCKS, {91: 'bad MAC'}):
                with self.assertRaises(SystemExit):
                    self.main('--dry-run')
            run.assert_not_called()
            root.assert_not_called()

    def test_selection_sorted_and_specific(self):
        self.assertEqual(list(script.selected_epucks([])), sorted(script.EPUCKS))
        self.assertEqual(list(script.selected_epucks([91, 76])), [91, 76])

    def test_worker_missing_import_is_helpful(self):
        with patch.object(script, 'pair_device', side_effect=ImportError()):
            self.assertEqual(self.main('--pair-device', '91'), 1)


if __name__ == '__main__':
    unittest.main()
