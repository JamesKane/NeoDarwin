// SPDX-License-Identifier: BSD-2-Clause
//
// NDToolkit.swift - the NeoDarwin toolkit's public Swift surface, API version 1.
//
// Normative text: docs/desktop/toolkit-api.md (requirement ids TK-*). This file
// is an interface: it is type-checked by //docs/desktop:ndtoolkit_interface
// (Swift 6, warnings as errors) and never linked into a product. Bodies are
// fatalError("interface only") (spec-conventions.md §6). Where this file and the
// text differ, the text is normative.
//
// Ownership (language-policy.md §4): owners of resources are ~Copyable
// (Loop, CPUSurface, Canvas, GPUContext, GPUFrame, AudioStream, AudioBuffer,
// IOBuffer, ThreadHandle); everything that crosses a thread or the C ABI is a
// Sendable value or a u64 handle. Functions on the T2 paths (event decode,
// layout and paint, drawing, audio render) carry @_noLocks and
// @_noAllocation where the whole function is Swift; the system call at the end
// of wait, present and draw stays in an unannotated outer wrapper (S7 W8).
//
// Pinned-toolchain fallbacks (toolkit-api.md §4.0.6): a property returning a
// MutableRawSpan needs the experimental Lifetimes feature, so pixel access is
// the closure form withPixels(_:); event text is Span8 (a pointer and a count
// valid until the next wait), because a ~Escapable Span cannot be stored in an
// enum payload.

// MARK: - Time (TK-TIME)

/// An absolute time on the SC clock (mach_absolute_time in ns; SC-TIME-001).
public struct Deadline: Comparable, Hashable, Sendable {
    public var ns: UInt64
    public init(ns: UInt64) { self.ns = ns }
    public static var now: Deadline { fatalError("interface only") }
    public static func < (a: Deadline, b: Deadline) -> Bool { a.ns < b.ns }
    public static func + (d: Deadline, t: Duration) -> Deadline { fatalError("interface only") }
    public static func - (a: Deadline, b: Deadline) -> Duration { fatalError("interface only") }
}

/// Sleeps the calling thread until `deadline`, late by at most `leeway` (SC-LIB-001).
public func sleep(until deadline: Deadline, leeway: Duration = .zero) { fatalError("interface only") }

/// What a thread says its work is (SC-INT-002). Raw values equal enum sc_intent.
public enum ThreadIntent: UInt32, Sendable {
    case interactive = 1, throughput = 2, background = 3, audio = 4
}

/// Sets the calling thread's intent (SC-INT-001). There is no call for another thread.
@discardableResult
public func setThreadIntent(_ intent: ThreadIntent) -> Bool { fatalError("interface only") }

/// A thread started with an intent. Joining consumes it.
public struct ThreadHandle: ~Copyable, Sendable {
    public let raw: UInt64
    public consuming func join() { fatalError("interface only") }
    deinit {}
}

/// Starts a thread with `intent` and `name`. There is no affinity parameter (SC-PLACE-001).
public func spawn(intent: ThreadIntent, name: String,
                  _ body: @escaping @Sendable () -> Void) throws(ToolkitError) -> ThreadHandle {
    fatalError("interface only")
}

// MARK: - Errors (TK-ERR)

/// Raw values equal enum ndtk_error.
public enum ErrorCode: Int32, Sendable {
    case handle = 1, argument, state, unsupported, permission, noMemory, limit, busy, gone, service,
         admission, format, device, interrupted, timeout, io, gpu, noPress, noFocus, cancelled
}

/// The typed error of every throwing call. `detail` is the lower layer's text, verbatim where one
/// exists (for .admission, SC's reason: AU-ABI-002); `refusal` is set for .admission only.
public struct ToolkitError: Error, Sendable {
    public var code: ErrorCode
    public var detail: String
    public var refusal: AdmissionRefusal?
    public init(code: ErrorCode, detail: String, refusal: AdmissionRefusal? = nil) {
        self.code = code; self.detail = detail; self.refusal = refusal
    }
}

/// The calling thread's last failure from a non-throwing call (TK-ERR-002); nil after success.
public func lastError() -> ToolkitError? { fatalError("interface only") }

// MARK: - Handles (TK-HDL)

/// Index + generation handles; `raw` is the u64 of the C ABI.
public struct Window: Hashable, Sendable {
    public let raw: UInt64
    public init(raw: UInt64) { self.raw = raw }
    public static let none = Window(raw: 0)
}
public struct TimerID: Hashable, Sendable { public let raw: UInt64 }
public struct SourceID: Hashable, Sendable { public let raw: UInt64 }
public struct OutputID: Hashable, Sendable { public let raw: UInt64 }
public struct GamepadID: Hashable, Sendable { public let raw: UInt64 }
public struct DialogID: Hashable, Sendable { public let raw: UInt64 }
public struct IORequest: Hashable, Sendable { public let raw: UInt64 }
public struct RequestTag: Hashable, Sendable { public let raw: UInt32 }
public struct NodeID: Hashable, Sendable {
    public let raw: UInt64
    public static let none = NodeID(raw: 0)
    public init(raw: UInt64) { self.raw = raw }
    /// Stores the handle in `slot` and returns it, so a one-expression tree can hand handles back.
    public func bind(_ slot: inout NodeID) -> NodeID { slot = self; return self }
}
/// Audio stream, buffer and voice handles are the audio service's AUhandles.
public struct VoiceID: Hashable, Sendable { public let raw: UInt64 }

// MARK: - Capabilities (TK-CAP)

public struct Capabilities: OptionSet, Sendable {
    public var rawValue: UInt64
    public init(rawValue: UInt64) { self.rawValue = rawValue }
    public static let windows = Capabilities(rawValue: 1 << 0)
    public static let frameFeedback = Capabilities(rawValue: 1 << 1)
    public static let viewport = Capabilities(rawValue: 1 << 2)
    public static let decor = Capabilities(rawValue: 1 << 3)
    public static let popup = Capabilities(rawValue: 1 << 4)
    public static let keymap = Capabilities(rawValue: 1 << 5)
    public static let ime = Capabilities(rawValue: 1 << 6)
    public static let pen = Capabilities(rawValue: 1 << 7)
    public static let pointerLock = Capabilities(rawValue: 1 << 8)
    public static let scroll = Capabilities(rawValue: 1 << 9)
    public static let outputs = Capabilities(rawValue: 1 << 10)
    public static let gamepad = Capabilities(rawValue: 1 << 11)
    public static let gpuSurface = Capabilities(rawValue: 1 << 12)
    public static let gpuHelper = Capabilities(rawValue: 1 << 13)
    public static let gpuBudget = Capabilities(rawValue: 1 << 14)
    public static let audio = Capabilities(rawValue: 1 << 15)
    public static let audioCapture = Capabilities(rawValue: 1 << 16)
    public static let audioRealtimeGroup = Capabilities(rawValue: 1 << 17)
    public static let timerPrecise = Capabilities(rawValue: 1 << 18)
    public static let intent = Capabilities(rawValue: 1 << 19)
    public static let ioAsync = Capabilities(rawValue: 1 << 20)
    public static let pathWatch = Capabilities(rawValue: 1 << 21)
    public static let agent = Capabilities(rawValue: 1 << 22)
    public static let textShaping = Capabilities(rawValue: 1 << 23)
    public static let fileDialog = Capabilities(rawValue: 1 << 24)
}

public let toolkitAPIVersion: UInt32 = 1

// MARK: - Geometry

public struct Size: Hashable, Sendable {
    public var width: Float, height: Float
    public init(_ width: Float, _ height: Float) { self.width = width; self.height = height }
}
public struct Point: Hashable, Sendable {
    public var x: Float, y: Float
    public init(_ x: Float, _ y: Float) { self.x = x; self.y = y }
}
public struct Rect: Hashable, Sendable {
    public var x: Float, y: Float, width: Float, height: Float
    public init(x: Float, y: Float, width: Float, height: Float) {
        self.x = x; self.y = y; self.width = width; self.height = height
    }
}
/// Buffer pixels, half-open.
public struct PixelRect: Hashable, Sendable {
    public var x0: Int32, y0: Int32, x1: Int32, y1: Int32
    public init(x0: Int32, y0: Int32, x1: Int32, y1: Int32) { self.x0 = x0; self.y0 = y0; self.x1 = x1; self.y1 = y1 }
}
public struct PixelSize: Hashable, Sendable {
    public var width: UInt32, height: UInt32
    public init(_ width: UInt32, _ height: UInt32) { self.width = width; self.height = height }
}
public struct Rational: Hashable, Sendable {
    public var num: UInt32, den: UInt32
    public init(num: UInt32, den: UInt32) { self.num = num; self.den = den }
}

// MARK: - Events (TK-EV)

public struct EventKind: RawRepresentable, Hashable, Sendable {
    public var rawValue: UInt16
    public init(rawValue: UInt16) { self.rawValue = rawValue }
    public static let keyDown = EventKind(rawValue: 1), keyUp = EventKind(rawValue: 2)
    public static let text = EventKind(rawValue: 3), preedit = EventKind(rawValue: 4)
    public static let pointerMotion = EventKind(rawValue: 5), pointerDown = EventKind(rawValue: 6)
    public static let pointerUp = EventKind(rawValue: 7), scroll = EventKind(rawValue: 8)
    public static let relative = EventKind(rawValue: 9), configure = EventKind(rawValue: 10)
    public static let frame = EventKind(rawValue: 11), gamepad = EventKind(rawValue: 12)
    public static let timer = EventKind(rawValue: 13), fd = EventKind(rawValue: 14)
    public static let message = EventKind(rawValue: 15), wake = EventKind(rawValue: 16)
    public static let audio = EventKind(rawValue: 17), close = EventKind(rawValue: 18)
    public static let quit = EventKind(rawValue: 19), ack = EventKind(rawValue: 20)
    public static let deleteSurrounding = EventKind(rawValue: 21), focus = EventKind(rawValue: 22)
    public static let pointerEnter = EventKind(rawValue: 23), pointerLeave = EventKind(rawValue: 24)
    public static let state = EventKind(rawValue: 25), menu = EventKind(rawValue: 26)
    public static let drop = EventKind(rawValue: 27), popupDone = EventKind(rawValue: 28)
    public static let pointerConstraint = EventKind(rawValue: 29), proximity = EventKind(rawValue: 30)
    public static let keymap = EventKind(rawValue: 31), output = EventKind(rawValue: 32)
    public static let theme = EventKind(rawValue: 33), io = EventKind(rawValue: 34)
    public static let dialog = EventKind(rawValue: 35), agent = EventKind(rawValue: 36)
    public static let expose = EventKind(rawValue: 37), move = EventKind(rawValue: 38)
    public static let path = EventKind(rawValue: 39), gpu = EventKind(rawValue: 40)
}

/// UTF-8 bytes in the loop's frame arena, valid until the next wait or poll (TK-MEM-003).
/// Not Sendable: it borrows the loop arena.
public struct Span8: Sequence {
    public let base: UnsafePointer<UInt8>?
    public let count: Int
    public init(base: UnsafePointer<UInt8>?, count: Int) { self.base = base; self.count = count }
    public var isEmpty: Bool { count == 0 }
    public func makeIterator() -> UnsafeBufferPointer<UInt8>.Iterator { fatalError("interface only") }
    /// Copies out; allocates, so not on a T2 path.
    public var string: String { fatalError("interface only") }
}

public struct Modifiers: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let shift = Modifiers(rawValue: 0x1), ctrl = Modifiers(rawValue: 0x2)
    public static let alt = Modifiers(rawValue: 0x4), meta = Modifiers(rawValue: 0x8)
    public static let caps = Modifiers(rawValue: 0x10), num = Modifiers(rawValue: 0x20)
    public static let rightShift = Modifiers(rawValue: 0x100), rightCtrl = Modifiers(rawValue: 0x200)
    public static let rightAlt = Modifiers(rawValue: 0x400), rightMeta = Modifiers(rawValue: 0x800)
    public static let level3 = Modifiers(rawValue: 0x1000)
}

public struct KeyFlags: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let down = KeyFlags(rawValue: 0x1), `repeat` = KeyFlags(rawValue: 0x2)
    public static let noRune = KeyFlags(rawValue: 0x4), imePass = KeyFlags(rawValue: 0x8)
    public static let bound = KeyFlags(rawValue: 0x10), cancel = KeyFlags(rawValue: 0x20)
}

public enum Keysym {
    public static let backspace: UInt32 = 0xff08, tab: UInt32 = 0xff09, `return`: UInt32 = 0xff0d
    public static let escape: UInt32 = 0xff1b, home: UInt32 = 0xff50, left: UInt32 = 0xff51
    public static let up: UInt32 = 0xff52, right: UInt32 = 0xff53, down: UInt32 = 0xff54
    public static let pageUp: UInt32 = 0xff55, pageDown: UInt32 = 0xff56, end: UInt32 = 0xff57
    public static let delete: UInt32 = 0xffff
}

public struct Key: Sendable {
    public var scancode: UInt32      // USB HID usage (WP-KEY-003)
    public var keysym: UInt32
    public var base: UInt32
    public var rune: UInt32
    public var mods: Modifiers
    public var flags: KeyFlags
    public var layout: UInt32
    public var isRepeat: Bool { flags.contains(.repeat) }
}

public struct TextInput {
    public var imeSerial: UInt32
    public var fromKey: Bool
    public var text: Span8
}

public struct Preedit {
    public var imeSerial: UInt32
    public var text: Span8
    public var cursor: Range<Int>?   // byte offsets; nil hides the cursor
    public var styles: UnsafeBufferPointer<UInt32>   // (begin, end, style) triples, valid until the next wait
}

public enum Tool: UInt16, Sendable { case mouse = 0, pen, eraser, brush, pencil, airbrush, lens }

public struct PointerFlags: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let pen = PointerFlags(rawValue: 1), warped = PointerFlags(rawValue: 2), hover = PointerFlags(rawValue: 4)
}

public struct Pointer: Sendable {
    public var position: Point       // content, logical points
    public var buttons: UInt32       // held after the event
    public var button: UInt32        // the one that changed; 0 for motion
    public var mods: Modifiers
    public var flags: PointerFlags
    public var tool: Tool
    public var pressure: Float, tiltX: Float, tiltY: Float, rotation: Float, distance: Float
    public var toolSerial: UInt32
}

public struct Scroll: Sendable {
    public var dx: Float, dy: Float
    public var v120x: Int32, v120y: Int32
    public var precise: Bool, inverted: Bool, stop: Bool
    public var mods: Modifiers
}

public struct Relative: Sendable { public var dx: Float, dy: Float, acceleratedDX: Float, acceleratedDY: Float }

public struct WindowState: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let visible = WindowState(rawValue: 0x1), focused = WindowState(rawValue: 0x2)
    public static let hidden = WindowState(rawValue: 0x4), zoomed = WindowState(rawValue: 0x8)
    public static let grabbed = WindowState(rawValue: 0x10), lagging = WindowState(rawValue: 0x20)
    public static let snapped = WindowState(rawValue: 0x40), full = WindowState(rawValue: 0x80)
    public static let closing = WindowState(rawValue: 0x100), interactive = WindowState(rawValue: 0x200)
    public static let occluded = WindowState(rawValue: 0x400)
}

public enum Visibility: UInt32, Sendable { case unmapped = 0, shown, occluded, offscreen, hidden }

public struct Configure: Sendable {
    public var size: Size            // logical points
    public var pixels: PixelSize     // device pixels covered
    public var scale: Rational
    public var buffer: PixelSize
    public var configSeq: UInt32
    public var state: WindowState
    public var visibility: Visibility
    public var refresh: Duration
    public var output: OutputID
}

public struct FrameFlags: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let hardwareTime = FrameFlags(rawValue: 0x1), composited = FrameFlags(rawValue: 0x2)
    public static let throttled = FrameFlags(rawValue: 0x4), requested = FrameFlags(rawValue: 0x8)
    public static let zeroCopy = FrameFlags(rawValue: 0x10), late = FrameFlags(rawValue: 0x20)
    /// Set only by host shims; never on NeoDarwin (TK-FRM-006).
    public static let presentedEstimated = FrameFlags(rawValue: 0x10000)
}

public struct Frame: Sendable {
    public var frame: UInt64
    public var target: Deadline      // a valid wait deadline
    public var presented: Deadline?  // nil when nothing was shown since the previous frame
    public var refresh: Duration
    public var present: UInt32
    public var configSeq: UInt32
    public var flags: FrameFlags
    public var missed: UInt32
}

public struct Ack: Sendable { public var tag: RequestTag; public var error: ErrorCode?; public var configSeq: UInt32 }

public enum GamepadButton: UInt32, Sendable {
    case a = 0, b, x, y, back, guide, start, leftStick, rightStick, leftShoulder, rightShoulder,
         up, down, left, right
}
public enum GamepadAxis: UInt32, Sendable { case leftX = 0, leftY, rightX, rightY, leftTrigger, rightTrigger }

public struct GamepadEvent: Sendable {
    public enum Change: UInt32, Sendable { case connected = 1, disconnected, button, axis, sensor }
    public var pad: GamepadID
    public var change: Change
    public var index: UInt32
    public var value: Float
    public var buttons: UInt32
}

public struct GamepadState: Sendable {
    public var connected: Bool
    public var buttons: UInt32
    public var axes: SIMD8<Float>
    public var time: Deadline
    public func pressed(_ b: GamepadButton) -> Bool { buttons & (1 << b.rawValue) != 0 }
    public subscript(_ a: GamepadAxis) -> Float { axes[Int(a.rawValue)] }
}

public struct TimerEvent: Sendable { public var timer: TimerID; public var deadline: Deadline; public var fired: Deadline; public var expirations: UInt32 }

public struct FDInterest: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let read = FDInterest(rawValue: 1), write = FDInterest(rawValue: 2), eof = FDInterest(rawValue: 4)
}
public struct FDEvent: Sendable { public var source: SourceID; public var fd: Int32; public var ready: FDInterest; public var data: Int64 }

/// A copyable two-word message; delivered in the target loop's wait (TK-LOOP-006).
public struct Message: Sendable {
    public var a: UInt64, b: UInt64
    public init(a: UInt64, b: UInt64 = 0) { self.a = a; self.b = b }
}

/// An audio service event (AUevent), projected; semantics are AU-EVENT-*.
public struct AudioEvent: Sendable {
    public var type: UInt16          // AUevtype
    public var arg32: UInt32
    public var object: UInt64        // AUhandle
    public var value: UInt64
    public var host: UInt64          // host ticks
}

public struct IOCompletion: Sendable {
    public var request: IORequest
    public var udata: UInt64
    public var bytes: UInt64
    public var error: ErrorCode?
}

public enum AgentVerb: UInt32, Sendable { case app = 0, press, setValue, setText, focus, scroll }
public struct AgentAction {
    public var request: UInt32
    public var verb: AgentVerb
    public var node: NodeID
    public var value: Double
    public var line: Span8
}

public enum Payload {
    case none
    case key(Key)
    case text(TextInput)
    case preedit(Preedit)
    case deleteSurrounding(imeSerial: UInt32, before: UInt32, after: UInt32)
    case pointer(Pointer)
    case scroll(Scroll)
    case relative(Relative)
    case pointerConstraint(state: UInt32, reason: UInt32)
    case proximity(entered: Bool, toolSerial: UInt32, tool: Tool, hardwareSerial: UInt64)
    case configure(Configure)
    case frame(Frame)
    case ack(Ack, message: Span8)
    case focus(Bool)
    case state(WindowState, old: WindowState)
    case move(x: Int32, y: Int32)
    case menu(menu: UInt32, item: UInt32, checked: Bool)
    case drop(at: Point, paths: Span8)
    case popupDone(reason: UInt32)
    case expose(PixelRect)
    case keymap(generation: UInt32, layout: UInt32, name: Span8)
    case output(OutputID, change: UInt32)
    case theme(generation: UInt32)
    case gamepad(GamepadEvent)
    case timer(TimerEvent)
    case fd(FDEvent)
    case message(Message)
    case wake
    case audio(AudioEvent)
    case io(IOCompletion)
    case dialog(DialogID, accepted: Bool, paths: Span8)
    case agent(AgentAction)
    case path(SourceID, what: UInt32, path: Span8)
    case gpu(reason: UInt32, message: Span8)
    case close(forced: Bool)
    case quit
    case unknown(UInt16)
}

public struct Event {
    public var kind: EventKind
    public var flags: UInt16
    public var seq: UInt32
    public var window: Window        // .none for loop-level events
    public var time: Deadline        // sample time for input (WP-EXT-006)
    public var payload: Payload
}

/// The events of one wait, in the loop's frame arena; valid until the next wait or poll.
public struct Events: RandomAccessCollection {
    public let count: Int
    public var startIndex: Int { 0 }
    public var endIndex: Int { count }
    /// Decodes one record: T2.
    public subscript(i: Int) -> Event {
        @_noLocks @_noAllocation get { fatalError("interface only") }
    }
}

// MARK: - Loop (TK-LOOP)

public struct LoopOptions: Sendable {
    public var autoIntent: Bool = false
    public var eventCapacity: Int = 256
    public var arenaBytes: Int = 64 << 10
    public var postCapacity: Int = 1024
    public var intent: ThreadIntent? = nil
    public init() {}
}

/// A cross-thread handle to a loop: post and wake from any thread, including a real-time one.
public struct LoopWaker: Sendable {
    public let raw: UInt64
    /// [RT]: one non-blocking kevent64 trigger (SC-USER-004).
    @discardableResult public func wake() -> Bool { fatalError("interface only") }
    /// [RT]: a ring slot and the wake.
    @discardableResult public func post(_ message: Message) -> Bool { fatalError("interface only") }
}

/// The one wait (P1). Owned by one thread at a time; moving it moves the ownership (TK-THR-002).
public struct Loop: ~Copyable, Sendable {
    public let raw: UInt64
    public init(_ options: LoopOptions = LoopOptions()) throws(ToolkitError) { fatalError("interface only") }
    deinit {}

    /// Blocks until an event, the deadline or a wake. T2 up to the system call.
    public func wait(until deadline: Deadline? = nil, leeway: Duration = .zero) -> Events { fatalError("interface only") }
    public func poll() -> Events { fatalError("interface only") }
    public var waker: LoopWaker { fatalError("interface only") }
    public func timer(at deadline: Deadline, leeway: Duration = .zero, repeating: Duration? = nil) -> TimerID {
        fatalError("interface only")
    }
    @discardableResult public func cancel(_ timer: TimerID) -> Bool { fatalError("interface only") }
    public func watch(fd: Int32, _ interest: FDInterest) -> SourceID { fatalError("interface only") }
    public func watch(machPort: UInt32) -> SourceID { fatalError("interface only") }
    @discardableResult public func unwatch(_ source: SourceID) -> Bool { fatalError("interface only") }
    public var capabilities: Capabilities { fatalError("interface only") }
    /// The loop's kqueue, so another event loop can nest this one.
    public var descriptor: Int32 { fatalError("interface only") }

    // Windows and outputs (TK-WIN)
    public func openWindow(_ title: String, size: Size, options: WindowOptions = []) throws(ToolkitError) -> Window {
        fatalError("interface only")
    }
    public func openWindow(_ descriptor: WindowDescriptor) throws(ToolkitError) -> Window { fatalError("interface only") }
    public func outputs() -> [OutputInfo] { fatalError("interface only") }
    @discardableResult public func setClipboard(_ text: String) -> Bool { fatalError("interface only") }
    public func clipboard() -> String? { fatalError("interface only") }
    @discardableResult public func drag(paths: [String]) -> Bool { fatalError("interface only") }
    public func openDialog(_ descriptor: DialogDescriptor) throws(ToolkitError) -> DialogID { fatalError("interface only") }
    @discardableResult public func watchGamepads(_ on: Bool) -> Bool { fatalError("interface only") }
    /// Folded from the events this loop has read; no system call (TK-INP-014). T2.
    @_noLocks @_noAllocation
    public func gamepadState(_ pad: GamepadID) -> GamepadState? { fatalError("interface only") }

    // Files and I/O (TK-IO)
    public func read(fd: Int32, offset: UInt64, into buffer: consuming IOBuffer, udata: UInt64 = 0) throws(ToolkitError) -> IORequest {
        fatalError("interface only")
    }
    public func write(fd: Int32, offset: UInt64, from buffer: consuming IOBuffer, udata: UInt64 = 0) throws(ToolkitError) -> IORequest {
        fatalError("interface only")
    }
    /// Returns the buffer of a completed or cancelled request, once.
    public func take(_ request: IORequest) -> IOBuffer? { fatalError("interface only") }
    @discardableResult public func cancel(_ request: IORequest) -> Bool { fatalError("interface only") }
    public func watch(path: String, _ what: PathChanges) throws(ToolkitError) -> SourceID { fatalError("interface only") }

    // Agent export (TK-AGENT)
    public func exportAgent(appID: String, version: String) throws(ToolkitError) { fatalError("interface only") }
    @discardableResult public func addAgentVerb(_ name: String, parameters: String, description: String) -> Bool {
        fatalError("interface only")
    }
    @discardableResult public func setAgentState(json: String) -> Bool { fatalError("interface only") }
    @discardableResult public func replyAgent(_ request: UInt32, json: String) -> Bool { fatalError("interface only") }
    @discardableResult public func logAgent(json: String) -> Bool { fatalError("interface only") }
}

// MARK: - Callback driver (TK-DRV)

public enum AppResult: Int32, Sendable { case `continue` = 0, success = 1, failure = 2 }

/// SDL3's init / iterate / event / quit over a Loop (R3). Returns the exit status.
public func run<State>(options: LoopOptions = LoopOptions(),
                       init: (borrowing Loop) throws(ToolkitError) -> State,
                       iterate: (inout State, borrowing Loop) -> AppResult,
                       event: (inout State, borrowing Loop, Event) -> AppResult,
                       quit: (inout State, AppResult) -> Void) -> Int32 {
    fatalError("interface only")
}

// MARK: - Windows and outputs (TK-WIN)

public struct WindowOptions: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let hidden = WindowOptions(rawValue: 0x1), noDecor = WindowOptions(rawValue: 0x2)
    public static let fixedSize = WindowOptions(rawValue: 0x4), alpha = WindowOptions(rawValue: 0x8)
    public static let noFocus = WindowOptions(rawValue: 0x10), onTop = WindowOptions(rawValue: 0x20)
    public static let wantAcks = WindowOptions(rawValue: 0x40), noRetain = WindowOptions(rawValue: 0x80)
    /// `buffer logical` instead of the default `buffer device`.
    public static let logicalBuffer = WindowOptions(rawValue: 0x100)
}

public enum WindowKind: UInt32, Sendable { case toplevel = 0, transient, popup, tooltip }
public enum BufferMode: UInt32, Sendable { case logical = 0, device, fixed }

public struct WindowDescriptor: Sendable {
    public var kind: WindowKind = .toplevel
    public var title: String = ""
    public var size: Size = Size(640, 480)
    public var options: WindowOptions = []
    public var parent: Window = .none
    public var minSize: Size? = nil
    public var maxSize: Size? = nil
    public init() {}
}

public enum Edge: UInt32, Sendable { case none = 0, n, s, e, w, ne, nw, se, sw, center }
public enum HitRegion: Sendable { case client, drag, close, depth, zoom, hide, menu, edge(Edge) }
public enum PointerConstraint: Sendable { case free(at: Point?), lock, confine(Rect), warp(Point) }
public struct PopupOptions: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let flip = PopupOptions(rawValue: 1), slide = PopupOptions(rawValue: 2)
    public static let resize = PopupOptions(rawValue: 4), grab = PopupOptions(rawValue: 8)
}

public enum WindowRequest: Sendable {
    case title(String), size(Size), move(Point)
    case show, hide, raise, lower, focus, zoom
    case fullscreen(Bool)
    case minSize(Size), maxSize(Size)
    case decor(server: Bool)
    case hit(HitRegion, Rect), hitClear
    case interactiveMove, interactiveResize(Edge)
    case pointer(PointerConstraint)
    case keyRepeat(Bool)
    case menu(String), menuItem(id: UInt32, enabled: Bool)
    case cursorImage([UInt8]?), cursorVisible(Bool)
    case workspace(UInt32), parent(Window)
    case latency(UInt32)
    case buffer(BufferMode, fixed: PixelSize?)
    case viewport(PixelRect?, nearest: Bool)
    case popup(parent: Window, anchor: Rect, at: Edge, gravity: Edge, options: PopupOptions)
    case textInputRect(Rect)
}

public enum TextPurpose: UInt32, Sendable { case normal = 0, password, number, phone, email, url, terminal }

public struct TextInputState: Sendable {
    public var enabled: Bool = false
    public var purpose: TextPurpose = .normal
    public var surrounding: String = ""
    public var cursor: Int = 0      // byte offsets into surrounding
    public var anchor: Int = 0
    public var caret: Rect = Rect(x: 0, y: 0, width: 0, height: 0)
    public init() {}
}

public struct OutputInfo: Sendable {
    public var output: OutputID
    public var name: String
    public var scale: Rational
    public var refresh: Duration
    public var rect: Rect
    public var pixels: PixelSize
    public var primary: Bool
    public var hardwareTime: Bool
}

public enum DialogKind: UInt32, Sendable { case open = 1, save, folder, message }
public struct DialogDescriptor: Sendable {
    public var kind: DialogKind = .open
    public var parent: Window = .none
    public var title: String = ""
    public var path: String = ""
    public var filter: String? = nil
    public var multiple: Bool = false
    public init() {}
}

extension Window {
    public func close() { fatalError("interface only") }
    /// Applied or refused when the call returns (WP-ACK-001); false with lastError() when refused.
    @discardableResult public func set(_ request: WindowRequest) -> Bool { fatalError("interface only") }
    /// As set(_:), and an .ack event with the returned tag follows the events the request caused.
    public func submit(_ request: WindowRequest) -> RequestTag? { fatalError("interface only") }
    @discardableResult public func ctl(_ line: String) -> RequestTag? { fatalError("interface only") }
    @discardableResult public func attach(to loop: borrowing Loop) -> Bool { fatalError("interface only") }
    public var id: UInt32 { fatalError("interface only") }
    @discardableResult public func setTextInput(_ state: TextInputState) -> Bool { fatalError("interface only") }
    @discardableResult public func resetTextInput() -> Bool { fatalError("interface only") }

    // Frames and surfaces (TK-FRM)
    /// Marks the window; its loop writes `wantframe` before the next wait (TK-FRM-001).
    @discardableResult public func requestFrame() -> Bool { fatalError("interface only") }
    public func cpuSurface() -> CPUSurface? { fatalError("interface only") }
    @discardableResult
    public func present(_ surface: consuming CPUSurface, damage: PixelRect?, configSeq: UInt32? = nil, at: Deadline? = nil) -> Bool {
        fatalError("interface only")
    }

    // GPU (TK-GPU)
    public func gpuHandleBag() -> GPUHandleBag? { fatalError("interface only") }
    public func openGPU(_ descriptor: GPUDescriptor = GPUDescriptor()) throws(ToolkitError) -> GPUContext {
        fatalError("interface only")
    }
    /// Raw webgpu.h or Vulkan path: states which configuration the next GPU present was drawn for.
    @discardableResult public func presented(configSeq: UInt32) -> Bool { fatalError("interface only") }

    public func measure(_ text: String, class: String, maxWidth: Float = .infinity) -> TextMetrics? {
        fatalError("interface only")
    }
}

// MARK: - Frames and surfaces (TK-FRM), Drawing (TK-DRAW)

/// Shared pixels of one configuration. Dropping it without presenting shows nothing.
public struct CPUSurface: ~Copyable {
    public let width: UInt32, height: UInt32, stride: Int
    public let chan: UInt32
    public let configSeq: UInt32
    /// 0: contents undefined; n: holds the frame presented n presents ago (TK-FRM-009).
    public let age: UInt32
    public mutating func withPixels<R, Failure: Error>(_ body: (inout MutableRawSpan) throws(Failure) -> R) throws(Failure) -> R {
        fatalError("interface only")
    }
    @_noLocks @_noAllocation
    public mutating func fill(_ pixel: UInt32, rect: PixelRect? = nil) { fatalError("interface only") }
    /// Draws through a canvas in the frame arena; returns the damage drawn.
    public mutating func draw(scale: Rational, _ body: (inout Canvas) -> Void) -> PixelRect? { fatalError("interface only") }
    deinit {}
}

public struct Color: Hashable, Sendable {
    public var argb: UInt32
    public init(argb: UInt32) { self.argb = argb }
}

public struct TextMetrics: Sendable {
    public var size: Size
    public var ascent: Float, descent: Float
    public var lines: UInt32
    public var clusters: UInt32
}

/// A draw list in the frame arena, valid inside the closure that received it. Every operation is T2.
public struct Canvas: ~Copyable {
    @_noLocks @_noAllocation public mutating func clip(_ rect: Rect) { fatalError("interface only") }
    @_noLocks @_noAllocation public mutating func unclip() { fatalError("interface only") }
    @_noLocks @_noAllocation public mutating func fill(_ rect: Rect, radius: Float = 0, _ color: Color) {
        fatalError("interface only")
    }
    @_noLocks @_noAllocation public mutating func stroke(_ rect: Rect, radius: Float = 0, width: Float, _ color: Color) {
        fatalError("interface only")
    }
    @_noLocks @_noAllocation public mutating func polyline(_ xy: Span<Float>, width: Float, _ color: Color) {
        fatalError("interface only")
    }
    /// The theme's material for `class` and `part`: widgets paint only through this (P7).
    @_noLocks @_noAllocation public mutating func material(_ rect: Rect, class: StaticString, part: UInt32) {
        fatalError("interface only")
    }
    /// Shaped text (TK-TEXT-001) in the font the theme gives `class`.
    @_noLocks @_noAllocation public mutating func text(_ utf8: Span<UInt8>, at: Point, class: StaticString, maxWidth: Float) {
        fatalError("interface only")
    }
    @_noLocks @_noAllocation public mutating func image(_ pixels: RawSpan, width: UInt32, height: UInt32, stride: Int, chan: UInt32, in rect: Rect) {
        fatalError("interface only")
    }
}

// MARK: - GPU (TK-GPU)

/// The NeoDarwin window-handle bag handed to Vulkan WSI or webgpu.h (R7).
public struct GPUHandleBag: Sendable {
    public var window: Window
    public var windowID: UInt32
    public var surfacePort: UInt32
    public var desktop: String
    public var configSeq: UInt32
}

public struct GPUDescriptor: Sendable {
    public var lowPower: Bool = false
    public var storage: Bool = false       // surface textures also allow STORAGE_BINDING
    public var srgb: Bool = false
    public var latency: UInt32 = 0          // 0 mailbox
    public var fixedBuffer: PixelSize? = nil
    public var features: [UInt32] = []      // WGPUFeatureName values
    public init() {}
}

/// A configured webgpu.h device, queue and surface for one window: the one-call helper.
public struct GPUContext: ~Copyable {
    public let instance: OpaquePointer
    public let adapter: OpaquePointer
    public let device: OpaquePointer
    public let queue: OpaquePointer
    public let surface: OpaquePointer
    public let format: UInt32              // WGPUTextureFormat
    public let usage: UInt64               // WGPUTextureUsage
    /// Reconfigures the surface if the window's configuration moved, and acquires the frame's texture.
    public mutating func beginFrame() -> GPUFrame? { fatalError("interface only") }
    /// Presents, tagged with the frame's configuration (WP-BUF-007).
    @discardableResult public mutating func endFrame(_ frame: consuming GPUFrame, damage: PixelRect? = nil) -> Bool {
        fatalError("interface only")
    }
    deinit {}
}

/// One surface texture; must be ended exactly once.
public struct GPUFrame: ~Copyable {
    public let texture: OpaquePointer
    public let view: OpaquePointer
    public let size: PixelSize
    public let configSeq: UInt32
    public let reconfigured: Bool
}

public struct GPUBudget: Sendable {
    public var budgetBytes: UInt64
    public var usedBytes: UInt64
    public var pressure: UInt32
}

/// NDTK_E_UNSUPPORTED until P7 provides the budget (TK-GPU-009).
public func gpuBudget() throws(ToolkitError) -> GPUBudget { fatalError("interface only") }

// MARK: - UI (TK-UI, TK-LAYOUT, TK-STYLE, TK-TEXT)

public struct FontSpec: Hashable, Sendable {
    public var family: String
    public var size: Float                 // points
    public var weight: UInt16
    public var monospace: Bool
}

/// A resolved style: read-only for applications; set only by the theme (P7).
public struct Style: Sendable {
    public var background: Color?
    public var foreground: Color
    public var accent: Color
    public var font: FontSpec
    public var padding: Float, gap: Float, radius: Float
    public var minSize: Size
}

/// The theme: the window system's (/n/theme) unless a path is given; reloaded on .theme events.
public struct Theme: Sendable {
    public static var system: Theme { fatalError("interface only") }
    public init(path: String) throws(ToolkitError) { fatalError("interface only") }
    /// Lookup falls back along dots: "label.param", "label", "default".
    public func style(for className: String) -> Style { fatalError("interface only") }
}

public enum NodeChange: Sendable {
    case text(String)
    case value(Double)
    case range(ClosedRange<Double>)
    case flex(Float)
    case hidden(Bool)
    case className(String)
    case accessibleName(String)
}

public enum UIEvent: Sendable {
    case ignored, consumed
    case clicked(NodeID)
    case changed(NodeID, Double)
    case edited(NodeID)
    case focused(NodeID)
}

public struct UIStats: Sendable {
    public var nodes: Int
    public var presents: UInt64
    public var skippedDraws: UInt64
    public var fullRepaints: UInt64
    public var invalidHandles: UInt64
    public var arenaCapacity: Int
    public var arenaHighWater: Int
    public var arenaSpills: UInt64
}

/// The retained node table of one window. Bound to the thread of the window's loop: not Sendable.
public final class UI {
    public init(window: Window, theme: Theme = .system) throws(ToolkitError) { fatalError("interface only") }
    public var root: NodeID {
        get { fatalError("interface only") }
        set { fatalError("interface only") }
    }
    public var theme: Theme {
        get { fatalError("interface only") }
        set { fatalError("interface only") }
    }

    // Construction: leaves first; containers adopt them, so a tree is one expression.
    public func column(_ children: NodeID..., class: String = "column", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func row(_ children: NodeID..., class: String = "row", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func stack(_ children: NodeID..., class: String = "stack", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func scroll(_ child: NodeID, class: String = "scroll", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func spacer(flex: Float = 1) -> NodeID { fatalError("interface only") }
    public func label(_ text: String, class: String = "label", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func button(_ text: String, class: String = "button", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func checkbox(_ text: String, checked: Bool = false, class: String = "checkbox", flex: Float = 0) -> NodeID {
        fatalError("interface only")
    }
    public func slider(_ value: Double, in range: ClosedRange<Double> = 0...1, class: String = "slider", flex: Float = 0) -> NodeID {
        fatalError("interface only")
    }
    public func meter(class: String = "meter", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func scope(capacity: Int = 1024, class: String = "scope", flex: Float = 0) -> NodeID { fatalError("interface only") }
    public func textField(_ text: String = "", purpose: TextPurpose = .normal, class: String = "textField", flex: Float = 0) -> NodeID {
        fatalError("interface only")
    }
    public func textView<S: Sequence<String>>(lines: S, class: String = "textView", flex: Float = 0) -> NodeID {
        fatalError("interface only")
    }
    public func image(_ pixels: RawSpan, width: UInt32, height: UInt32, stride: Int, chan: UInt32,
                      class: String = "image", flex: Float = 0) -> NodeID {
        fatalError("interface only")
    }
    /// A node the application paints; `draw` runs on the draw path (T2).
    public func canvas(class: String = "canvas", flex: Float = 0, draw: @escaping (inout Canvas, Rect) -> Void) -> NodeID {
        fatalError("interface only")
    }
    @discardableResult public func add(_ child: NodeID, to parent: NodeID, at index: Int? = nil) -> Bool { fatalError("interface only") }
    /// Destroys the subtree; its handles become invalid (their generations advance).
    @discardableResult public func destroy(_ node: NodeID) -> Bool { fatalError("interface only") }

    // State
    @discardableResult public func set(_ node: NodeID, _ change: NodeChange) -> Bool { fatalError("interface only") }
    /// Scope samples; unchanged samples do not repaint. T2.
    @_noLocks @_noAllocation @discardableResult
    public func set(_ node: NodeID, samples: Span<Float>) -> Bool { fatalError("interface only") }
    public func value(_ node: NodeID) -> Double { fatalError("interface only") }
    public func text(_ node: NodeID) -> String? { fatalError("interface only") }
    public func frame(_ node: NodeID) -> Rect? { fatalError("interface only") }
    @discardableResult public func focus(_ node: NodeID) -> Bool { fatalError("interface only") }
    @discardableResult public func scroll(_ node: NodeID, by dy: Float) -> Bool { fatalError("interface only") }

    // Loop integration
    /// Routes one event through the node table. T2.
    @_noLocks @_noAllocation
    public func handle(_ event: borrowing Event) -> UIEvent { fatalError("interface only") }
    /// On .frame: layout, paint the damage, present. False when nothing changed (P9). T2 up to the present.
    @discardableResult public func draw() -> Bool { fatalError("interface only") }
    public var stats: UIStats { fatalError("interface only") }
    /// The node table as text, one node per line (P14; TK-AGENT-003).
    public func export() -> String { fatalError("interface only") }
}

// MARK: - Audio (TK-AUD), over the audio service (AU)

public enum SampleFormat: UInt16, Sendable { case f32 = 1, s16, s32, s24in32, u8 }

public struct AudioFormat: Hashable, Sendable {
    public var rate: UInt32
    public var channels: UInt16
    public var format: SampleFormat
    public init(rate: UInt32, channels: UInt16, format: SampleFormat = .f32) {
        self.rate = rate; self.channels = channels; self.format = format
    }
}

/// The admission refusal of the scheduling contract, projected (SC-RT-007).
public struct AdmissionRefusal: Sendable {
    public var reason: UInt32               // sc_rt_reason
    public var maxComputation: Duration
    public var availablePPM: UInt32
    public var budgetPPM: UInt32
}

/// AUcontract, projected. The semantics are AU-CONTRACT-001..007.
public struct AudioContract: Sendable {
    public var stream: UInt64
    public var generation: UInt64
    public var change: UInt32
    public var rate: UInt32, deviceRate: UInt32
    public var periodFrames: UInt32, mixerPeriodFrames: UInt32
    public var period: Duration
    public var channels: UInt16
    public var leadPeriods: UInt16
    public var latency: Duration
    public var latencyClient: Duration, latencyConvert: Duration, latencyMixer: Duration
    public var latencyDevice: Duration, latencyHardware: Duration
    public var deviceUID: SIMD16<UInt8>
    /// Presentation (output) or capture (input) time of client frame `position` (AU-CONTRACT-002).
    public func hostTime(ofFrame position: UInt64) -> Deadline { fatalError("interface only") }
}

/// AUrender_info, projected.
public struct RenderInfo: Sendable {
    public var position: UInt64
    public var presentHost: UInt64
    public var deadlineHost: UInt64
    public var generation: UInt64
    public var frames: UInt32
    public var flags: UInt32                // AU_RF_*: prime, discontinuity, contract
    public var underruns: UInt64
}

/// A render the compiler checks: implementations carry @_noLocks and @_noAllocation (AU-T2-003).
public protocol AudioRenderer: ~Copyable, Sendable {
    @_noLocks @_noAllocation
    mutating func render(_ output: inout MutableSpan<Float>, _ info: RenderInfo)
}

/// The C trampoline of AU-T2-004: identical in type to AUrender_fn.
public typealias CRender = @convention(c) (UnsafeMutableRawPointer?, UnsafeRawPointer?, UnsafeMutableRawPointer?) -> Void

/// A Tier-1 stream (AU-STREAM). Closing is deinit.
public struct AudioStream: ~Copyable, Sendable {
    public let raw: UInt64                  // AUhandle
    /// Callback mode with a checked renderer.
    public static func open<R: AudioRenderer & ~Copyable>(_ format: AudioFormat, periodFrames: UInt32, loop: borrowing Loop,
                                                          renderer: consuming R) throws(ToolkitError) -> AudioStream {
        fatalError("interface only")
    }
    /// Callback mode with a closure (not checkable at the call site; the S7 shape).
    public static func open(_ format: AudioFormat, periodFrames: UInt32, loop: borrowing Loop,
                            render: @escaping @Sendable (inout MutableSpan<Float>, RenderInfo) -> Void) throws(ToolkitError) -> AudioStream {
        fatalError("interface only")
    }
    /// Callback mode with a C render: installed with no Swift thunk (AU-T2-004).
    public static func open(_ format: AudioFormat, periodFrames: UInt32, loop: borrowing Loop,
                            cRender: CRender, context: UnsafeMutableRawPointer?) throws(ToolkitError) -> AudioStream {
        fatalError("interface only")
    }
    /// Queue mode (push for output, read for capture).
    public static func openQueue(_ format: AudioFormat, periodFrames: UInt32, capture: Bool = false,
                                 loop: borrowing Loop) throws(ToolkitError) -> AudioStream {
        fatalError("interface only")
    }
    public func start() throws(ToolkitError) { fatalError("interface only") }
    public func stop() throws(ToolkitError) { fatalError("interface only") }
    public var contract: AudioContract { fatalError("interface only") }
    /// Queue mode, non-blocking: frames moved. [RT] through au_stream_write (AU-LIB-006).
    public func write(_ samples: Span<Float>) -> Int { fatalError("interface only") }
    /// [RT] through au_stream_read.
    public func read(into samples: inout MutableSpan<Float>) -> Int { fatalError("interface only") }
    /// The calling thread joins this stream's admitted deadline (SC-RT-009).
    public func joinDeadline() throws(ToolkitError) { fatalError("interface only") }
    deinit {}
}

/// An immutable buffer on the system mixer (AU-VOICE-001). Releasing lets playing voices finish.
public struct AudioBuffer: ~Copyable, Sendable {
    public let raw: UInt64
    public init(_ samples: Span<Float>, format: AudioFormat, loop: borrowing Loop) throws(ToolkitError) {
        fatalError("interface only")
    }
    public init(frames: Int, format: AudioFormat, loop: borrowing Loop,
                _ fill: (_ frame: Int, _ channel: Int) -> Float) throws(ToolkitError) {
        fatalError("interface only")
    }
    deinit {}
}

/// Tier-2 voices on the system mixer. play, stop and set are [RT] through the au_voice_* calls they wrap.
public struct Mixer: Sendable {
    public static func shared(loop: borrowing Loop) -> Mixer { fatalError("interface only") }
    public func play(_ buffer: borrowing AudioBuffer, gain: Float = 1, looping: Bool = false, at: UInt64? = nil) -> VoiceID? {
        fatalError("interface only")
    }
    @discardableResult public func stop(_ voice: VoiceID) -> Bool { fatalError("interface only") }
    @discardableResult public func set(_ voice: VoiceID, gain: Float) -> Bool { fatalError("interface only") }
    public var contract: AudioContract? { fatalError("interface only") }
}

// MARK: - Files and I/O (TK-IO)

/// Bytes owned by the application, lent to an asynchronous request until its completion.
public struct IOBuffer: ~Copyable, Sendable {
    public init(capacity: Int) { fatalError("interface only") }
    public var count: Int { fatalError("interface only") }
    public func withBytes<R, Failure: Error>(_ body: (RawSpan) throws(Failure) -> R) throws(Failure) -> R {
        fatalError("interface only")
    }
    public mutating func withMutableBytes<R, Failure: Error>(_ body: (inout MutableRawSpan) throws(Failure) -> R) throws(Failure) -> R {
        fatalError("interface only")
    }
    deinit {}
}

public enum KnownPath: UInt32, Sendable { case home = 1, documents, config, data, cache, temp, resources }
public func knownPath(_ which: KnownPath) -> String? { fatalError("interface only") }

public struct PathChanges: OptionSet, Sendable {
    public var rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }
    public static let write = PathChanges(rawValue: 1), delete = PathChanges(rawValue: 2)
    public static let rename = PathChanges(rawValue: 4), attributes = PathChanges(rawValue: 8)
}
