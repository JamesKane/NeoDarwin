"""NeoDarwin Swift macros enforcing the language policy (T1).

Every first-party Swift target uses these instead of the rules_swift symbols
directly, so the language mode and warning policy cannot drift per target.
See docs/architecture/language-policy.md.
"""

load("@rules_swift//swift:swift.bzl", "swift_binary", "swift_library")

# Swift 6 language mode implies complete strict concurrency checking.
ND_SWIFT_COPTS = [
    "-swift-version",
    "6",
    "-warnings-as-errors",
]

def nd_swift_library(name, copts = [], **kwargs):
    swift_library(name = name, copts = ND_SWIFT_COPTS + copts, **kwargs)

def nd_swift_binary(name, copts = [], **kwargs):
    swift_binary(name = name, copts = ND_SWIFT_COPTS + copts, **kwargs)
