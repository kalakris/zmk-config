import CoreGraphics
@testable import RawTouchCore
import RawTouchTestSupport
import XCTest

final class ReleaseReviewTests: XCTestCase {
    func testDisablingActivePadClosesGestureAndAllowsOtherPad() {
        let capture = CapturedScrollEvents()
        let scheduler = FakeTimerScheduler()
        let ticker = ManualDisplayTicker()
        var now = 0.0
        var config = RawTouchConfiguration()
        config.acceleration.enabled = false
        let caps = RawTouchCapabilities(padsPresent: 3, capabilityBits: 1, rotate90: true, invertY: true)
        let pipeline = TouchScrollPipeline(configuration: config, poster: capture.makePoster(), scheduler: scheduler,
            displayTicker: ticker, now: { now }, pointsPerMM: { 9.5 })
        pipeline.configure(capabilities: caps, systemPrefersNatural: false)
        pipeline.handle(wireFrame: scrollFrame(x: 1000, at: 0))
        ticker.tick(at: 0.001)
        pipeline.handle(wireFrame: scrollFrame(x: 1040, at: 0.010))
        ticker.tick(at: 0.011)
        XCTAssertEqual(pipeline.activeScrollPad, 0)
        config.pads["0"] = .init(enabled: false)
        pipeline.configure(capabilities: caps, configuration: config, systemPrefersNatural: false)
        pipeline.handle(wireFrame: releaseFrame(at: 0.02, scrollMode: true))
        now = 1
        scheduler.fireOneShots()
        XCTAssertNil(pipeline.activeScrollPad, "disabled pad must relinquish the gesture even after its release/watchdog")
        XCTAssertFalse(ticker.isTicking, "disabled pad must not retain a display link")
        pipeline.handle(wireFrame: scrollFrame(x: 1000, at: 1, pad: 1))
        ticker.tick(at: 1.001)
        XCTAssertEqual(pipeline.activeScrollPad, 1, "other pad must be able to scroll")
        pipeline.interrupt()
    }

    func testChangingAxisAtRestDoesNotGenerateMotion() {
        let capture = CapturedScrollEvents()
        let scheduler = FakeTimerScheduler()
        let ticker = ManualDisplayTicker()
        var config = RawTouchConfiguration()
        config.acceleration.enabled = false
        let caps = RawTouchCapabilities(padsPresent: 1, capabilityBits: 1, rotate90: true, invertY: true)
        let pipeline = TouchScrollPipeline(configuration: config, poster: capture.makePoster(), scheduler: scheduler,
            displayTicker: ticker, pointsPerMM: { 9.5 })
        pipeline.configure(capabilities: caps, systemPrefersNatural: false)
        pipeline.handle(wireFrame: scrollFrame(x: 1000, y: 800, at: 0))
        ticker.tick(at: 0.001)
        pipeline.handle(wireFrame: scrollFrame(x: 1040, y: 800, at: 0.010))
        ticker.tick(at: 0.010)
        capture.reset()
        config.pads["0"] = .init(axis: .y)
        pipeline.configure(capabilities: caps, configuration: config, systemPrefersNatural: false)
        ticker.tick(at: 0.02)
        XCTAssertEqual(capture.deltas.reduce(0, +), 0, accuracy: 0.001, "stationary Y must not be differenced against the previous X")
        pipeline.interrupt()
    }
}
