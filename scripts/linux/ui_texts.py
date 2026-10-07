#!/usr/bin/env python3
# The Linux start test's eyes (scripts/linux/smoke_mdmm.sh): every name and text in the accessibility tree of the
# desktop's applications, read through AT-SPI, one per line. The editor's page is in WebKit's web process, which
# serves its own part of the tree, so this walks every application, not one process.
#
#   python3 scripts/linux/ui_texts.py        (needs at-spi2-core, python3-gi, gir1.2-atspi-2.0 and a session bus)
import sys

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi  # noqa: E402

budget = 20000


def walk(node, depth, lines):
	global budget
	if node is None or depth > 80 or budget <= 0:
		return
	budget -= 1
	try:
		role = node.get_role_name()
	except Exception:
		role = "?"
	try:
		name = node.get_name()
		if name:
			lines.append(f"[{role}] {name}")
	except Exception:
		pass
	try:
		text = node.get_text_iface()
		if text is not None:
			value = Atspi.Text.get_text(text, 0, min(Atspi.Text.get_character_count(text), 400))
			if value and value.strip():
				lines.append(f"[{role} text] {value.strip()}")
	except Exception:
		pass
	try:
		count = node.get_child_count()
	except Exception:
		return
	for i in range(min(count, 2000)):
		try:
			child = node.get_child_at_index(i)
		except Exception:
			continue
		walk(child, depth + 1, lines)


def main():
	desktop = Atspi.get_desktop(0)
	lines = []
	for i in range(desktop.get_child_count()):
		app = desktop.get_child_at_index(i)
		if app is None:
			continue
		try:
			lines.append(f"[application] {app.get_name()} pid={app.get_process_id()}")
		except Exception:
			lines.append("[application] ?")
		walk(app, 0, lines)
	print("\n".join(lines).replace("\n\n", "\n"))
	return 0


if __name__ == "__main__":
	sys.exit(main())
