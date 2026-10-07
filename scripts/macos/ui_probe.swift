// The macOS start test's eyes (scripts/macos/smoke_mdmm.sh): what a person would see in an app's windows, read
// through the Accessibility API, so the shipped build needs no log or diagnostics switch.
//
//   swiftc -O -o ui_probe scripts/macos/ui_probe.swift
//   ui_probe trusted          exit 0 when this process may read other apps' UI (Accessibility permission)
//   ui_probe texts <pid>      every title, value and description under the app's windows (the WKWebView page
//                            included: WebKit serves its accessibility tree to the app), one per line
//   ui_probe window <pid>     the id of the app's largest on-screen window (for screencapture -l), or nothing
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
	FileHandle.standardError.write("usage: ui_probe trusted | texts <pid> | window <pid>\n".data(using: .utf8)!)
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
