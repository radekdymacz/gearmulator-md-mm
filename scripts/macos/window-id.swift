// The window id (CGWindowID) of an app's largest normal window, for `screencapture -l <id>`, which captures a window
// even behind others. Usage: swift scripts/macos/window-id.swift "Machinedrum Editor"   (prints the id, or nothing)
import CoreGraphics
import Foundation

let owner = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "Machinedrum Editor"
let list = CGWindowListCopyWindowInfo([.optionAll, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
var best: (id: Int, area: Double)? = nil
for w in list {
	guard (w[kCGWindowOwnerName as String] as? String) == owner, (w[kCGWindowLayer as String] as? Int) == 0,
		let id = w[kCGWindowNumber as String] as? Int, let b = w[kCGWindowBounds as String] as? [String: Double] else { continue }
	let area = (b["Width"] ?? 0) * (b["Height"] ?? 0)
	if best == nil || area > best!.area { best = (id, area) }
}
if let best = best { print(best.id) }
