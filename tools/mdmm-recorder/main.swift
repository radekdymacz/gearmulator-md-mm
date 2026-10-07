// mdmm-recorder: records one app's window and that app's audio only, with ScreenCaptureKit, to a movie file
// (doc/modern-ux/DEMO-VIDEOS.md). No third-party code; build with tools/mdmm-recorder/build.sh (swiftc).
//
//   mdmm-recorder record (--bundle-id ID | --pid N) --out FILE.mov [--max-seconds 120] [--fps 60] [--codec h264|hevc]
//                 [--content-size WxH] [--ready-file F] [--info-file F] [--wait-seconds 30] [--stdin]
//       Waits for the app's largest on-screen window, then records it until SIGINT/SIGTERM, "stop" (or end of
//       input) on stdin with --stdin, or --max-seconds. The picture is the window at the display's pixel scale
//       (Retina: 2x); --content-size takes only the window's content (its bottom WxH points, centred: the title
//       bar is left out). The sound is the app's own output only (other apps and this process are not heard).
//       --ready-file is written once the first frames are being written; --info-file gets the window as JSON.
//       Exit 0 when the file is complete, 1 on an error, 2 on bad arguments, 3 when screen recording is not allowed.
//
//   mdmm-recorder caption --text "TEXT" --out FILE.png [--width 1000] [--size 60] [--band] [--max-words 6]
//       A caption for ffmpeg's overlay (this ffmpeg build has no drawtext): white heavy text on a near-black box with
//       a lime rule on top (the site's look), the box the text's size or, with --band, the full --width. "\n" in
//       TEXT breaks the line; a longer line is split in two.
//
//   mdmm-recorder card --out FILE.png --width W --height H --title T --line L --url U [--safe x,y,w,h]
//       An end card: black, the name in white, the line in grey, the URL in lime, centred in the safe rectangle.
//
//   mdmm-recorder front --pid N
//       Brings the app with that process id to the front (by pid: never by bundle id).
//
// Screen Recording must be allowed (System Settings > Privacy & Security > Screen & System Audio Recording) for the
// process that runs this tool: the terminal or the agent host that starts it.

import AppKit
import AVFoundation
import CoreMedia
import Foundation
import ScreenCaptureKit

func fail(_ message: String, _ code: Int32 = 1) -> Never {
	FileHandle.standardError.write(("mdmm-recorder: " + message + "\n").data(using: .utf8)!)
	exit(code)
}

func say(_ message: String) {
	FileHandle.standardError.write(("mdmm-recorder: " + message + "\n").data(using: .utf8)!)
}

/// The arguments as name -> value ("--stdin" -> "").
func parse(_ args: ArraySlice<String>) -> [String: String] {
	var out: [String: String] = [:]
	var i = args.startIndex
	while i < args.endIndex {
		let a = args[i]
		guard a.hasPrefix("--") else { fail("unexpected argument \(a)", 2) }
		let key = String(a.dropFirst(2))
		if key == "stdin" || key == "band" { out[key] = ""; i += 1; continue }
		guard i + 1 < args.endIndex else { fail("\(a) needs a value", 2) }
		out[key] = args[i + 1]
		i += 2
	}
	return out
}

// MARK: - caption and end card (the site's look: black, white, lime #c8ff00, sharp edges, a system sans)

let lime = NSColor(srgbRed: 0xc8 / 255.0, green: 1, blue: 0, alpha: 1)
let grey = NSColor(srgbRed: 0xb0 / 255.0, green: 0xb0 / 255.0, blue: 0xb0 / 255.0, alpha: 1)

/// A caption's lines: "\n" breaks them; a longer line is split in two, balanced, so each has about maxWords at most.
func captionLines(_ text: String, maxWords: Int) -> [String] {
	let given = text.replacingOccurrences(of: "\\n", with: "\n").components(separatedBy: "\n")
	if given.count > 1 { return given }
	let words = text.split(separator: " ").map(String.init)
	if words.count <= maxWords { return [text] }
	var best = 1, bestDiff = Int.max
	for i in 1..<words.count {
		let d = abs(words[..<i].joined(separator: " ").count - words[i...].joined(separator: " ").count)
		if d < bestDiff { bestDiff = d; best = i }
	}
	return [words[..<best].joined(separator: " "), words[best...].joined(separator: " ")]
}

/// A transparent w x h bitmap, drawn into by body (origin bottom left), written as PNG.
func writePNG(_ w: Int, _ h: Int, _ out: String, _ body: () -> Void) {
	guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w, pixelsHigh: h, bitsPerSample: 8, samplesPerPixel: 4,
		hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { fail("no bitmap") }
	rep.size = NSSize(width: w, height: h)
	NSGraphicsContext.saveGraphicsState()
	NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
	NSColor.clear.setFill()
	NSRect(x: 0, y: 0, width: w, height: h).fill()
	body()
	NSGraphicsContext.restoreGraphicsState()
	guard let data = rep.representation(using: .png, properties: [:]) else { fail("no PNG") }
	do { try data.write(to: URL(fileURLWithPath: out)) } catch { fail("cannot write \(out): \(error.localizedDescription)") }
}

func styled(_ s: String, _ size: CGFloat, _ weight: NSFont.Weight, _ color: NSColor, kern: CGFloat = 0) -> NSAttributedString {
	let para = NSMutableParagraphStyle()
	para.alignment = .center
	para.lineSpacing = size * 0.08
	return NSAttributedString(string: s, attributes: [.font: NSFont.systemFont(ofSize: size, weight: weight), .foregroundColor: color,
		.paragraphStyle: para, .kern: kern])
}

/// caption: white heavy text on a near-black box with a lime rule on top; --band makes the box the full --width.
func caption(_ o: [String: String]) {
	guard let raw = o["text"], let out = o["out"] else { fail("caption needs --text and --out", 2) }
	let width = CGFloat(Double(o["width"] ?? "") ?? 1000)
	let size = CGFloat(Double(o["size"] ?? "") ?? 60)
	let band = o["band"] != nil
	let s = styled(captionLines(raw, maxWords: Int(o["max-words"] ?? "") ?? 6).joined(separator: "\n"), size, .heavy, .white)
	let padX = size * 0.55, padY = size * 0.42, rule = max(4, (size * 0.08).rounded())
	let r = s.boundingRect(with: NSSize(width: width - 2 * padX, height: 10000), options: [.usesLineFragmentOrigin, .usesFontLeading])
	let w = band ? Int(width) : Int(ceil(r.width + 2 * padX)), h = Int(ceil(r.height + 2 * padY + rule))
	writePNG(w, h, out) {
		NSColor(srgbRed: 0, green: 0, blue: 0, alpha: 0.86).setFill()
		NSRect(x: 0, y: 0, width: w, height: h).fill()
		lime.setFill()
		NSRect(x: 0, y: CGFloat(h) - rule, width: CGFloat(w), height: rule).fill()
		s.draw(with: NSRect(x: (CGFloat(w) - r.width) / 2, y: padY, width: r.width, height: r.height), options: [.usesLineFragmentOrigin, .usesFontLeading])
	}
}

/// card: the end card, a full frame: the product's name, one line, the URL in lime, centred in --safe "x,y,w,h"
/// (from the top left; the whole frame by default).
func card(_ o: [String: String]) {
	guard let out = o["out"], let w = Int(o["width"] ?? ""), let h = Int(o["height"] ?? "") else { fail("card needs --out, --width, --height", 2) }
	let sf = (o["safe"] ?? "0,0,\(w),\(h)").split(separator: ",").compactMap { Double($0) }
	guard sf.count == 4 else { fail("--safe is x,y,w,h", 2) }
	let safe = CGRect(x: sf[0], y: Double(h) - sf[1] - sf[3], width: sf[2], height: sf[3])	// to a bottom-left origin
	let unit = min(safe.width, safe.height * 1.6) / 1000
	let parts = [styled(o["title"] ?? "", 104 * unit, .black, .white, kern: -1.5 * unit), styled(o["line"] ?? "", 50 * unit, .medium, grey),
		styled(o["url"] ?? "", 70 * unit, .heavy, lime)].filter { $0.length > 0 }
	let rects = parts.map { $0.boundingRect(with: NSSize(width: safe.width * 0.94, height: 10000), options: [.usesLineFragmentOrigin, .usesFontLeading]) }
	let gap = 44 * unit, rule = max(5, 9 * unit)
	let total = rects.reduce(0) { $0 + $1.height } + gap * CGFloat(parts.count) + rule
	writePNG(w, h, out) {
		NSColor.black.setFill()
		NSRect(x: 0, y: 0, width: w, height: h).fill()
		var y = safe.midY + total / 2
		lime.setFill()	// a short lime rule above the name, as the site's section rules
		NSRect(x: safe.midX - 70 * unit, y: y - rule, width: 140 * unit, height: rule).fill()
		y -= rule + gap
		for (p, r) in zip(parts, rects) {
			y -= r.height
			p.draw(with: NSRect(x: safe.midX - r.width / 2, y: y, width: r.width, height: r.height), options: [.usesLineFragmentOrigin, .usesFontLeading])
			y -= gap
		}
	}
}

// MARK: - record

final class Recorder: NSObject, SCStreamDelegate, SCRecordingOutputDelegate, @unchecked Sendable {
	let options: [String: String]
	var stream: SCStream?
	var recording: SCRecordingOutput?
	var stopping = false
	var started = false

	init(_ o: [String: String]) { options = o }

	/// The app's largest on-screen window, waited for.
	func findWindow() async -> (SCWindow, SCShareableContent) {
		let bundleID = options["bundle-id"]
		let pid = options["pid"].flatMap { Int32($0) }
		let wait = Double(options["wait-seconds"] ?? "") ?? 30
		let end = Date().addingTimeInterval(wait)
		while true {
			let content: SCShareableContent
			do {
				content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
			} catch {
				fail("screen recording is not allowed for this process (\(error.localizedDescription)). Allow the app that runs "
					+ "this tool (the terminal or agent host) in System Settings > Privacy & Security > Screen & System Audio "
					+ "Recording, then restart that app.", 3)
			}
			let mine = content.windows.filter { w in
				guard let app = w.owningApplication, w.isOnScreen, w.windowLayer == 0, w.frame.width > 200, w.frame.height > 150 else { return false }
				if let b = bundleID { return app.bundleIdentifier == b }
				if let p = pid { return app.processID == p }
				return false
			}
			if let w = mine.max(by: { $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height }) { return (w, content) }
			if Date() > end { fail("no on-screen window of \(bundleID ?? options["pid"] ?? "?") after \(Int(wait)) s") }
			try? await Task.sleep(nanoseconds: 300_000_000)
		}
	}

	func start() async {
		guard let out = options["out"] else { fail("record needs --out", 2) }
		guard options["bundle-id"] != nil || options["pid"] != nil else { fail("record needs --bundle-id or --pid", 2) }
		let (window, content) = await findWindow()
		guard let app = window.owningApplication else { fail("the window has no app") }
		// The display the window is on: the filter is that display with this app alone on it, so the app's audio is
		// captured with it and no other window can cover the picture; the window is then cut out with sourceRect.
		let center = CGPoint(x: window.frame.midX, y: window.frame.midY)
		guard let display = content.displays.first(where: { $0.frame.contains(center) }) ?? content.displays.first else { fail("no display") }
		let filter = SCContentFilter(display: display, including: [app], exceptingWindows: [])
		let scale = CGFloat(filter.pointPixelScale)
		var rect = window.frame.offsetBy(dx: -display.frame.origin.x, dy: -display.frame.origin.y)
		if let cs = options["content-size"] {
			let p = cs.split(separator: "x").compactMap { Double($0) }
			guard p.count == 2 else { fail("--content-size is WxH", 2) }
			let w = min(CGFloat(p[0]), rect.width), h = min(CGFloat(p[1]), rect.height)
			rect = CGRect(x: rect.minX + (rect.width - w) / 2, y: rect.maxY - h, width: w, height: h)
		}
		let fps = Int32(options["fps"] ?? "") ?? 60
		let cfg = SCStreamConfiguration()
		cfg.sourceRect = rect
		cfg.width = Int(rect.width * scale)
		cfg.height = Int(rect.height * scale)
		cfg.scalesToFit = false
		cfg.minimumFrameInterval = CMTime(value: 1, timescale: fps)
		cfg.queueDepth = 8
		cfg.showsCursor = false
		cfg.capturesAudio = true
		cfg.excludesCurrentProcessAudio = true
		cfg.sampleRate = 48000
		cfg.channelCount = 2
		cfg.colorSpaceName = CGColorSpace.sRGB
		let info: [String: Any] = [
			"pid": app.processID, "bundleId": app.bundleIdentifier, "windowTitle": window.title ?? "",
			"window": ["x": window.frame.minX, "y": window.frame.minY, "w": window.frame.width, "h": window.frame.height],
			"source": ["x": rect.minX, "y": rect.minY, "w": rect.width, "h": rect.height],
			"scale": scale, "pixels": ["w": cfg.width, "h": cfg.height], "fps": fps,
		]
		if let f = options["info-file"], let d = try? JSONSerialization.data(withJSONObject: info, options: [.prettyPrinted, .sortedKeys]) {
			try? d.write(to: URL(fileURLWithPath: f))
		}
		say("window \"\(window.title ?? "")\" of \(app.bundleIdentifier) (pid \(app.processID)): \(Int(rect.width))x\(Int(rect.height)) pt at \(Int(rect.minX)),\(Int(rect.minY)) -> \(cfg.width)x\(cfg.height) px, \(fps) fps")

		let url = URL(fileURLWithPath: out)
		try? FileManager.default.removeItem(at: url)
		let rc = SCRecordingOutputConfiguration()
		rc.outputURL = url
		rc.outputFileType = url.pathExtension.lowercased() == "mp4" ? .mp4 : .mov
		rc.videoCodecType = (options["codec"] ?? "h264") == "hevc" ? .hevc : .h264
		let rec = SCRecordingOutput(configuration: rc, delegate: self)
		let s = SCStream(filter: filter, configuration: cfg, delegate: self)
		do {
			try s.addRecordingOutput(rec)
			try await s.startCapture()
		} catch {
			fail("cannot start the capture: \(error.localizedDescription)")
		}
		stream = s
		recording = rec
		let maxSeconds = Double(options["max-seconds"] ?? "") ?? 120
		DispatchQueue.main.asyncAfter(deadline: .now() + maxSeconds) { [weak self] in
			say("max duration reached (\(Int(maxSeconds)) s)")
			self?.stop()
		}
	}

	func stop() {
		guard !stopping else { return }
		stopping = true
		guard let s = stream else { exit(1) }
		Task {
			do { try await s.stopCapture() } catch { say("stop: \(error.localizedDescription)") }
			// recordingOutputDidFinishRecording ends the process; a file that never started ends it here
			try? await Task.sleep(nanoseconds: 5_000_000_000)
			fail("the recording did not finish within 5 s of stopping")
		}
	}

	func recordingOutputDidStartRecording(_ recordingOutput: SCRecordingOutput) {
		started = true
		say("recording")
		if let f = options["ready-file"] { FileManager.default.createFile(atPath: f, contents: Data("recording\n".utf8)) }
	}

	func recordingOutput(_ recordingOutput: SCRecordingOutput, didFailWithError error: Error) {
		fail("recording failed: \(error.localizedDescription)")
	}

	func recordingOutputDidFinishRecording(_ recordingOutput: SCRecordingOutput) {
		// ScreenCaptureKit can end a recording by itself (seen once after 13 s, no error given): a file that ended
		// before anyone asked is not the take, exit 4
		if !stopping { say("the recording ended by itself before a stop was asked: \(options["out"] ?? "") is short"); exit(4) }
		say("finished: \(options["out"] ?? "")")
		exit(started ? 0 : 1)
	}

	func stream(_ stream: SCStream, didStopWithError error: Error) {
		say("the stream stopped: \(error.localizedDescription)")
		stop()
	}
}

nonisolated(unsafe) var stopSignal: sig_atomic_t = 0

let argv = CommandLine.arguments
guard argv.count >= 2 else { fail("usage: mdmm-recorder record|caption --… (see the top of main.swift)", 2) }
let opts = parse(argv.dropFirst(2))
switch argv[1] {
case "caption":
	caption(opts)
	exit(0)
case "card":
	card(opts)
	exit(0)
case "front":
	// The app with this process id to the front. By pid, never by bundle id: an older copy of the same app (same
	// bundle id) elsewhere on the disk would be launched or picked instead.
	guard let p = opts["pid"].flatMap({ Int32($0) }), let app = NSRunningApplication(processIdentifier: p) else { fail("front needs the --pid of a running app", 2) }
	exit(app.activate(options: [.activateAllWindows]) ? 0 : 1)
case "record":
	let recorder = Recorder(opts)
	// SIGINT / SIGTERM set a flag that a main run loop timer reads (a dispatch signal source held in a local is
	// released after its last use under -O, and the signal is then never seen)
	signal(SIGINT) { _ in stopSignal = 1 }
	signal(SIGTERM) { _ in stopSignal = 1 }
	let poll = Timer(timeInterval: 0.05, repeats: true) { _ in
		if stopSignal != 0 { stopSignal = 0; say("stop asked (signal)"); recorder.stop() }
	}
	RunLoop.main.add(poll, forMode: .common)
	if opts["stdin"] != nil {
		Thread.detachNewThread {
			while let line = readLine() { if line.trimmingCharacters(in: .whitespaces) == "stop" { break } }
			DispatchQueue.main.async { say("stop asked (stdin)"); recorder.stop() }
		}
	}
	Task { await recorder.start() }
	// a real main thread run loop: ScreenCaptureKit puts its menu bar indicator up from the main queue, which
	// dispatchMain() would drain on a worker thread
	NSApplication.shared.setActivationPolicy(.prohibited)
	NSApplication.shared.run()
default:
	fail("unknown command \(argv[1]): record or caption", 2)
}
