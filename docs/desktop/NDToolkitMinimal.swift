// SPDX-License-Identifier: BSD-2-Clause
//
// NDToolkitMinimal.swift - the S7 minimal program (window + frame + input +
// sound) written against NDToolkit.swift, type-checked with it and never run.
// TK-T-060 counts its toolkit call sites (marked //C n); the bar is 13
// (TK-PERF-001, charter §1). The C version is in toolkit-api.md §7.

func minimalProgram() throws(ToolkitError) -> Int32 {
    let loop = try Loop()                                                        //C 1
    let window = try loop.openWindow("minimal", size: Size(640, 360))            //C 2
    let tone = try AudioBuffer(frames: 24_000, format: AudioFormat(rate: 48_000, channels: 1), loop: loop) {  //C 3
        frame, _ in Float(frame % 109) / 109 - 0.5
    }
    window.requestFrame()                                                        //C 4
    var playing = false
    while true {
        for event in loop.wait() {                                               //C 5
            switch event.payload {
            case .configure(let c):
                if c.visibility == .shown, !playing {
                    playing = Mixer.shared(loop: loop)                           //C 6
                        .play(tone) != nil                                       //C 7
                }
            case .frame(let f):
                guard var surface = window.cpuSurface() else { break }           //C 8
                surface.fill(0xff00_0000 | UInt32(truncatingIfNeeded: f.target.ns >> 24) & 0xff)   //C 9
                window.present(surface, damage: nil)                             //C 10
                window.requestFrame()                                            //C 11
            case .key(let k):
                if k.keysym == Keysym.escape { return 0 }
            case .text(let t):
                print(t.text.string)                                             //C 12
            case .close, .quit:
                return 0
            default:
                break
            }
        }
    }
}
