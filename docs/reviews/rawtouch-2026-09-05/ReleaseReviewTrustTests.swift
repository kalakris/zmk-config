import Foundation
@testable import RawTouchAppCore
import RawTouchCore
import RawTouchTestSupport
import XCTest

final class ReleaseReviewTrustTests: XCTestCase {
    func testUntrustedAppMustNotEnableClaiming() {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let fake = FakeRawTouchDeviceManager()
        var initialConfig: RawTouchConfiguration?
        let model = AppModel(configPath: root.appendingPathComponent("config.json"),
            lockPath: root.appendingPathComponent("lock"), trustCheck: { false },
            loginItems: FakeLoginItems(), makeManager: { config, _ in initialConfig = config; return fake })
        XCTAssertTrue(model.start())
        XCTAssertFalse(model.axTrusted)
        XCTAssertFalse(initialConfig!.enabled, "service may discover devices, but must not claim until it can post")
        model.shutdownForQuit()
    }
}
