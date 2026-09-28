// SPDX-License-Identifier: BSD-2-Clause
// Type-checks that nd_audio.h imports into Swift 6 with every record usable:
// the toolkit's Audio layer (P4-12) is built on this import. Never linked into a product.

import NDAudio

public func ndAudioImportCheck() -> Int {
    var c = AUcontract()
    c.size = UInt32(MemoryLayout<AUcontract>.size)
    c.latency_ns = c.lat_client_ns &+ c.lat_convert_ns &+ c.lat_mixer_ns &+ c.lat_device_ns &+ c.lat_hw_ns
    let when = au_frame_to_host(&c, 0)
    var ring = AUring_hdr()
    ring.client_pos = ring.server_pos
    var cmd = AUvcmd()
    cmd.gain = 1.0
    let render: AUrender_fn = { _, info, _ in _ = info?.pointee.frames }
    var p = AUstream_params()
    p.render = render
    let err: AUerror = AU_OK
    return MemoryLayout<AUcontract>.size + MemoryLayout<AUring_hdr>.size + MemoryLayout<AUvcmd>.size
        + Int(truncatingIfNeeded: when) * 0 + Int(err.rawValue) + Int(p.size) + Int(cmd.op)
}
