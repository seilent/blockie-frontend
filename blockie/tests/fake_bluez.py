import os
import shutil
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

BLUEZ = "org.bluez"
ADAPTER = "/org/bluez/hci0"
ADAPTER_IFACE = "org.bluez.Adapter1"
DEVICE_IFACE = "org.bluez.Device1"
AGENT_IFACE = "org.bluez.Agent1"
MANAGER_IFACE = "org.bluez.AgentManager1"
PROPS = "org.freedesktop.DBus.Properties"
OM = "org.freedesktop.DBus.ObjectManager"
TEST_IFACE = "net.seilent.Test.FakeBluez"
SINK = "0000110b-0000-1000-8000-00805f9b34fb"
HID = "00001124-0000-1000-8000-00805f9b34fb"

XML = """
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>
  </interface>
  <interface name="org.bluez.AgentManager1">
    <method name="RegisterAgent"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
    <method name="UnregisterAgent"><arg type="o" direction="in"/></method>
    <method name="RequestDefaultAgent"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.Adapter1">
    <method name="StartDiscovery"/>
    <method name="StopDiscovery"/>
    <method name="RemoveDevice"><arg type="o" direction="in"/></method>
    <property name="Address" type="s" access="read"/>
    <property name="Powered" type="b" access="readwrite"/>
    <property name="Discovering" type="b" access="read"/>
    <property name="Pairable" type="b" access="readwrite"/>
    <property name="PairableTimeout" type="u" access="readwrite"/>
  </interface>
  <interface name="org.bluez.Device1">
    <method name="Pair"/>
    <method name="CancelPairing"/>
    <method name="Connect"/>
    <method name="Disconnect"/>
    <property name="Address" type="s" access="read"/>
    <property name="Alias" type="s" access="readwrite"/>
    <property name="Adapter" type="o" access="read"/>
    <property name="Class" type="u" access="read"/>
    <property name="Paired" type="b" access="read"/>
    <property name="Trusted" type="b" access="readwrite"/>
    <property name="Connected" type="b" access="read"/>
    <property name="UUIDs" type="as" access="read"/>
  </interface>
  <interface name="net.seilent.Test.FakeBluez">
    <method name="Log"><arg type="as" direction="out"/></method>
    <method name="Mode"><arg type="s" direction="in"/></method>
    <method name="Restart"/>
    <method name="SetPowered"><arg type="b" direction="in"/></method>
    <method name="Ask">
      <arg type="s" direction="in"/><arg type="o" direction="in"/><arg type="s" direction="in"/>
    </method>
  </interface>
</node>
"""

ASK_ARGS = {
    "AuthorizeService": lambda dev, arg: GLib.Variant("(os)", (dev, arg)),
    "RequestConfirmation": lambda dev, arg: GLib.Variant("(ou)", (dev, 111111)),
    "RequestAuthorization": lambda dev, arg: GLib.Variant("(o)", (dev,)),
    "RequestPasskey": lambda dev, arg: GLib.Variant("(o)", (dev,)),
    "RequestPinCode": lambda dev, arg: GLib.Variant("(o)", (dev,)),
}


def device_path(mac):
    return ADAPTER + "/dev_" + mac.replace(":", "_")


def device_props(mac, alias, paired, cls, uuids):
    return {
        "Address": GLib.Variant("s", mac),
        "Alias": GLib.Variant("s", alias),
        "Adapter": GLib.Variant("o", ADAPTER),
        "Class": GLib.Variant("u", cls),
        "Paired": GLib.Variant("b", paired),
        "Trusted": GLib.Variant("b", paired),
        "Connected": GLib.Variant("b", False),
        "UUIDs": GLib.Variant("as", uuids),
    }


def remote_error(e):
    return Gio.DBusError.get_remote_error(e) or e.message


class Fake:
    def __init__(self, conn):
        self.conn = conn
        self.info = Gio.DBusNodeInfo.new_for_xml(XML)
        self.log = []
        self.mode = "justworks"
        self.agent = None
        self.held = None
        self.objects = {}
        self.regs = {}
        self.add("/", OM, {}, False)
        self.add("/org/bluez", MANAGER_IFACE, {}, False)
        self.add("/test", TEST_IFACE, {}, False)
        self.add(ADAPTER, ADAPTER_IFACE, {
            "Address": GLib.Variant("s", "02:00:00:00:00:10"),
            "Powered": GLib.Variant("b", True),
            "Discovering": GLib.Variant("b", False),
            "Pairable": GLib.Variant("b", False),
            "PairableTimeout": GLib.Variant("u", 0),
        }, False)
        self.add(device_path("02:00:00:00:00:01"), DEVICE_IFACE,
                 device_props("02:00:00:00:00:01", "Synth Headset", True, 0x240404, [SINK]), False)
        self.add(device_path("02:00:00:00:00:02"), DEVICE_IFACE,
                 device_props("02:00:00:00:00:02", "Synth Pad", False, 0x002508, [HID]), False)

    def own(self, want):
        method = "RequestName" if want else "ReleaseName"
        args = GLib.Variant("(su)", (BLUEZ, 4)) if want else GLib.Variant("(s)", (BLUEZ,))
        self.conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method, args,
                            None, Gio.DBusCallFlags.NONE, -1, None)

    def add(self, path, iface, props, announce):
        self.objects.setdefault(path, {})[iface] = dict(props)
        rid = self.conn.register_object(path, self.info.lookup_interface(iface), self.on_call, None, None)
        self.regs.setdefault(path, []).append(rid)
        if announce:
            self.emit("/", OM, "InterfacesAdded", GLib.Variant("(oa{sa{sv}})", (path, {iface: props})))

    def remove(self, path):
        for rid in self.regs.pop(path, []):
            self.conn.unregister_object(rid)
        ifaces = list(self.objects.pop(path, {}).keys())
        self.emit("/", OM, "InterfacesRemoved", GLib.Variant("(oas)", (path, ifaces)))

    def emit(self, path, iface, name, value):
        self.conn.emit_signal(None, path, iface, name, value)

    def set(self, path, iface, name, value):
        self.objects[path][iface][name] = value
        self.emit(path, PROPS, "PropertiesChanged", GLib.Variant("(sa{sv}as)", (iface, {name: value}, [])))

    def get(self, path, iface, name):
        return self.objects[path][iface][name].unpack()

    def managed(self):
        out = {}
        for path, ifaces in self.objects.items():
            if path in ("/", "/test"):
                continue
            out[path] = {i: p for i, p in ifaces.items()}
        return GLib.Variant("(a{oa{sa{sv}}})", (out,))

    def agent_call(self, method, params, done):
        if self.agent is None:
            done(None, "no agent")
            return
        owner, path = self.agent

        def finished(conn, res, _data):
            try:
                done(conn.call_finish(res), None)
            except GLib.Error as e:
                done(None, remote_error(e))

        self.conn.call(owner, path, AGENT_IFACE, method, params, None, Gio.DBusCallFlags.NONE, 5000, None,
                       finished, None)

    def finish_pair(self, path, inv, err):
        if err is None:
            self.set(path, DEVICE_IFACE, "Paired", GLib.Variant("b", True))
            self.log.append(f"Pair {path} ok")
            inv.return_value(None)
        else:
            self.log.append(f"Pair {path} {err}")
            inv.return_dbus_error(err, "pairing failed")
        return False

    def pair(self, path, inv):
        if self.get(path, DEVICE_IFACE, "Paired"):
            inv.return_dbus_error("org.bluez.Error.AlreadyExists", "already paired")
            return
        mode = self.mode
        if mode == "justworks":
            GLib.timeout_add(20, self.finish_pair, path, inv, None)
        elif mode == "fail":
            self.finish_pair(path, inv, "org.bluez.Error.AuthenticationFailed")
        elif mode == "hold":
            self.held = (path, inv)
            self.log.append(f"Pair hold {path}")
        elif mode in ("confirm", "cancel"):
            state = {"cancelled": False}

            def confirmed(reply, err):
                self.log.append("RequestConfirmation " + ("ok" if err is None else err))
                if state["cancelled"]:
                    return
                self.finish_pair(path, inv, None if err is None else "org.bluez.Error.AuthenticationRejected")

            self.agent_call("RequestConfirmation", GLib.Variant("(ou)", (path, 123456)), confirmed)
            if mode == "cancel":
                def cancel():
                    state["cancelled"] = True
                    self.agent_call("Cancel", None,
                                    lambda r, e: self.log.append("Cancel " + ("ok" if e is None else e)))
                    self.finish_pair(path, inv, "org.bluez.Error.AuthenticationCanceled")
                    return False

                GLib.timeout_add(100, cancel)
        elif mode == "pin":
            def pinned(reply, err):
                self.log.append("RequestPinCode " + (reply.unpack()[0] if err is None else err))
                self.finish_pair(path, inv, None if err is None else "org.bluez.Error.AuthenticationRejected")

            self.agent_call("RequestPinCode", GLib.Variant("(o)", (path,)), pinned)
        elif mode == "display":
            self.agent_call("DisplayPasskey", GLib.Variant("(ouq)", (path, 654321, 0)),
                            lambda r, e: self.log.append("DisplayPasskey " + ("ok" if e is None else e)))
            GLib.timeout_add(300, self.finish_pair, path, inv, "org.bluez.Error.AuthenticationFailed")
        else:
            inv.return_dbus_error("org.bluez.Error.Failed", "unknown mode " + mode)

    def found(self):
        path = device_path("02:00:00:00:00:03")
        if path not in self.objects:
            self.add(path, DEVICE_IFACE, device_props("02:00:00:00:00:03", "Synth Speaker", False, 0x240414, [SINK]),
                     True)
        return False

    def restart(self):
        self.agent = None
        self.log.append("Restart")
        self.own(False)
        GLib.timeout_add(100, lambda: self.own(True) or False)

    def ask(self, method, dev, arg):
        make = ASK_ARGS.get(method)
        params = make(dev, arg) if make else None
        self.agent_call(method, params, lambda r, e: self.log.append(f"Ask {method} " + ("ok" if e is None else e)))

    def on_props(self, path, method, params, inv):
        iface = params.get_child_value(0).get_string()
        props = self.objects.get(path, {}).get(iface, {})
        if method == "GetAll":
            inv.return_value(GLib.Variant("(a{sv})", (props,)))
            return
        name = params.get_child_value(1).get_string()
        if method == "Get":
            inv.return_value(GLib.Variant("(v)", (props[name],)))
            return
        value = params.get_child_value(2).get_variant()
        self.log.append(f"Set {iface.split('.')[-1]} {name} {value.unpack()}")
        self.set(path, iface, name, value)
        if iface == ADAPTER_IFACE and name == "Powered" and not value.unpack():
            self.set(path, iface, "Discovering", GLib.Variant("b", False))
        inv.return_value(None)

    def on_call(self, conn, sender, path, iface, method, params, inv):
        try:
            self.dispatch(sender, path, iface, method, params, inv)
        except Exception as e:
            inv.return_dbus_error("org.bluez.Error.Failed", f"{type(e).__name__}: {e}")

    def dispatch(self, sender, path, iface, method, params, inv):
        if iface == PROPS:
            self.on_props(path, method, params, inv)
            return
        args = params.unpack()
        if iface == OM:
            inv.return_value(self.managed())
            return
        if iface == MANAGER_IFACE:
            if method == "RegisterAgent":
                self.agent = (sender, args[0])
                self.log.append(f"RegisterAgent {args[0]} {args[1]}")
            elif method == "RequestDefaultAgent":
                self.log.append(f"RequestDefaultAgent {args[0]}")
            else:
                self.agent = None
            inv.return_value(None)
            return
        if iface == ADAPTER_IFACE:
            if method == "RemoveDevice":
                self.log.append(f"RemoveDevice {args[0]}")
                if args[0] not in self.objects:
                    inv.return_dbus_error("org.bluez.Error.DoesNotExist", "no device")
                    return
                self.remove(args[0])
            else:
                self.log.append(method)
                self.set(path, iface, "Discovering", GLib.Variant("b", method == "StartDiscovery"))
                if method == "StartDiscovery":
                    GLib.timeout_add(50, self.found)
            inv.return_value(None)
            return
        if iface == DEVICE_IFACE:
            self.log.append(f"{method} {path}")
            if method == "Pair":
                self.pair(path, inv)
                return
            if method == "CancelPairing":
                if self.held and self.held[0] == path:
                    held, self.held = self.held, None
                    self.finish_pair(path, held[1], "org.bluez.Error.AuthenticationCanceled")
            elif method in ("Connect", "Disconnect"):
                self.set(path, iface, "Connected", GLib.Variant("b", method == "Connect"))
            inv.return_value(None)
            return
        if iface == TEST_IFACE:
            if method == "Log":
                inv.return_value(GLib.Variant("(as)", (self.log,)))
                return
            if method == "Mode":
                self.mode = args[0]
            elif method == "Restart":
                self.restart()
            elif method == "SetPowered":
                self.set(ADAPTER, ADAPTER_IFACE, "Powered", GLib.Variant("b", args[0]))
                if not args[0]:
                    self.set(ADAPTER, ADAPTER_IFACE, "Discovering", GLib.Variant("b", False))
            elif method == "Ask":
                self.ask(*args)
            inv.return_value(None)
            return
        inv.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)


def main():
    if len(sys.argv) < 2:
        print("usage: fake_bluez.py TEST [ARGS...]", file=sys.stderr)
        return 2
    if shutil.which("dbus-daemon") is None:
        print("FAIL dbus-daemon not found")
        return 1
    tmp = tempfile.mkdtemp(prefix="blockie-bt.", dir=os.environ.get("TMPDIR"))
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
