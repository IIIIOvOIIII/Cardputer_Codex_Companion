import XCTest
@testable import CodexAppServer

final class CodexAdapterTests: XCTestCase {
    func testCodexExecutableCanBeConstructed() {
        _ = JSONRPCProcess(codexExecutable: "codex")
    }
}
