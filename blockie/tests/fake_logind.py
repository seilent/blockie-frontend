import os
import shutil
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

NAME = "org.freedesktop.login1"
PATH = "/org/freedesktop/login1"
MANAGER = "org.freedesktop.login1.Manager"
SESSION = "org.freedesktop.login1.Session"
PROPS = "org.freedesktop.DBus.Properties"
TEST = "net.seilent.Test.FakeLogind"
TEST_PATH = "/test"

XML = f"""
<node>
  <interface name="{MANAGER}">
    <method name="ListSessions"><arg type="a(susso)" direction="out"/></method>
  </interface>
  <interface name="{SESSION}">
    <method name="SetBrightness">
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="u" direction="in"/>
    </method>
    <property name="Active" type="b" access="read"/>
  </interface>
  <interface name="{TEST}">
    <method name="Log"><arg type="as" direction="out"/></method>
    <method name="Mode"><arg type="s" direction="in"/></method>
    <method name="Seats"><arg type="s" direction="in"/></method>
  </interface>
</node>
"""


def session_path(n):
    return f"{PATH}/session/_3{n}"


class Fake:
    def __init__(self, conn):
        self.conn = conn
        self.info = Gio.DBusNodeInfo.new_for_xml(XML)
        self.log = []
        self.mode = "ok"
        self.seats = "one"
        conn.register_object(PATH, self.info.lookup_interface(MANAGER), self.on_call, None, None)
        for n in range(1, 5):
            conn.register_object(session_path(n), self.info.lookup_interface(SESSION), self.on_call, None, None)
        conn.register_object(TEST_PATH, self.info.lookup_interface(TEST), self.on_call, None, None)

    def name(self, name):
        self.conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                            GLib.Variant("(su)", (name, 4)), None, Gio.DBusCallFlags.NONE, -1, None)

    def sessions(self):
        uid = os.getuid()
        rows = [("1", uid + 1, "other", "seat0", session_path(1)), ("2", uid, "blockie-test", "", session_path(2))]
        rows.append(("3", uid, "blockie-test", "seat0", session_path(3)))
        if self.seats == "two":
            rows.append(("4", uid, "blockie-test", "seat0", session_path(4)))
        if self.seats == "none":
            rows = rows[:2]
        return rows

    def active(self, path):
        return self.seats != "two" or path == session_path(4)

    def set_brightness(self, path, params, inv):
        sub, name, raw = params.unpack()
        self.log.append(f"SetBrightness {path} {sub} {name} {raw}")
        if self.mode == "deny":
            inv.return_dbus_error("org.freedesktop.DBus.Error.AccessDenied", "denied for the test")
        elif self.mode == "delay":
            GLib.timeout_add(200, lambda: inv.return_value(None) or False)
        else:
            inv.return_value(None)

    def on_call(self, conn, sender, path, iface, method, params, inv):
        try:
            self.dispatch(path, iface, method, params, inv)
        except Exception as e:
            inv.return_dbus_error("org.freedesktop.DBus.Error.Failed", f"{type(e).__name__}: {e}")

    def dispatch(self, path, iface, method, params, inv):
        if iface == MANAGER and method == "ListSessions":
            self.log.append("ListSessions")
            if self.mode == "nolist":
                inv.return_dbus_error("org.freedesktop.DBus.Error.ServiceUnknown", "gone for the test")
                return
            inv.return_value(GLib.Variant("(a(susso))", (self.sessions(),)))
            return
        if iface == PROPS and method == "Get":
            self.log.append(f"Active {path}")
            inv.return_value(GLib.Variant("(v)", (GLib.Variant("b", self.active(path)),)))
            return
        if iface == SESSION and method == "SetBrightness":
            self.set_brightness(path, params, inv)
            return
        args = params.unpack()
        if method == "Log":
            inv.return_value(GLib.Variant("(as)", (self.log,)))
            return
        if method == "Mode":
            self.mode = args[0]
        elif method == "Seats":
            self.seats = args[0]
        inv.return_value(None)


def main():
    if len(sys.argv) < 2:
        print("usage: fake_logind.py TEST [ARGS...]", file=sys.stderr)
        return 2
    if shutil.which("dbus-daemon") is None:
        print("FAIL dbus-daemon not found")
        return 1
    tmp = tempfile.mkdtemp(prefix="blockie-logind.", dir=os.environ.get("TMPDIR"))
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
        fake.name(TEST)
        fake.name(NAME)
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
