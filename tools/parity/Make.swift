// SPDX-License-Identifier: BSD-2-Clause
//
// Just enough of FreeBSD's make(1) to read the Makefiles of bin, sbin,
// usr.bin and usr.sbin: logical lines, assignments, variable expansion with
// the common modifiers, and .if/.elif/.else/.endif conditions, which are kept
// as text (the inventory's cond column) and evaluated for arm64 with
// src.opts.mk's defaults. Recipes, .for loops and targets are skipped.

import Foundation

/// One Makefile line after continuation lines are joined and comments
/// removed.
enum Statement {
    case assign(name: String, op: String, value: String)
    case directive(keyword: String, argument: String)
}

func statements(_ text: String) -> [Statement] {
    var lines: [String] = []
    var pending = ""
    for raw in text.split(separator: "\n", omittingEmptySubsequences: false) {
        if raw.hasSuffix("\\") {
            pending += raw.dropLast() + " "
            continue
        }
        lines.append(pending + raw)
        pending = ""
    }
    if !pending.isEmpty { lines.append(pending) }

    var out: [Statement] = []
    for line in lines {
        if line.hasPrefix("\t") { continue }  // a recipe line
        let text = stripComment(line).trimmingCharacters(in: .whitespaces)
        if text.isEmpty { continue }
        if text.hasPrefix(".") {
            let body = text.dropFirst().drop { $0 == " " || $0 == "\t" }
            let keyword = String(body.prefix { $0.isLetter })
            if !keyword.isEmpty {
                let argument = body.dropFirst(keyword.count).trimmingCharacters(in: .whitespaces)
                out.append(.directive(keyword: keyword, argument: argument))
                continue
            }
        }
        if let a = assignment(text) { out.append(a) }
    }
    return out
}

private func stripComment(_ line: String) -> String {
    var out = ""
    var previous: Character = " "
    var depth = 0  // inside ${...}, where # is a modifier character
    for c in line {
        if c == "{" && previous == "$" { depth += 1 }
        if c == "}" && depth > 0 { depth -= 1 }
        if c == "#" && depth == 0 && previous != "\\" { break }
        out.append(c)
        previous = c
    }
    return out
}

/// NAME op value, where op is =, +=, ?=, := or != and NAME has no blanks
/// (outside ${...}).
private func assignment(_ text: String) -> Statement? {
    let chars = Array(text)
    var depth = 0
    var i = 0
    while i < chars.count {
        let c = chars[i]
        if c == "$" && i + 1 < chars.count && chars[i + 1] == "{" { depth += 1; i += 2; continue }
        if c == "}" && depth > 0 { depth -= 1; i += 1; continue }
        if depth == 0 {
            if c == ":" && i + 1 < chars.count && chars[i + 1] != "=" { return nil }  // a target
            if c == "=" {
                var start = i
                var op = "="
                if i > 0, "+?:!".contains(chars[i - 1]) {
                    op = String(chars[i - 1]) + "="
                    start = i - 1
                }
                let name = String(chars[..<start]).trimmingCharacters(in: .whitespaces)
                if name.isEmpty || name.contains(where: { $0 == " " || $0 == "\t" }) && !name.contains("${") {
                    return nil
                }
                let value = String(chars[(i + 1)...]).trimmingCharacters(in: .whitespaces)
                return .assign(name: name, op: op, value: value)
            }
        }
        i += 1
    }
    return nil
}

/// Variables, with make's deferred expansion: values are stored as written
/// and expanded when read.
struct Variables {
    var values: [String: String] = [:]

    mutating func apply(_ name: String, _ op: String, _ value: String) {
        let key = expand(name)
        switch op {
        case "+=":
            if let old = values[key], !old.isEmpty { values[key] = old + " " + value } else { values[key] = value }
        case "?=":
            if values[key] == nil { values[key] = value }
        case ":=":
            values[key] = expand(value)
        case "!=":
            values[key] = ""  // a shell command's output: not evaluated
        default:
            values[key] = value
        }
    }

    func expand(_ text: String, depth: Int = 0) -> String {
        if depth > 20 || !text.contains("$") { return text }
        var out = ""
        let chars = Array(text)
        var i = 0
        while i < chars.count {
            if chars[i] == "$", i + 1 < chars.count, chars[i + 1] == "{" || chars[i + 1] == "(" {
                let close: Character = chars[i + 1] == "{" ? "}" : ")"
                var j = i + 2
                var nest = 1
                while j < chars.count {
                    if chars[j] == "$", j + 1 < chars.count, chars[j + 1] == chars[i + 1] { nest += 1; j += 2; continue }
                    if chars[j] == close { nest -= 1; if nest == 0 { break } }
                    j += 1
                }
                let inner = expand(String(chars[(i + 2)..<min(j, chars.count)]), depth: depth + 1)
                out += lookup(inner, depth: depth)
                i = j + 1
                continue
            }
            if chars[i] == "$", i + 1 < chars.count, chars[i + 1] == "$" { out.append("$"); i += 2; continue }
            out.append(chars[i])
            i += 1
        }
        return out
    }

    /// NAME or NAME:modifier:...; supports :T :H :R :E :tl :tu :M and :N
    /// (globs), :S/old/new/; other modifiers leave the value as it is.
    private func lookup(_ expression: String, depth: Int) -> String {
        var parts = expression.split(separator: ":", omittingEmptySubsequences: false).map(String.init)
        let name = parts.removeFirst()
        var value = expand(values[name] ?? "", depth: depth + 1)
        for m in parts {
            let words = value.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
            switch m.first {
            case "T": value = words.map { ($0 as NSString).lastPathComponent }.joined(separator: " ")
            case "H": value = words.map { ($0 as NSString).deletingLastPathComponent }.joined(separator: " ")
            case "R": value = words.map { ($0 as NSString).deletingPathExtension }.joined(separator: " ")
            case "E": value = words.map { ($0 as NSString).pathExtension }.joined(separator: " ")
            case "M": value = words.filter { glob(String(m.dropFirst()), $0) }.joined(separator: " ")
            case "N": value = words.filter { !glob(String(m.dropFirst()), $0) }.joined(separator: " ")
            case "S":
                let f = m.dropFirst(2).split(separator: m.dropFirst().first ?? "/", omittingEmptySubsequences: false)
                if f.count >= 2 {
                    value = words.map { $0.replacingOccurrences(of: String(f[0]), with: String(f[1])) }.joined(separator: " ")
                }
            default:
                if m == "tl" { value = value.lowercased() } else if m == "tu" { value = value.uppercased() }
            }
        }
        return value
    }
}

/// Expands the references to one variable (a .for loop's) in text and
/// leaves every other reference as written.
func substitute(_ text: String, _ name: String, _ value: String) -> String {
    var only = Variables()
    only.values[name] = value
    var out = ""
    let chars = Array(text)
    var i = 0
    while i < chars.count {
        if chars[i] == "$", i + 1 < chars.count, chars[i + 1] == "{" {
            var j = i + 2
            var nest = 1
            while j < chars.count {
                if chars[j] == "{" { nest += 1 }
                if chars[j] == "}" { nest -= 1; if nest == 0 { break } }
                j += 1
            }
            let inner = String(chars[(i + 2)..<min(j, chars.count)])
            let reference = String(chars[i...min(j, chars.count - 1)])
            if inner == name || inner.hasPrefix(name + ":") {
                out += only.expand(reference)
            } else {
                out += "${" + substitute(inner, name, value) + "}"
            }
            i = j + 1
            continue
        }
        out.append(chars[i])
        i += 1
    }
    return out
}

func glob(_ pattern: String, _ text: String) -> Bool {
    fnmatch(pattern, text, 0) == 0
}

func words(_ s: String) -> [String] {
    s.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
}

// MARK: - Conditions

/// A make conditional's text, shortened for the inventory: `${MK_PF} !=
/// "no"` reads `MK_PF`, `${MK_PF} == "no"` reads `!MK_PF`.
func displayCondition(_ expression: String) -> String {
    var s = expression
    for (pattern, replacement) in [
        (#"\$\{(MK_[A-Z0-9_]+)\}\s*!=\s*"?no"?"#, "$1"),
        (#"\$\{(MK_[A-Z0-9_]+)\}\s*==\s*"?yes"?"#, "$1"),
        (#"\$\{(MK_[A-Z0-9_]+)\}\s*==\s*"?no"?"#, "!$1"),
        (#"\$\{(MACHINE(_ARCH|_CPUARCH)?)\}\s*==\s*"?([A-Za-z0-9_]+)"?"#, "arch=$3"),
        (#"\$\{(MACHINE(_ARCH|_CPUARCH)?)\}\s*!=\s*"?([A-Za-z0-9_]+)"?"#, "arch!=$3"),
        (#"\s+"#, " "),
    ] {
        s = s.replacingOccurrences(of: pattern, with: replacement, options: .regularExpression)
    }
    return s.trimmingCharacters(in: .whitespaces)
}

/// Conditions joined with &&, a disjunction in parentheses.
func conjunction(_ conds: [String]) -> String {
    let c = conds.filter { !$0.isEmpty }
    if c.count == 1 { return c[0] }
    return c.map { $0.contains("||") ? "(\($0))" : $0 }.joined(separator: " && ")
}

/// Evaluates a conditional expression with make's semantics (strings,
/// comparisons, !, &&, ||, defined(), empty(); make(), exists() and
/// target() are false).
struct ConditionEvaluator {
    var variables: Variables
    private var chars: [Character] = []
    private var i = 0

    init(_ variables: Variables) { self.variables = variables }

    mutating func evaluate(_ expression: String) -> Bool {
        chars = Array(expression)
        i = 0
        return parseOr()
    }

    private mutating func skipBlanks() { while i < chars.count, chars[i] == " " || chars[i] == "\t" { i += 1 } }

    private mutating func eat(_ s: String) -> Bool {
        skipBlanks()
        let t = Array(s)
        if i + t.count <= chars.count, Array(chars[i..<(i + t.count)]) == t { i += t.count; return true }
        return false
    }

    private mutating func parseOr() -> Bool {
        var v = parseAnd()
        while eat("||") { let r = parseAnd(); v = v || r }
        return v
    }

    private mutating func parseAnd() -> Bool {
        var v = parseUnary()
        while eat("&&") { let r = parseUnary(); v = v && r }
        return v
    }

    private mutating func parseUnary() -> Bool {
        if eat("!") { return !parseUnary() }
        if eat("(") { let v = parseOr(); _ = eat(")"); return v }
        skipBlanks()
        for function in ["defined", "empty", "make", "exists", "target", "commands"] where eat(function + "(") {
            var depth = 1
            var argument = ""
            while i < chars.count {
                if chars[i] == "(" { depth += 1 }
                if chars[i] == ")" { depth -= 1; if depth == 0 { break } }
                argument.append(chars[i]); i += 1
            }
            i += 1
            switch function {
            case "defined": return variables.values[argument] != nil
            case "empty": return variables.expand("${\(argument)}").trimmingCharacters(in: .whitespaces).isEmpty
            default: return false
            }
        }
        let left = operand()
        for op in ["==", "!=", "<=", ">=", "<", ">"] where eat(op) {
            let right = operand()
            if let l = Double(left), let r = Double(right) {
                switch op {
                case "==": return l == r
                case "!=": return l != r
                case "<=": return l <= r
                case ">=": return l >= r
                case "<": return l < r
                default: return l > r
                }
            }
            switch op {
            case "==": return left == right
            case "!=": return left != right
            default: return false
            }
        }
        if let n = Double(left) { return n != 0 }
        return !left.isEmpty
    }

    private mutating func operand() -> String {
        skipBlanks()
        guard i < chars.count else { return "" }
        if chars[i] == "\"" {
            i += 1
            var s = ""
            while i < chars.count, chars[i] != "\"" { s.append(chars[i]); i += 1 }
            i += 1
            return variables.expand(s)
        }
        var s = ""
        var depth = 0
        while i < chars.count {
            let c = chars[i]
            if c == "$", i + 1 < chars.count, chars[i + 1] == "{" { depth += 1; s += "${"; i += 2; continue }
            if c == "}", depth > 0 { depth -= 1; s.append(c); i += 1; continue }
            if depth == 0, " \t!=<>&|()".contains(c) { break }
            s.append(c); i += 1
        }
        return variables.expand(s)
    }
}

/// The .if stack of one Makefile: each open conditional keeps its branch's
/// text (for display) and whether it holds on arm64.
struct ConditionStack {
    private struct Frame {
        var previous: [String] = []  // earlier branches' expressions
        var current: String  // this branch's expression, "" for none (in a .for)
        var holds: Bool
        var anyHeld: Bool
        var isFor = false
    }

    private var frames: [Frame] = []

    /// False inside a .for loop, whose body is skipped.
    var readable: Bool { !frames.contains { $0.isFor } }

    /// The conjunction of the open branches, as displayCondition text.
    var text: String { conjunction(frames.map(\.current)) }

    /// Whether every open branch holds on arm64.
    var holds: Bool { frames.allSatisfy { $0.holds } }

    /// Applies a directive; returns false if it isn't a conditional.
    mutating func apply(_ keyword: String, _ argument: String, _ variables: Variables) -> Bool {
        var evaluator = ConditionEvaluator(variables)
        switch keyword {
        case "if", "ifdef", "ifndef":
            let expression = keyword == "if" ? argument : (keyword == "ifdef" ? "defined(\(argument))" : "!defined(\(argument))")
            let h = evaluator.evaluate(expression)
            frames.append(Frame(current: displayCondition(expression), holds: h, anyHeld: h))
        case "elif", "elifdef", "elifndef":
            guard var f = frames.popLast() else { return true }
            let expression = keyword == "elif" ? argument : (keyword == "elifdef" ? "defined(\(argument))" : "!defined(\(argument))")
            f.previous.append(f.current)
            let h = !f.anyHeld && evaluator.evaluate(expression)
            f.anyHeld = f.anyHeld || h
            f.current = (f.previous.map { "!(\($0))" } + [displayCondition(expression)]).joined(separator: " && ")
            f.holds = h
            frames.append(f)
        case "else":
            guard var f = frames.popLast() else { return true }
            f.previous.append(f.current)
            f.current = f.previous.map { "!(\($0))" }.joined(separator: " && ")
            f.holds = !f.anyHeld
            frames.append(f)
        case "endif", "endfor":
            _ = frames.popLast()
        case "for":
            frames.append(Frame(current: "", holds: false, anyHeld: false, isFor: true))
        default:
            return false
        }
        return true
    }
}

/// The MK_* option values of an arm64 build with no src.conf, from
/// share/mk/bsd.opts.mk and share/mk/src.opts.mk read in that order: their
/// default lists as each include of bsd.mkopt.mk resolves them, the branches
/// that hold for aarch64, BROKEN_OPTIONS, and the MK_ overrides after them.
func aarch64Options(_ files: [String]) -> Variables {
    var v = Variables()
    v.values = [
        "MACHINE": "arm64", "MACHINE_ARCH": "aarch64", "MACHINE_CPUARCH": "aarch64",
        "TARGET_ARCH": "aarch64", "__T": "aarch64", "COMPILER_TYPE": "clang",
    ]
    func resolve(_ v: inout Variables) {
        func list(_ name: String) -> [String] { words(v.expand(v.values[name] ?? "")) }
        for o in list("__DEFAULT_YES_OPTIONS") + list("__REQUIRED_OPTIONS") where v.values["MK_" + o] == nil {
            v.values["MK_" + o] = "yes"
        }
        for o in list("__DEFAULT_NO_OPTIONS") where v.values["MK_" + o] == nil { v.values["MK_" + o] = "no" }
        for pair in list("__DEFAULT_DEPENDENT_OPTIONS") {
            let p = pair.split(separator: "/").map(String.init)
            if p.count == 2, v.values["MK_" + p[0]] == nil { v.values["MK_" + p[0]] = v.values["MK_" + p[1]] ?? "yes" }
        }
        for o in list("BROKEN_OPTIONS") { v.values["MK_" + o] = "no" }
        for name in ["__DEFAULT_YES_OPTIONS", "__DEFAULT_NO_OPTIONS", "__REQUIRED_OPTIONS", "__DEFAULT_DEPENDENT_OPTIONS"] {
            v.values[name] = nil
        }
    }
    func run(_ list: [Statement], _ v: inout Variables, _ stack: inout ConditionStack) {
        var n = 0
        while n < list.count {
            switch list[n] {
            case let .directive("for", argument):
                // The body, once per item, with the loop variable set.
                var depth = 1
                var end = n + 1
                while end < list.count {
                    if case let .directive(k, _) = list[end] {
                        if k == "for" { depth += 1 }
                        if k == "endfor" { depth -= 1; if depth == 0 { break } }
                    }
                    end += 1
                }
                let parts = argument.components(separatedBy: " in ")
                if stack.holds, parts.count == 2 {
                    // make substitutes the loop variable into the body's text.
                    let name = parts[0].trimmingCharacters(in: .whitespaces)
                    for item in words(v.expand(parts[1])) {
                        let body: [Statement] = list[(n + 1)..<end].map {
                            switch $0 {
                            case let .assign(a, op, value):
                                return .assign(name: substitute(a, name, item), op: op, value: substitute(value, name, item))
                            case let .directive(k, argument):
                                return .directive(keyword: k, argument: substitute(argument, name, item))
                            }
                        }
                        run(body, &v, &stack)
                    }
                }
                n = end + 1
                continue
            case let .directive(keyword, argument):
                if stack.apply(keyword, argument, v) { break }
                if keyword == "include", argument.contains("bsd.mkopt.mk"), stack.holds { resolve(&v) }
            case let .assign(name, op, value):
                if stack.holds { v.apply(name, op, value) }
            }
            n += 1
        }
    }
    for text in files {
        var stack = ConditionStack()
        run(statements(text), &v, &stack)
    }
    resolve(&v)
    return v
}
