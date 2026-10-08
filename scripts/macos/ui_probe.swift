// The macOS start test's eyes (scripts/macos/smoke_mdmm.sh): what a person would see in an app's windows, read
// through the Accessibility API, so the shipped build needs no log or diagnostics switch.
//
//   swiftc -O -o ui_probe scripts/macos/ui_probe.swift
//   ui_probe trusted          exit 0 when this process may read other apps' UI (Accessibility permission)
//   ui_probe texts <pid>      every title, value and description under the app's windows (the WKWebView page
//                            included: WebKit serves its accessibility tree to the app), one per line
//   ui_probe window <pid>     the id of the app's largest on-screen window (for screencapture -l), or nothing
//   ui_probe keys <pid> <key>...  brings the app to the front, clicks the page's key probe, presses the keys
//                            ("cmd+c", "shift+/") through the system's event stream, prints the probe's text
import AppKit
import ApplicationServices
import Foundation

func attribute(_ element: AXUIElement, _ name: String) -> AnyObject? {
	var value: AnyObject?
	return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}

func texts(_ element: AXUIElement, depth: Int, into lines: inout [String], budget: inout Int) {
	if depth > 60 || budget <= 0 { return }
	budget -= 1
	let role = attribute(element, kAXRoleAttribute) as? String ?? "?"
	for name in [kAXTitleAttribute, kAXValueAttribute, kAXDescriptionAttribute] {
		if let text = attribute(element, name) as? String, !text.isEmpty {
			lines.append("[\(role)] \(text.replacingOccurrences(of: "\n", with: " "))")
		}
	}
	guard let children = attribute(element, kAXChildrenAttribute) as? [AXUIElement] else { return }
	for child in children { texts(child, depth: depth + 1, into: &lines, budget: &budget) }
}

let args = CommandLine.arguments
guard args.count >= 2 else {
	FileHandle.standardError.write("usage: ui_probe trusted | texts <pid> | window <pid> | keys <pid> <key>...\n".data(using: .utf8)!)
	exit(2)
}
switch args[1] {
case "trusted":
	exit(AXIsProcessTrusted() ? 0 : 1)
case "texts":
	guard args.count >= 3, let pid = pid_t(args[2]) else { exit(2) }
	let app = AXUIElementCreateApplication(pid)
	// Chromium-style switch; WebKit builds its tree for any client that asks, this only makes sure.
	AXUIElementSetAttributeValue(app, "AXManualAccessibility" as CFString, kCFBooleanTrue)
	var lines: [String] = []
	var budget = 20000
	if let windows = attribute(app, kAXWindowsAttribute) as? [AXUIElement] {
		for window in windows { texts(window, depth: 0, into: &lines, budget: &budget) }
	}
	print(lines.joined(separator: "\n"))
case "keys":
	// ui_probe keys <pid> <key>...: real key presses into the app, as a person makes them: the app is brought to the
	// front, the page's key probe (skins/shared/deskKeys.js, GEARMULATOR_MDMM_KEYPROBE=1) is clicked with the mouse
	// (the web view then has the keyboard), and each key ("cmd+c", "shift+/") is posted to the system's event stream
	// (the HID tap: the window server routes it to the front app, AppKit, the host and JUCE take their turns). Prints
	// the probe's text afterwards. Refuses to type while another app is in front (the keys would go there). For
	// machines with no person at them (the CI runners): it takes the focus.
	guard args.count >= 4, let pid = pid_t(args[2]) else { exit(2) }
	let app = AXUIElementCreateApplication(pid)
	AXUIElementSetAttributeValue(app, "AXManualAccessibility" as CFString, kCFBooleanTrue)
	func findProbe(_ element: AXUIElement, _ depth: Int) -> AXUIElement? {
		if depth > 60 { return nil }
		for name in [kAXValueAttribute, kAXTitleAttribute, kAXDescriptionAttribute] {
			if let text = attribute(element, name) as? String, text.hasPrefix("Keys seen:") { return element }
		}
		for child in attribute(element, kAXChildrenAttribute) as? [AXUIElement] ?? [] {
			if let found = findProbe(child, depth + 1) { return found }
		}
		return nil
	}
	func probeText() -> String {
		for window in attribute(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] {
			if let p = findProbe(window, 0) {
				for name in [kAXValueAttribute, kAXTitleAttribute, kAXDescriptionAttribute] {
					if let text = attribute(p, name) as? String, text.hasPrefix("Keys seen:") { return text }
				}
			}
		}
		return ""
	}
	// In front: the app's own accessibility switch (it works where activating another app is refused, macOS 14+).
	let front = { (attribute(app, kAXFrontmostAttribute) as? Bool) == true }
	let bringToFront = {
		AXUIElementSetAttributeValue(app, kAXFrontmostAttribute as CFString, kCFBooleanTrue)
		for window in attribute(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] { AXUIElementPerformAction(window, kAXRaiseAction as CFString) }
	}
	var probe: AXUIElement? = nil
	for _ in 0..<40 {
		bringToFront()
		for window in attribute(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] { if probe == nil { probe = findProbe(window, 0) } }
		if probe != nil && front() { break }
		Thread.sleep(forTimeInterval: 0.25)
	}
	guard let probe else { print("no key probe on the page (GEARMULATOR_MDMM_KEYPROBE=1 not set, or the page is not up)"); exit(1) }
	var position = CGPoint.zero, size = CGSize.zero
	if let p = attribute(probe, kAXPositionAttribute) { AXValueGetValue(p as! AXValue, .cgPoint, &position) }
	if let s = attribute(probe, kAXSizeAttribute) { AXValueGetValue(s as! AXValue, .cgSize, &size) }
	let at = CGPoint(x: position.x + max(4, size.width / 2), y: position.y + max(4, size.height / 2))
	// The click goes to whatever window is on top there: only when that is the app's.
	let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
	let topOwner = windows.first { w in
		guard (w[kCGWindowLayer as String] as? Int) == 0, let b = w[kCGWindowBounds as String] as? [String: Double] else { return false }
		return CGRect(x: b["X"] ?? 0, y: b["Y"] ?? 0, width: b["Width"] ?? 0, height: b["Height"] ?? 0).contains(at)
	}?[kCGWindowOwnerPID as String] as? Int
	guard topOwner == Int(pid) else { print("another window covers the page's key probe (owner pid \(topOwner.map(String.init) ?? "none")): no click, no keys"); exit(1) }
	let source = CGEventSource(stateID: .hidSystemState)
	for type in [CGEventType.leftMouseDown, .leftMouseUp] {
		CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: at, mouseButton: .left)?.post(tap: .cghidEventTap)
		Thread.sleep(forTimeInterval: 0.08)
	}
	Thread.sleep(forTimeInterval: 0.5)
	guard front() else { print("the app is not in front after the click: no keys sent"); exit(1) }
	// The ANSI keyboard's virtual key codes (HIToolbox Events.h) for the keys the start test presses.
	let codes: [String: CGKeyCode] = ["a": 0, "c": 8, "d": 2, "v": 9, "x": 7, "z": 6, "y": 16, "/": 44, "escape": 53, "space": 49]
	for spec in args[3...] {
		guard front() else {
			print("another app came to the front: stopped before \(spec)"); exit(1)
		}
		var parts = spec.lowercased().split(separator: "+").map(String.init)
		guard let name = parts.popLast(), let code = codes[name] else { print("not a key this knows: \(spec)"); exit(2) }
		var flags = CGEventFlags()
		for m in parts { flags.insert(m == "cmd" ? .maskCommand : m == "ctrl" ? .maskControl : m == "alt" ? .maskAlternate : .maskShift) }
		for down in [true, false] {
			let e = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: down)
			e?.flags = flags
			e?.post(tap: .cghidEventTap)
			Thread.sleep(forTimeInterval: 0.05)
		}
		Thread.sleep(forTimeInterval: 0.4)
	}
	Thread.sleep(forTimeInterval: 0.5)
	print(probeText())
case "window":
	guard args.count >= 3, let pid = Int(args[2]) else { exit(2) }
	let info = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
	var best: (id: Int, area: Double)? = nil
	for window in info where (window[kCGWindowOwnerPID as String] as? Int) == pid {
		guard let id = window[kCGWindowNumber as String] as? Int,
			let bounds = window[kCGWindowBounds as String] as? [String: Double] else { continue }
		let area = (bounds["Width"] ?? 0) * (bounds["Height"] ?? 0)
		if best == nil || area > best!.area { best = (id, area) }
	}
	if let best = best { print(best.id) }
default:
	exit(2)
}
