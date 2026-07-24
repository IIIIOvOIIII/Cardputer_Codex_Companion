import Foundation

if CommandLine.arguments.count == 2 && CommandLine.arguments[1] == "--version" {
    print("cardputer-phase0-probe 0.1.0")
} else {
    FileHandle.standardError.write(
        Data("usage: cardputer-phase0-probe --version\n".utf8)
    )
    exit(64)
}
