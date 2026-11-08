import os
import shutil
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

NAME = "net.seilent.Blockie.Settings"
PATH = "/net/seilent/Blockie/Settings"
IFACE = "net.seilent.Blockie.Settings1"
ERR = "net.seilent.Blockie.Settings.Error."
TEST = "net.seilent.Test.FakeSetd"
TEST_PATH = "/test"

XML = f"""
<node>
  <interface name="{IFACE}">
    <method name="Get"><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
    <method name="Set"><arg type="s" direction="in"/><arg type="v" direction="in"/></method>
    <signal name="Changed"><arg type="s"/><arg type="v"/></signal>
  </interface>
  <interface name="{TEST}">
    <method name="Log"><arg type="as" direction="out"/></method>
    <method name="Mode"><arg type="s" direction="in"/></method>
    <method name="GetMode"><arg type="s" direction="in"/></method>
    <method name="GetDelay"><arg type="u" direction="in"/></method>
    <method name="Own"><arg type="b" direction="in"/></method>
    <method name="Restart"/>
    <method name="Write"><arg type="s" direction="in"/><arg type="s" direction="in"/></method>
    <method name="Burst"><arg type="s" direction="in"/><arg type="u" direction="in"/></method>
    <method name="Forget"><arg type="s" direction="in"/></method>
  </interface>
</node>
"""

SET_ERRORS = {"invalid": "Invalid", "busy": "Busy", "failed": "Failed"}


class Fake:
    def __init__(self, conn):
        self.conn = conn
        self.info = Gio.DBusNodeInfo.new_for_xml(XML)
        self.log = []
        self.mode = "ok"
        self.get_mode = "ok"
        self.get_delay = 0
        self.owned = False
        self.held = []
        self.values = {"frontend.theme": "dark", "system.timezone": "Asia/Tokyo"}
        conn.register_object(PATH, self.info.lookup_interface(IFACE), self.on_call, None, None)
        conn.register_object(TEST_PATH, self.info.lookup_interface(TEST), self.on_call, None, None)

    def name(self, name, want):
        method = "RequestName" if want else "ReleaseName"
        args = GLib.Variant("(su)", (name, 4)) if want else GLib.Variant("(s)", (name,))
        self.conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method, args,
                            None, Gio.DBusCallFlags.NONE, -1, None)

    def own(self, want):
        if want != self.owned:
            self.owned = want
            self.name(NAME, want)

    def changed(self, key):
        self.conn.emit_signal(None, PATH, IFACE, "Changed",
                              GLib.Variant("(sv)", (key, GLib.Variant("s", self.values[key]))))

    def write(self, key, value):
        if self.values.get(key) != value:
            self.values[key] = value
            self.changed(key)

    def get(self, key, inv):
        self.log.append(f"Get {key}")
        if key not in self.values:
            inv.return_dbus_error(ERR + "UnknownKey", f"no key {key}")
            return
        if self.get_mode == "fail":
            inv.return_dbus_error(ERR + "Failed", "Get failed")
            return

        def answer():
            if key in self.values:
                inv.return_value(GLib.Variant("(v)", (GLib.Variant("s", self.values[key]),)))
            else:
                inv.return_dbus_error(ERR + "UnknownKey", f"no key {key}")
            return False

        if self.get_delay:
            GLib.timeout_add(self.get_delay, answer)
        else:
            answer()

    def set(self, key, value, inv):
        self.log.append(f"Set {key} {value}")
        if key not in self.values:
            inv.return_dbus_error(ERR + "UnknownKey", f"no key {key}")
            return
        mode = self.mode
        if mode in SET_ERRORS:
            inv.return_dbus_error(ERR + SET_ERRORS[mode], f"{mode} for the test")
        elif mode == "timeout":
            GLib.timeout_add(1000, lambda: inv.return_dbus_error(ERR + "Timeout", "timedated did not answer") or False)
        elif mode == "slowfail":
            GLib.timeout_add(200, lambda: inv.return_dbus_error(ERR + "Failed", "slowfail for the test") or False)
        elif mode == "hang":
            self.held.append(inv)
        elif mode == "delay":
            def late():
                self.write(key, value)
                inv.return_value(None)
                return False

            GLib.timeout_add(200, late)
        else:
            self.write(key, value)
            inv.return_value(None)

    def on_call(self, conn, sender, path, iface, method, params, inv):
        try:
            self.dispatch(method, params.unpack(), inv)
        except Exception as e:
            inv.return_dbus_error(ERR + "Failed", f"{type(e).__name__}: {e}")

    def dispatch(self, method, args, inv):
        if method == "Get":
            self.get(args[0], inv)
            return
        if method == "Set":
            self.set(args[0], args[1], inv)
            return
        if method == "Log":
            inv.return_value(GLib.Variant("(as)", (self.log,)))
            return
        if method == "Mode":
            self.mode = args[0]
        elif method == "GetMode":
            self.get_mode = args[0]
        elif method == "GetDelay":
            self.get_delay = args[0]
        elif method == "Own":
            self.own(args[0])
        elif method == "Restart":
            self.log.append("Restart")
            self.own(False)
            GLib.timeout_add(100, lambda: self.own(True) or False)
        elif method == "Write":
            self.write(args[0], args[1])
        elif method == "Burst":
            for _ in range(args[1]):
                self.changed(args[0])
        elif method == "Forget":
            self.values.pop(args[0], None)
        inv.return_value(None)


def main():
    if len(sys.argv) < 2:
        print("usage: fake_setd.py TEST [ARGS...]", file=sys.stderr)
        return 2
    if shutil.which("dbus-daemon") is None:
        print("FAIL dbus-daemon not found")
        return 1
    tmp = tempfile.mkdtemp(prefix="blockie-setd.", dir=os.environ.get("TMPDIR"))
    daemon = None
    try:
        sock = os.path.join(tmp, "bus")
        conf = os.path.join(tmp, "bus.conf")
        with open(conf, "w") as f:
            f.write(f"""<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path={sock}</listen>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
""")
        daemon = subprocess.Popen(["dbus-daemon", "--config-file=" + conf, "--nofork", "--nopidfile"])
        for _ in range(50):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        addr = "unix:path=" + sock
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        conn = Gio.DBusConnection.new_for_address_sync(addr, flags, None, None)
        fake = Fake(conn)
        fake.name(TEST, True)
        child = subprocess.Popen(sys.argv[1:], env=dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=addr))
        loop = GLib.MainLoop()
        rc = [1]

        def watch():
            r = child.poll()
            if r is None:
                return True
            rc[0] = r
            loop.quit()
            return False

        def overdue():
            print("FAIL test did not finish in 120 s")
            child.kill()
            return False

        GLib.timeout_add(20, watch)
        GLib.timeout_add_seconds(120, overdue)
        loop.run()
        return rc[0]
    finally:
        if daemon is not None:
            daemon.kill()
            daemon.wait()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
