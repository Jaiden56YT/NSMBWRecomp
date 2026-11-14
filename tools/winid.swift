import CoreGraphics
let name = CommandLine.arguments[1]
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in list where (w[kCGWindowOwnerName as String] as? String) == name {
    let b = w[kCGWindowBounds as String] as? [String: Any]
    if let h = b?["Height"] as? Double, h > 100 { print(w[kCGWindowNumber as String]!); break }
}
