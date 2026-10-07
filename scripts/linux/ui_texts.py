#!/usr/bin/env python3
# The Linux start test's eyes (scripts/linux/smoke_mdmm.sh): every name and text on the accessibility bus, one per
# line, as a screen reader would read them. The editor's page lives in WebKit's web process, which serves its own
# part of the tree (plugged into the web view of JUCE's GTK child, itself embedded in the plug-in window over
# XEmbed), so this does not walk from the desktop: it asks every connection on the accessibility bus for the
# objects it exports and reads each one's name and text (org.a11y.atspi.Accessible, org.a11y.atspi.Text).
#
#   python3 scripts/linux/ui_texts.py        (needs at-spi2-core, python3-gi and a session bus)
import sys
import xml.etree.ElementTree as ET

from gi.repository import Gio, GLib

ACCESSIBLE = "org.a11y.atspi.Accessible"
TEXT = "org.a11y.atspi.Text"
budget = 30000


def call(bus, name, path, interface, method, args=None, reply=None):
	return bus.call_sync(name, path, interface, method, args, GLib.VariantType(reply) if reply else None,
		Gio.DBusCallFlags.NONE, 2000, None)


def objects(bus, name, path, out):
	"""Every object path under path that name exports, with its interfaces (from Introspect)."""
	global budget
	if budget <= 0:
		return
	budget -= 1
	try:
		xml = call(bus, name, path, "org.freedesktop.DBus.Introspectable", "Introspect", None, "(s)").unpack()[0]
	except GLib.Error:
		return
	root = ET.fromstring(xml)
	interfaces = {i.get("name") for i in root.findall("interface")}
	if interfaces:
		out.append((path, interfaces))
	for node in root.findall("node"):
		child = node.get("name")
		if child:
			objects(bus, name, path.rstrip("/") + "/" + child, out)


def texts(bus, name, path, interfaces):
	lines = []
	if ACCESSIBLE in interfaces:
		try:
			value = call(bus, name, path, "org.freedesktop.DBus.Properties", "Get",
				GLib.Variant("(ss)", (ACCESSIBLE, "Name")), "(v)").unpack()[0]
			if value:
				lines.append(f"[name] {value}")
		except GLib.Error:
			pass
	if TEXT in interfaces:
		try:
			value = call(bus, name, path, TEXT, "GetText", GLib.Variant("(ii)", (0, -1)), "(s)").unpack()[0]
			if value and value.strip():
				lines.append(f"[text] {value.strip()}")
		except GLib.Error:
			pass
	return lines


def children(bus, name, path, seen, out):
	"""The tree from path down through Accessible.GetChildren, for exporters whose objects Introspect does not list
	(at-spi2-atk answers under /org/a11y/atspi/accessible with a fallback handler)."""
	global budget
	if budget <= 0 or (name, path) in seen:
		return
	budget -= 1
	seen.add((name, path))
	out.extend(texts(bus, name, path, {ACCESSIBLE, TEXT}))
	try:
		kids = call(bus, name, path, ACCESSIBLE, "GetChildren", None, "(a(so))").unpack()[0]
	except GLib.Error:
		return
	for kid_name, kid_path in kids:
		children(bus, kid_name or name, kid_path, seen, out)


def main():
	session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
	address = call(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", None, "(s)").unpack()[0]
	bus = Gio.DBusConnection.new_for_address_sync(address,
		Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
	names = call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
		None, "(as)").unpack()[0]
	for name in sorted(n for n in names if n.startswith(":")):
		try:
			pid = call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
				"GetConnectionUnixProcessID", GLib.Variant("(s)", (name,)), "(u)").unpack()[0]
		except GLib.Error:
			pid = "?"
		try:
			with open(f"/proc/{pid}/comm") as f:
				program = f.read().strip()
		except (OSError, TypeError):
			program = "?"
		found = []
		objects(bus, name, "/", found)
		print(f"[connection] {name} pid={pid} {program} objects={len(found)}: "
			+ " ".join(path for path, _ in found[:6]))
		# WebKit makes the page's objects when a reader asks for them: walk down from every object with children.
		lines = []
		seen = set()
		roots = [path for path, interfaces in found if ACCESSIBLE in interfaces] or ["/org/a11y/atspi/accessible/root"]
		for path in roots:
			children(bus, name, path, seen, lines)
		for line in lines:
			print(line.replace("\n", " "))
	return 0


if __name__ == "__main__":
	sys.exit(main())
