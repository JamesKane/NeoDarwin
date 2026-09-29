// SPDX-License-Identifier: BSD-2-Clause
//
// Property lists to and from launch_data, the representation launchd's
// launch_msg() interface carries, as launchctl-842's CF2launch_data() and
// its reverse do. Dates, which launchd has no type for, go as strings.

import Launch

func launchData(_ value: Plist) -> launch_data_t? {
    switch value {
    case .dictionary(let entries):
        guard let d = launch_data_alloc(LAUNCH_DATA_DICTIONARY) else { return nil }
        for entry in entries {
            guard let v = launchData(entry.value) else { continue }
            _ = launch_data_dict_insert(d, v, entry.key)
        }
        return d
    case .array(let items):
        guard let a = launch_data_alloc(LAUNCH_DATA_ARRAY) else { return nil }
        var index = 0
        for item in items {
            guard let v = launchData(item) else { continue }
            _ = launch_data_array_set_index(a, v, index)
            index += 1
        }
        return a
    case .string(let s), .date(let s):
        return launch_data_new_string(s)
    case .integer(let i):
        return launch_data_new_integer(i)
    case .real(let r):
        return launch_data_new_real(r)
    case .boolean(let b):
        return launch_data_new_bool(b)
    case .data(let bytes):
        return bytes.withUnsafeBytes { launch_data_new_opaque($0.baseAddress, $0.count) }
    }
}

/// Sends `request`, a dictionary with one key, or a bare string command
/// such as GetJobs, and returns launchd's response. The caller frees it.
func launchMessage(_ request: launch_data_t) -> launch_data_t? {
    let response = launch_msg(request)
    launch_data_free(request)
    return response
}

/// A request: a dictionary mapping `command` to `argument`.
func launchRequest(_ command: String, _ argument: launch_data_t?) -> launch_data_t {
    let request = launch_data_alloc(LAUNCH_DATA_DICTIONARY)!
    guard let argument else { return request }
    _ = launch_data_dict_insert(request, argument, command)
    return request
}

/// The errno a response carries: 0 when launchd answers with success.
func responseErrno(_ response: launch_data_t?) -> Int32 {
    guard let response else { return __error().pointee }
    defer { launch_data_free(response) }
    if launch_data_get_type(response) == LAUNCH_DATA_ERRNO { return launch_data_get_errno(response) }
    return 0
}

func errorString(_ error: Int32) -> String {
    String(cString: strerror(error))
}
