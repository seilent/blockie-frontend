import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

NM = "org.freedesktop.NetworkManager"
NM_PATH = "/org/freedesktop/NetworkManager"
SETTINGS = NM_PATH + "/Settings"
DEVICE = NM_PATH + "/Devices/3"
PROPS = "org.freedesktop.DBus.Properties"
DEV_IFACE = NM + ".Device"
WIFI_IFACE = NM + ".Device.Wireless"
AP_IFACE = NM + ".AccessPoint"
SET_IFACE = NM + ".Settings"
CONN_IFACE = NM + ".Settings.Connection"
TEST_IFACE = "net.seilent.Test.FakeNm"
SEC = "802-11-wireless-security"
VOLATILE = 0x4
PSK_CCMP = 0x188

XML = f"""
<node>
  <interface name="{NM}">
    <method name="GetDeviceByIpIface"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
    <method name="GetPermissions"><arg type="a{{ss}}" direction="out"/></method>
    <signal name="CheckPermissions"/>
    <method name="AddAndActivateConnection2">
      <arg type="a{{sa{{sv}}}}" direction="in"/><arg type="o" direction="in"/><arg type="o" direction="in"/>
      <arg type="a{{sv}}" direction="in"/><arg type="o" direction="out"/><arg type="o" direction="out"/>
      <arg type="a{{sv}}" direction="out"/>
    </method>
    <method name="ActivateConnection">
      <arg type="o" direction="in"/><arg type="o" direction="in"/><arg type="o" direction="in"/>
      <arg type="o" direction="out"/>
    </method>
    <property name="WirelessEnabled" type="b" access="readwrite"/>
    <property name="Devices" type="ao" access="read"/>
  </interface>
  <interface name="{DEV_IFACE}">
    <method name="Disconnect"/>
    <signal name="StateChanged"><arg type="u"/><arg type="u"/><arg type="u"/></signal>
    <property name="State" type="u" access="read"/>
    <property name="ActiveConnection" type="o" access="read"/>
    <property name="Interface" type="s" access="read"/>
  </interface>
  <interface name="{WIFI_IFACE}">
    <method name="RequestScan"><arg type="a{{sv}}" direction="in"/></method>
    <method name="GetAllAccessPoints"><arg type="ao" direction="out"/></method>
    <signal name="AccessPointAdded"><arg type="o"/></signal>
    <signal name="AccessPointRemoved"><arg type="o"/></signal>
    <property name="ActiveAccessPoint" type="o" access="read"/>
    <property name="LastScan" type="x" access="read"/>
  </interface>
  <interface name="{AP_IFACE}">
    <property name="Ssid" type="ay" access="read"/>
    <property name="Strength" type="y" access="read"/>
    <property name="Flags" type="u" access="read"/>
    <property name="WpaFlags" type="u" access="read"/>
    <property name="RsnFlags" type="u" access="read"/>
    <property name="Frequency" type="u" access="read"/>
    <property name="HwAddress" type="s" access="read"/>
  </interface>
  <interface name="{SET_IFACE}">
    <method name="ListConnections"><arg type="ao" direction="out"/></method>
    <method name="AddConnection"><arg type="a{{sa{{sv}}}}" direction="in"/><arg type="o" direction="out"/></method>
  </interface>
  <interface name="{CONN_IFACE}">
    <method name="GetSettings"><arg type="a{{sa{{sv}}}}" direction="out"/></method>
    <method name="Update"><arg type="a{{sa{{sv}}}}" direction="in"/></method>
    <method name="Update2">
      <arg type="a{{sa{{sv}}}}" direction="in"/><arg type="u" direction="in"/><arg type="a{{sv}}" direction="in"/>
      <arg type="a{{sv}}" direction="out"/>
    </method>
    <method name="Delete"/>
    <property name="Flags" type="u" access="read"/>
    <property name="Unsaved" type="b" access="read"/>
  </interface>
  <interface name="{TEST_IFACE}">
    <method name="Log"><arg type="as" direction="out"/></method>
    <method name="Mode"><arg type="s" direction="in"/></method>
    <method name="Restart"/>
    <method name="Disk"><arg type="as" direction="out"/></method>
    <method name="Conns"><arg type="as" direction="out"/></method>
    <method name="Key"><arg type="s" direction="in"/><arg type="s" direction="in"/></method>
    <method name="Hold"><arg type="b" direction="in"/></method>
    <method name="FailOnce"><arg type="s" direction="in"/></method>
    <method name="Permission"><arg type="s" direction="in"/></method>
  </interface>
</node>
"""

APS = [
    ("1", b"blockie-test-home", 80, 1, 0, PSK_CCMP, 2437, "02:00:00:00:00:01"),
    ("2", b"blockie-test-cafe", 60, 0, 0, 0, 2412, "02:00:00:00:00:02"),
    ("3", b"blockie-test-wep", 50, 1, 0, 0, 2462, "02:00:00:00:00:03"),
    ("4", b"blockie-test-home", 40, 1, 0, PSK_CCMP, 5180, "02:00:00:00:00:04"),
    ("5", b"", 70, 1, 0, PSK_CCMP, 5200, "02:00:00:00:00:05"),
    ("6", b"blockie-test-office", 30, 1, 0, PSK_CCMP, 5745, "02:00:00:00:00:06"),
]
LATE = ("7", b"blockie-test-late", 20, 0, 0, 0, 2422, "02:00:00:00:00:07")


def variants(v):
    out = {}
    for i in range(v.n_children()):
        entry = v.get_child_value(i)
        name = entry.get_child_value(0).get_string()
        props = entry.get_child_value(1)
        inner = {}
        for j in range(props.n_children()):
            kv = props.get_child_value(j)
            inner[kv.get_child_value(0).get_string()] = kv.get_child_value(1).get_variant()
        out[name] = inner
    return out


def ssid_of(settings):
    v = settings.get("802-11-wireless", {}).get("ssid")
    return bytes(v.unpack()) if v is not None else b""


def psk_of(settings):
    v = settings.get(SEC, {}).get("psk")
    return v.unpack() if v is not None else ""


def complete(settings, ap_ssid):
    wifi = dict(settings.get("802-11-wireless", {}))
    if "ssid" not in wifi:
        wifi["ssid"] = GLib.Variant("ay", ap_ssid)
    settings["802-11-wireless"] = wifi
    con = dict(settings.get("connection", {}))
    con["type"] = GLib.Variant("s", "802-11-wireless")
    con.setdefault("uuid", GLib.Variant("s", str(uuid.uuid4())))
    con.setdefault("id", GLib.Variant("s", ssid_of(settings).decode("utf-8", "replace")))
    con.setdefault("interface-name", GLib.Variant("s", "wlan0"))
    settings["connection"] = con
    return settings


def mgmt_of(settings):
    if SEC not in settings:
        return "open"
    v = settings[SEC].get("key-mgmt")
    return v.unpack() if v is not None else ""


class Fake:
    def __init__(self, conn):
        self.conn = conn
        self.info = Gio.DBusNodeInfo.new_for_xml(XML)
        self.log = []
        self.mode = "ok"
        self.permission = "yes"
        self.radio = True
        self.state = 30
        self.active = "/"
        self.active_conn = "/"
        self.active_ap = "/"
        self.last_scan = -1
        self.aps = {}
        self.keys = {b"blockie-test-home": "blockie-test-psk-01", b"blockie-test-office": "blockie-test-psk-02"}
        self.conns = {}
        self.regs = {}
        self.n_conn = 0
        self.n_active = 0
        self.hold = False
        self.held = None
        self.fail_once = set()
        self.steps = []
        self.ticking = False
        self.register(NM_PATH, [NM])
        self.register(DEVICE, [DEV_IFACE, WIFI_IFACE])
        self.register(SETTINGS, [SET_IFACE])
        self.register("/test", [TEST_IFACE])
        for ap in APS:
            self.add_ap(ap, False)
        office = {
            "connection": {"id": GLib.Variant("s", "blockie-test-office"),
                           "type": GLib.Variant("s", "802-11-wireless")},
            "802-11-wireless": {"ssid": GLib.Variant("ay", b"blockie-test-office")},
            SEC: {"key-mgmt": GLib.Variant("s", "wpa-psk"), "psk": GLib.Variant("s", "blockie-test-psk-02")},
        }
        self.add_conn(office, 0, True)
        wired = {"connection": {"id": GLib.Variant("s", "blockie-test-wired"),
                                "type": GLib.Variant("s", "802-3-ethernet")}}
        self.add_conn(wired, 0, True)

    def register(self, path, ifaces):
        for iface in ifaces:
            rid = self.conn.register_object(path, self.info.lookup_interface(iface), self.on_call, None, None)
            self.regs.setdefault(path, []).append(rid)

    def unregister(self, path):
        for rid in self.regs.pop(path, []):
            self.conn.unregister_object(rid)

    def own(self, want):
        method = "RequestName" if want else "ReleaseName"
        args = GLib.Variant("(su)", (NM, 4)) if want else GLib.Variant("(s)", (NM,))
        self.conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method, args,
                            None, Gio.DBusCallFlags.NONE, -1, None)

    def emit(self, path, iface, name, value):
        self.conn.emit_signal(None, path, iface, name, value)

    def changed(self, path, iface, props):
        self.emit(path, PROPS, "PropertiesChanged", GLib.Variant("(sa{sv}as)", (iface, props, [])))

    def add_ap(self, ap, announce):
        n, ssid, strength, flags, wpa, rsn, freq, hw = ap
        path = NM_PATH + "/AccessPoint/" + n
        self.aps[path] = {"ssid": ssid, "strength": strength, "flags": flags, "wpa": wpa, "rsn": rsn, "freq": freq,
                          "hw": hw}
        self.register(path, [AP_IFACE])
        if announce:
            self.emit(DEVICE, WIFI_IFACE, "AccessPointAdded", GLib.Variant("(o)", (path,)))

    def add_conn(self, settings, flags, disk):
        self.n_conn += 1
        path = SETTINGS + "/" + str(self.n_conn)
        self.conns[path] = {"settings": settings, "flags": flags, "disk": disk}
        self.register(path, [CONN_IFACE])
        return path

    def drop_conn(self, path, why):
        if path in self.conns:
            self.log.append(f"{why} {path}")
            del self.conns[path]
            self.unregister(path)

    def set_state(self, new, reason):
        old = self.state
        self.state = new
        self.emit(DEVICE, DEV_IFACE, "StateChanged", GLib.Variant("(uuu)", (new, old, reason)))

    def set_active(self, active, conn, ap):
        self.active = active
        self.active_conn = conn
        if ap != self.active_ap:
            self.active_ap = ap
            self.changed(DEVICE, WIFI_IFACE, {"ActiveAccessPoint": GLib.Variant("o", ap)})

    def run_steps(self):
        if self.steps:
            self.steps.pop(0)()
        if self.steps:
            return True
        self.ticking = False
        return False

    def queue(self, steps):
        self.steps.extend(steps)
        if self.steps and not self.ticking:
            self.ticking = True
            GLib.timeout_add(20, self.run_steps)

    def teardown(self, reason):
        steps = []
        if self.state >= 40 and self.state <= 100:
            was = self.active_conn

            def down():
                self.set_state(110, reason)

            def gone():
                self.set_active("/", "/", "/")
                self.set_state(30, reason)
                if was in self.conns and self.conns[was]["flags"] & VOLATILE:
                    self.drop_conn(was, "Dropped")

            steps = [down, gone]
        return steps

    def activate(self, conn, active, ap):
        settings = self.conns[conn]["settings"]
        ssid = ssid_of(settings)
        want = self.keys.get(ssid, "")
        ok = mgmt_of(settings) == "open" if not want else psk_of(settings) == want
        steps = self.teardown(1)

        def prepare():
            self.set_active(active, conn, ap)
            self.set_state(40, 0)

        def config():
            self.set_state(50, 0)
            if self.hold:
                self.held = rest
                return
            for s in rest:
                s()

        def ip():
            self.set_state(70, 0)

        def up():
            self.set_state(100, 0)

        def failed():
            self.set_state(120, 7)

        def down():
            self.set_active("/", "/", "/")
            self.set_state(30, 7)
            if conn in self.conns and self.conns[conn]["flags"] & VOLATILE:
                self.drop_conn(conn, "Dropped")

        rest = [lambda: self.queue([ip, up])] if ok else [lambda: self.queue([failed, down])]
        self.queue(steps + [prepare, config])

    def best_ap(self, ssid):
        best = None
        for path, ap in self.aps.items():
            if ap["ssid"] == ssid and (best is None or ap["strength"] > self.aps[best]["strength"]):
                best = path
        return best

    def new_active(self):
        self.n_active += 1
        return NM_PATH + "/ActiveConnection/" + str(self.n_active)

    def nm_call(self, method, params, inv):
        if method == "GetPermissions":
            self.log.append(f"GetPermissions {self.permission}")
            perms = {"org.freedesktop.NetworkManager.enable-disable-wifi": self.permission,
                     "org.freedesktop.NetworkManager.network-control": "yes"}
            inv.return_value(GLib.Variant("(a{ss})", (perms,)))
            return
        if method == "GetDeviceByIpIface":
            (name,) = params.unpack()
            if name != "wlan0":
                inv.return_dbus_error(NM + ".UnknownDevice", "no device")
                return
            inv.return_value(GLib.Variant("(o)", (DEVICE,)))
            return
        if method == "AddAndActivateConnection2":
            ap = params.get_child_value(2).get_string()
            settings = complete(variants(params.get_child_value(0)), self.aps.get(ap, {}).get("ssid", b""))
            opts = params.get_child_value(3).unpack()
            persist = opts.get("persist", "disk")
            sec = settings.get(SEC, {})
            flags = sec.get("psk-flags")
            self.log.append(f"AddAndActivateConnection2 persist={persist} ap={ap} security={mgmt_of(settings)} "
                            f"psk-flags={flags.unpack() if flags is not None else '-'}")
            conn = self.add_conn(settings, VOLATILE if persist == "volatile" else 0, persist == "disk")
            active = self.new_active()
            inv.return_value(GLib.Variant("(ooa{sv})", (conn, active, {})))
            self.activate(conn, active, ap)
            return
        if method == "ActivateConnection":
            conn = params.get_child_value(0).get_string()
            self.log.append(f"ActivateConnection {conn}")
            if conn not in self.conns:
                inv.return_dbus_error(NM + ".UnknownConnection", "no connection")
                return
            ap = self.best_ap(ssid_of(self.conns[conn]["settings"]))
            if ap is None:
                inv.return_dbus_error(NM + ".UnknownConnection", "no access point")
                return
            active = self.new_active()
            inv.return_value(GLib.Variant("(o)", (active,)))
            self.activate(conn, active, ap)
            return
        inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)

    def props_of(self, path, iface):
        if path == NM_PATH and iface == NM:
            return {"WirelessEnabled": GLib.Variant("b", self.radio), "Devices": GLib.Variant("ao", [DEVICE])}
        if path == DEVICE and iface == DEV_IFACE:
            return {"State": GLib.Variant("u", self.state), "ActiveConnection": GLib.Variant("o", self.active),
                    "Interface": GLib.Variant("s", "wlan0")}
        if path == DEVICE and iface == WIFI_IFACE:
            return {"ActiveAccessPoint": GLib.Variant("o", self.active_ap),
                    "LastScan": GLib.Variant("x", self.last_scan)}
        if path in self.aps and iface == AP_IFACE:
            ap = self.aps[path]
            return {"Ssid": GLib.Variant("ay", ap["ssid"]), "Strength": GLib.Variant("y", ap["strength"]),
                    "Flags": GLib.Variant("u", ap["flags"]), "WpaFlags": GLib.Variant("u", ap["wpa"]),
                    "RsnFlags": GLib.Variant("u", ap["rsn"]), "Frequency": GLib.Variant("u", ap["freq"]),
                    "HwAddress": GLib.Variant("s", ap["hw"])}
        if path in self.conns and iface == CONN_IFACE:
            c = self.conns[path]
            return {"Flags": GLib.Variant("u", c["flags"]), "Unsaved": GLib.Variant("b", not c["disk"])}
        return {}

    def set_radio(self, on):
        if on == self.radio:
            return
        self.radio = on
        self.changed(NM_PATH, NM, {"WirelessEnabled": GLib.Variant("b", on)})
        if on:
            self.queue([lambda: self.set_state(30, 0)])
        else:
            self.queue(self.teardown(36) + [lambda: self.set_state(20, 36)])

    def on_props(self, path, method, params, inv):
        iface = params.get_child_value(0).get_string()
        props = self.props_of(path, iface)
        if method == "GetAll":
            inv.return_value(GLib.Variant("(a{sv})", (props,)))
            return
        name = params.get_child_value(1).get_string()
        if method == "Get":
            if name in self.fail_once:
                self.fail_once.discard(name)
                self.log.append(f"GetFailed {name}")
                inv.return_dbus_error(NM + ".Failed", "failed for the test")
                return
            if name not in props:
                inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownProperty", name)
                return
            inv.return_value(GLib.Variant("(v)", (props[name],)))
            return
        value = params.get_child_value(2).get_variant().unpack()
        self.log.append(f"Set {name} {value}")
        if self.mode == "deny":
            inv.return_dbus_error(NM + ".PermissionDenied", "not allowed")
            return
        if path == NM_PATH and name == "WirelessEnabled":
            self.set_radio(bool(value))
        inv.return_value(None)

    def wifi_call(self, method, params, inv):
        if method == "GetAllAccessPoints":
            inv.return_value(GLib.Variant("(ao)", (sorted(self.aps.keys()),)))
            return
        if method == "RequestScan":
            self.log.append("RequestScan")
            if self.mode == "scanfail" or not self.radio:
                inv.return_dbus_error(DEV_IFACE + ".NotAllowed", "scanning not allowed")
                return
            inv.return_value(None)

            def found():
                if NM_PATH + "/AccessPoint/" + LATE[0] not in self.aps:
                    self.add_ap(LATE, True)
                return False

            def finished():
                self.last_scan = int(time.monotonic() * 1000)
                self.changed(DEVICE, WIFI_IFACE, {"LastScan": GLib.Variant("x", self.last_scan)})
                return False

            GLib.timeout_add(60, found)
            GLib.timeout_add(120, finished)
            return
        inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)

    def settings_call(self, method, params, inv):
        if method == "ListConnections":
            inv.return_value(GLib.Variant("(ao)", (sorted(self.conns.keys(), key=lambda p: int(p.rsplit("/", 1)[1])),)))
            return
        if method == "AddConnection":
            settings = variants(params.get_child_value(0))
            named = "yes" if "connection" in settings else "no"
            self.log.append(f"AddConnection security={mgmt_of(settings)} id={named}")
            if self.mode == "refuse":
                inv.return_dbus_error(SET_IFACE + ".PermissionDenied", "not allowed")
                return
            inv.return_value(GLib.Variant("(o)", (self.add_conn(settings, 0, True),)))
            return
        inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)

    def conn_call(self, path, method, params, inv):
        c = self.conns.get(path)
        if c is None:
            inv.return_dbus_error(SET_IFACE + ".InvalidConnection", "no connection")
            return
        if method == "GetSettings":
            out = {}
            for name, props in c["settings"].items():
                out[name] = {k: v for k, v in props.items() if k != "psk"}
            inv.return_value(GLib.Variant("(a{sa{sv}})", (out,)))
            return
        if method == "Update":
            self.log.append(f"Update {path}")
            if self.mode == "refuse":
                inv.return_dbus_error(SET_IFACE + ".PermissionDenied", "not allowed")
                return
            c["settings"] = variants(params.get_child_value(0))
            inv.return_value(None)
            return
        if method == "Update2":
            settings = variants(params.get_child_value(0))
            flags = params.get_child_value(1).get_uint32()
            self.log.append(f"Update2 {path} settings={len(settings)} flags={flags}")
            if self.mode == "refuse":
                inv.return_dbus_error(SET_IFACE + ".PermissionDenied", "not allowed")
                return
            if settings:
                c["settings"] = settings
            if flags & 0x1:
                c["disk"] = True
                c["flags"] &= ~VOLATILE
            inv.return_value(GLib.Variant("(a{sv})", ({},)))
            return
        if method == "Delete":
            inv.return_value(None)
            if self.active_conn == path:
                self.queue(self.teardown(38))
            self.drop_conn(path, "Delete")
            return
        inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)

    def test_call(self, method, params, inv):
        args = params.unpack()
        if method == "Log":
            inv.return_value(GLib.Variant("(as)", (self.log,)))
            return
        if method in ("Disk", "Conns"):
            out = []
            for path, c in sorted(self.conns.items()):
                s = c["settings"]
                if method == "Disk" and not c["disk"]:
                    continue
                if s.get("connection", {}).get("type") is not None and \
                        s["connection"]["type"].unpack() != "802-11-wireless":
                    continue
                line = f"{ssid_of(s).decode()}|{mgmt_of(s)}|{psk_of(s)}"
                if method == "Conns":
                    line += f"|{c['flags']}|{'disk' if c['disk'] else 'memory'}"
                out.append(line)
            inv.return_value(GLib.Variant("(as)", (out,)))
            return
        if method == "Mode":
            self.mode = args[0]
        elif method == "Restart":
            self.log.append("Restart")
            self.own(False)
            GLib.timeout_add(100, lambda: self.own(True) or False)
        elif method == "FailOnce":
            self.fail_once.add(args[0])
        elif method == "Permission":
            self.permission = args[0]
            self.emit(NM_PATH, NM, "CheckPermissions", None)
        elif method == "Key":
            self.keys[args[0].encode()] = args[1]
        elif method == "Hold":
            self.hold = args[0]
            if not self.hold and self.held is not None:
                rest, self.held = self.held, None
                for s in rest:
                    s()
        inv.return_value(None)

    def on_call(self, conn, sender, path, iface, method, params, inv):
        try:
            if iface == PROPS:
                self.on_props(path, method, params, inv)
            elif iface == NM:
                self.nm_call(method, params, inv)
            elif iface == DEV_IFACE:
                self.log.append(f"{method} {path}")
                inv.return_value(None)
                self.queue(self.teardown(39))
            elif iface == WIFI_IFACE:
                self.wifi_call(method, params, inv)
            elif iface == SET_IFACE:
                self.settings_call(method, params, inv)
            elif iface == CONN_IFACE:
                self.conn_call(path, method, params, inv)
            elif iface == TEST_IFACE:
                self.test_call(method, params, inv)
            else:
                inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)
        except Exception as e:
            inv.return_dbus_error(NM + ".Failed", f"{type(e).__name__}: {e}")


def main():
    if len(sys.argv) < 2:
        print("usage: fake_nm.py TEST [ARGS...]", file=sys.stderr)
        return 2
    if shutil.which("dbus-daemon") is None:
        print("FAIL dbus-daemon not found")
        return 1
    tmp = tempfile.mkdtemp(prefix="blockie-nm.", dir=os.environ.get("TMPDIR"))
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
        fake.own(True)
        work = os.path.join(tmp, "work")
        os.mkdir(work)
        env = dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=addr, BLOCKIE_TEST_DIR=work)
        child = subprocess.Popen(sys.argv[1:], env=env)
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
