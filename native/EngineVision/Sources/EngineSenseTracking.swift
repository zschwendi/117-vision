import ARKit
import Foundation
import GameController
import Spatial
import UIKit
import simd

/// Accessory permission and provider failures must not stop the renderer's
/// world provider. This session uses ARKit's same metric world-reference space.
@available(visionOS 26.0, *)
final class EngineSenseTracking: @unchecked Sendable {
    struct Hand: Sendable {
        let pose: EngineSensePose
        /// Unpredicted physical sample used for release motion. Presentation
        /// prediction and camera movement must not create throw velocity.
        let measuredGripPosition: SIMD3<Float>?
        let measuredTimestamp: TimeInterval
        let trigger: Float
        let thumbstick: SIMD2<Float>
        let source: EngineSenseMenuTrigger.Source
    }
    struct Frame: Sendable {
        var left: Hand?
        var right: Hand?
        /// Paired identity survives a missing pose; it cannot authorize aiming.
        var rightSource: EngineSenseMenuTrigger.Source?
        var controllerGeneration: UInt64 = 0
        var pairCoherent = false
    }
    private let lock = NSLock()
    private var session: ARKitSession?
    private var provider: AccessoryTrackingProvider?
    private var controllers: [UUID: GCController] = [:]
    private var controllerGeneration: UInt64 = 0
    private var rightAccessoryID: UUID?
    private var task: Task<Void, Never>?
    private var eventsTask: Task<Void, Never>?
    private var observers: [NSObjectProtocol] = []
    private var continuation: AsyncStream<Void>.Continuation?
    private var stopped = true
    private var generation: UInt64 = 0
    private var statusSession: UInt64 = 0

    func start() {
        lock.withLock {
            guard stopped else { return }
            stopped = false
            generation &+= 1
            statusSession = EngineSenseTrackingStatus.shared.begin()
            guard AccessoryTrackingProvider.isSupported else {
                EngineSenseTrackingStatus.shared.update(statusSession) {
                    $0.phase = "unsupported"; $0.summary = "Accessory tracking is unavailable on this device."
                }
                return
            }
            let session = ARKitSession()
            self.session = session
            let changes = AsyncStream<Void>(bufferingPolicy: .bufferingNewest(1)) { self.continuation = $0 }
            for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect,
                         UIApplication.didBecomeActiveNotification] {
                observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: nil) { [weak self] _ in
                    self?.requestConfiguration()
                })
            }
            eventsTask = Task { [weak self] in await self?.monitorEvents(session: session) }
            task = Task { [weak self] in
                guard let self else { return }
                await self.configure(session: session)
                for await _ in changes {
                    guard !Task.isCancelled else { break }
                    await self.configure(session: session)
                }
            }
        }
    }

    private func requestConfiguration() {
        let pending = lock.withLock { () -> AsyncStream<Void>.Continuation? in
            guard !stopped else { return nil }
            // Invalidate immediately, including reconnects that reuse the same
            // controller object. A held trigger must be released after recovery.
            generation &+= 1
            controllers.removeAll()
            controllerGeneration = 0
            return continuation
        }
        pending?.yield(())
    }

    func stop() {
        let retired = lock.withLock { () -> (Task<Void, Never>?, Task<Void, Never>?, AsyncStream<Void>.Continuation?, [NSObjectProtocol], UInt64, ARKitSession?) in
            stopped = true; generation &+= 1
            provider = nil; controllers.removeAll(); controllerGeneration = 0
            let old = (task, eventsTask, continuation, observers, statusSession, session)
            task = nil; eventsTask = nil; continuation = nil; observers.removeAll(); session = nil
            return old
        }
        retired.0?.cancel(); retired.1?.cancel(); retired.2?.finish()
        retired.3.forEach { NotificationCenter.default.removeObserver($0) }
        retired.5?.stop()
        EngineSenseTrackingStatus.shared.end(retired.4)
    }

    private func isCurrent(_ token: UInt64, controllers nativeGeneration: UInt64? = nil) -> Bool {
        var pending: AsyncStream<Void>.Continuation?
        let current = lock.withLock {
            guard !stopped, generation == token else { return false }
            if let nativeGeneration, nativeGeneration != hostgc_source_generation() {
                // Notification observers have no ordering guarantee. If native
                // discovery advances after our callback, retry association once
                // rather than waiting forever for another Bluetooth event.
                generation &+= 1; controllers.removeAll(); controllerGeneration = 0
                pending = continuation
                return false
            }
            return true
        }
        pending?.yield(())
        return current
    }
    private func status(_ token: UInt64? = nil, _ update: (inout EngineSenseTrackingStatus.Snapshot) -> Void) {
        let owner = lock.withLock { () -> UInt64? in
            guard !stopped, token == nil || generation == token else { return nil }
            return statusSession
        }
        if let owner { EngineSenseTrackingStatus.shared.update(owner, update) }
    }

    private func sessionIsCurrent(_ candidate: ARKitSession) -> Bool {
        lock.withLock { !stopped && session === candidate }
    }

    private func monitorEvents(session: ARKitSession) async {
        for await event in session.events {
            guard !Task.isCancelled, sessionIsCurrent(session) else { break }
            switch event {
            case .authorizationChanged(let type, let value) where type == .accessoryTracking:
                status {
                    $0.authorization = String(describing: value)
                    if value == .denied {
                        $0.phase = "denied"
                        $0.summary = "Allow Accessory Tracking for Master Cheef in Settings, then return to Halo."
                        $0.left = .init(); $0.right = .init()
                    }
                }
                if value == .allowed || value == .denied { requestConfiguration() }
            case .dataProviderStateChanged(_, let state, let error):
                // Ignore a retired provider's stop event after a new one starts.
                let current = lock.withLock { provider }
                guard current == nil || current?.state == state else { continue }
                status {
                    $0.providerState = String(describing: state)
                    if let error {
                        $0.phase = "failed"; $0.lastError = error.localizedDescription
                        $0.summary = "Sense tracking stopped. Re-enter immersive Halo to retry."
                        $0.left = .init(); $0.right = .init()
                    } else if state == .paused {
                        $0.phase = "paused"
                        $0.summary = "Return to immersive Halo and keep the Sense controllers visible."
                        $0.left = .init(); $0.right = .init()
                    }
                }
            default: break
            }
        }
    }

    private func configure(session: ARKitSession) async {
        let configuration = lock.withLock { () -> (UInt64, UInt64)? in
            guard !stopped, self.session === session else { return nil }
            generation &+= 1; controllers.removeAll(); controllerGeneration = 0
            return (generation, hostgc_source_generation())
        }
        guard let (token, nativeGeneration) = configuration else { return }
        let connected = GCController.controllers().filter { $0.productCategory == GCProductCategorySpatialController }
        status(token) {
            $0.phase = "resolving"; $0.summary = "Finding the left and right Sense controllers…"
            $0.spatialControllers = connected.count; $0.accessories = 0
            $0.left = .init(); $0.right = .init(); $0.lastError = nil
        }
        guard connected.count == 2 else {
            status(token) {
                $0.phase = "waitingForControllers"
                $0.summary = connected.count > 2 ? "Connect only one left/right Sense pair to use tracked aiming." : "Connect both PS VR2 Sense controllers to use tracked aiming."
            }
            return
        }
        var accessories: [Accessory] = []
        var resolved: [UUID: GCController] = [:]
        do {
            for controller in connected {
                let accessory = try await Accessory(device: controller)
                guard isCurrent(token, controllers: nativeGeneration) else { return }
                guard accessory.locations.contains(.aim), accessory.locations.contains(.grip),
                      accessory.inherentChirality != .unspecified else {
                    status(token) {
                        $0.phase = "failed"
                        $0.lastError = "Accessory has no aim/grip location or inherent handedness."
                        $0.summary = "Reconnect both Sense controllers; their aiming locations could not be identified."
                    }
                    return
                }
                accessories.append(accessory); resolved[accessory.id] = controller
            }
            guard accessories.filter({ $0.inherentChirality == .left }).count == 1,
                  accessories.filter({ $0.inherentChirality == .right }).count == 1,
                  resolved.count == 2 else {
                status(token) {
                    $0.phase = "waitingForControllers"
                    $0.summary = "A matching left and right Sense controller are required."
                }
                return
            }
            status(token) { $0.accessories = accessories.count }
            let required = AccessoryTrackingProvider.requiredAuthorizations
            var authorization = await session.queryAuthorization(for: required)
            guard isCurrent(token, controllers: nativeGeneration) else { return }
            if authorization.values.contains(.notDetermined) {
                status(token) {
                    $0.phase = "awaitingPermission"; $0.authorization = "notDetermined"
                    $0.summary = "Allow Accessory Tracking in the headset prompt to aim with Sense."
                }
                authorization = await session.requestAuthorization(for: required)
                guard isCurrent(token, controllers: nativeGeneration) else { return }
            }
            guard required.allSatisfy({ authorization[$0] == .allowed }) else {
                // Retire only this accessory provider. The renderer's world
                // session must survive a denied/revoked accessory permission.
                let current = lock.withLock { () -> Bool in
                    guard !stopped, generation == token, self.session === session else { return false }
                    provider = nil; controllers.removeAll(); controllerGeneration = 0
                    return true
                }
                guard current else { return }
                session.stop()
                status(token) {
                    $0.phase = "denied"; $0.authorization = "denied"
                    $0.providerState = "stopped"
                    $0.summary = "Allow Accessory Tracking for Master Cheef in Settings, then return to Halo."
                }
                return
            }
            status(token) {
                $0.authorization = "allowed"; $0.phase = "starting"
                $0.summary = "Starting Sense position and orientation tracking…"
            }
            let previous = lock.withLock { provider }
            if #available(visionOS 27.0, *), let previous, previous.state != .stopped {
                try await previous.updateAccessories(accessories)
                guard isCurrent(token, controllers: nativeGeneration) else { return }
                lock.withLock {
                    if !stopped && generation == token && nativeGeneration == hostgc_source_generation() {
                        controllers = resolved; controllerGeneration = nativeGeneration
                        rightAccessoryID = accessories.first { $0.inherentChirality == .right }?.id
                    }
                }
            } else {
                let next = AccessoryTrackingProvider(accessories: accessories)
                try await session.run([next])
                guard isCurrent(token, controllers: nativeGeneration) else {
                    if !sessionIsCurrent(session) { session.stop() }
                    return
                }
                lock.withLock {
                    if !stopped && generation == token && nativeGeneration == hostgc_source_generation() {
                        provider = next; controllers = resolved; controllerGeneration = nativeGeneration
                        rightAccessoryID = accessories.first { $0.inherentChirality == .right }?.id
                    }
                }
            }
            guard isCurrent(token, controllers: nativeGeneration) else { return }
            status(token) {
                $0.phase = "waitingForAnchors"; $0.providerState = "running"
                $0.summary = "Keep both Sense controllers in view while tracking starts."
            }
        } catch {
            guard isCurrent(token, controllers: nativeGeneration) else { return }
            lock.withLock { provider = nil; controllers.removeAll(); controllerGeneration = 0 }
            status(token) {
                $0.phase = "failed"; $0.providerState = "stopped"; $0.lastError = error.localizedDescription
                $0.summary = "Sense tracking could not start. Re-enter immersive Halo to retry."
            }
        }
    }

    /// `timestamp` is the compositor's trackableAnchorTime, in ARKit uptime
    /// seconds. Fresh metric aim poses share the head anchor's world space.
    func frame(at timestamp: TimeInterval) -> Frame {
        let snapshot = lock.withLock { (provider, controllers, stopped, generation, controllerGeneration, rightAccessoryID) }
        guard !snapshot.2, let provider = snapshot.0,
              snapshot.1.count == 2, snapshot.4 != 0,
              isCurrent(snapshot.3, controllers: snapshot.4) else { return Frame() }
        guard provider.state == .running else {
            status(snapshot.3) {
                $0.phase = "paused"; $0.providerState = String(describing: provider.state)
                $0.summary = "Return to immersive Halo and keep the Sense controllers visible."
                $0.left = .init(); $0.right = .init()
            }
            return Frame()
        }
        let live = GCController.controllers()
        guard pairIsCurrent(snapshot.1, liveControllers: live) else {
            let ordinary = GCController.current.map {
                live.contains($0) && $0.productCategory != GCProductCategorySpatialController && $0.extendedGamepad != nil
            } ?? false
            status(snapshot.3) {
                $0.phase = ordinary ? "conventionalController" : "waitingForControllers"
                $0.summary = ordinary ? "Press a Sense button to select tracked controls, or continue with your gamepad." : "Reconnect both Sense controllers to restore tracked aiming."
                $0.left = .init(); $0.right = .init()
            }
            return Frame()
        }
        var frame = Frame(controllerGeneration: snapshot.4)
        frame.rightSource = snapshot.5.map { .init(accessoryID: $0, generation: snapshot.3) }
        var left = EngineSenseTrackingStatus.Hand(), right = EngineSenseTrackingStatus.Hand()
        let anchors = provider.latestAnchors
        var predictions: [UUID: AccessoryAnchor] = [:]
        for anchor in anchors where snapshot.1[anchor.accessory.id] != nil {
            if let prediction = provider.predictAnchor(for: anchor, at: timestamp) {
                predictions[anchor.accessory.id] = prediction
            }
        }
        let leftAnchors = anchors.filter { $0.accessory.inherentChirality == .left && snapshot.1[$0.accessory.id] != nil }
        let rightAnchors = anchors.filter { $0.accessory.inherentChirality == .right && snapshot.1[$0.accessory.id] != nil }
        let pairChoice: EngineSensePose.SampleChoice?
        if leftAnchors.count == 1, rightAnchors.count == 1 {
            let left = leftAnchors[0], right = rightAnchors[0]
            func sample(_ anchor: AccessoryAnchor) -> EngineSensePose.Sample {
                .init(quality: Self.quality(anchor.trackingState), timestamp: anchor.timestamp)
            }
            pairChoice = EngineSensePose.selectPair(left: sample(left), right: sample(right),
                predictedLeft: predictions[left.accessory.id].map(sample),
                predictedRight: predictions[right.accessory.id].map(sample), targetTimestamp: timestamp)
        } else { pairChoice = nil }
        for anchor in anchors {
            let side = anchor.accessory.inherentChirality
            guard side != .unspecified else { continue }
            var observation = EngineSenseTrackingStatus.Hand()
            let age = timestamp - anchor.timestamp
            observation.ageMilliseconds = age.isFinite ? age * 1000 : nil
            observation.state = String(describing: anchor.trackingState)
            defer { if side == .left { left = observation } else { right = observation } }
            guard anchors.filter({ $0.accessory.inherentChirality == side }).count == 1 else {
                observation.state = "Ambiguous accessory anchors"; continue
            }
            guard Self.hasPosition(anchor.trackingState) else { continue }
            guard EngineSensePose.isFresh(anchorTimestamp: anchor.timestamp, presentationTimestamp: timestamp) else {
                observation.state = "Stale accessory pose"; continue
            }
            guard let controller = snapshot.1[anchor.accessory.id], live.contains(where: { $0 === controller }) else {
                observation.state = "Accessory/controller identity changed"; continue
            }
            // Prediction can briefly fail at a horizon boundary. A fresh
            // measured 6-DoF pose remains usable; never reuse an old frame.
            let prediction = predictions[anchor.accessory.id]
            let choice = pairChoice ?? EngineSensePose.selectSample(
                measured: Self.quality(anchor.trackingState), measuredTimestamp: anchor.timestamp,
                predicted: prediction.map { Self.quality($0.trackingState) }, predictedTimestamp: prediction?.timestamp,
                targetTimestamp: timestamp)
            guard let choice else { observation.state = "No fresh position and orientation"; continue }
            let sample = choice == .predicted ? (prediction ?? anchor) : anchor
            observation.predicted = choice == .predicted
            observation.lowAccuracy = sample.trackingState == .positionOrientationTrackedLowAccuracy
            observation.state = String(describing: sample.trackingState)
            let aim = sample.coordinateSpace(for: .aim, correction: .none)
            let grip = sample.coordinateSpace(for: .grip, correction: .none)
            guard let transform = try? WorldReferenceCoordinateSpace.worldReference.transform(from: aim),
                  let gripTransform = try? WorldReferenceCoordinateSpace.worldReference.transform(from: grip),
                  let pose = EngineSensePose(originFromAim: transform.matrix, originFromGrip: gripTransform.matrix) else {
                observation.state = "Invalid metric aim or grip transform"; continue
            }
            let input = controller.input.capture()
            guard let trigger = input.buttons[.trigger]?.pressedInput.value,
                  let stick = input.dpads[.thumbstick], trigger.isFinite,
                  stick.xAxis.value.isFinite, stick.yAxis.value.isFinite else {
                observation.state = "Sense input profile unavailable"; continue
            }
            let measuredGrip = anchor.coordinateSpace(for: .grip, correction: .none)
            let measuredTransform = try? WorldReferenceCoordinateSpace.worldReference.transform(from: measuredGrip)
            let measuredPosition = measuredTransform.flatMap {
                EngineImmersiveOrientation.isRigidPose($0.matrix) ? simd_make_float3($0.matrix.columns.3) : nil
            }
            let hand = Hand(pose: pose, measuredGripPosition: measuredPosition,
                            measuredTimestamp: anchor.timestamp, trigger: trigger,
                            thumbstick: SIMD2(stick.xAxis.value, stick.yAxis.value),
                            source: .init(accessoryID: anchor.accessory.id, generation: snapshot.3))
            observation.tracked = true
            if side == .left { frame.left = hand } else { frame.right = hand }
        }
        frame.pairCoherent = pairChoice != nil && frame.left != nil && frame.right != nil
        guard isCurrent(snapshot.3, controllers: snapshot.4), pairIsCurrent(snapshot.1, liveControllers: GCController.controllers()) else { return Frame() }
        status(snapshot.3) {
            $0.framesQueried &+= 1; $0.providerState = "running"; $0.left = left; $0.right = right
            if left.tracked && right.tracked && !frame.pairCoherent {
                $0.phase = "waitingForAnchors"
                $0.summary = "Sense hand samples are out of sync. Keep both controllers visible to resume aiming."
            } else if left.tracked && right.tracked {
                let low = left.lowAccuracy || right.lowAccuracy
                $0.phase = low ? "degraded" : "tracking"
                $0.summary = low ? "Sense aiming is active with reduced accuracy. Improve lighting and keep both controllers visible." : "Both Sense controllers are tracked. Aim by moving your controller."
            } else {
                $0.phase = "waitingForAnchors"
                $0.summary = "Sense tracking needs both controllers in view. Left: \(left.state). Right: \(right.state)."
            }
        }
        return frame
    }

    private static func hasPosition(_ state: AccessoryAnchor.TrackingState) -> Bool {
        state == .positionOrientationTracked || state == .positionOrientationTrackedLowAccuracy
    }

    private static func quality(_ state: AccessoryAnchor.TrackingState) -> EngineSensePose.TrackingQuality {
        switch state {
        case .positionOrientationTracked: .full
        case .positionOrientationTrackedLowAccuracy: .fullLowAccuracy
        case .orientationTracked: .orientationOnly
        case .untracked: .untracked
        @unknown default: .untracked
        }
    }

    func ownsRightAccessory(at timestamp: TimeInterval) -> Bool {
        let snapshot = lock.withLock { (provider, controllers, stopped, generation, controllerGeneration) }
        guard !snapshot.2, let provider = snapshot.0, provider.state == .running,
              snapshot.4 != 0, isCurrent(snapshot.3, controllers: snapshot.4),
              pairIsCurrent(snapshot.1, liveControllers: GCController.controllers()) else { return false }
        let anchors = provider.latestAnchors
        guard anchors.filter({ $0.accessory.inherentChirality == .right }).count == 1 else { return false }
        return isCurrent(snapshot.3, controllers: snapshot.4) && anchors.contains {
            $0.accessory.inherentChirality == .right && Self.hasPosition($0.trackingState)
                && EngineSensePose.isFresh(anchorTimestamp: $0.timestamp, presentationTimestamp: timestamp)
                && snapshot.1[$0.accessory.id] != nil
        }
    }

    private func pairIsCurrent(_ pair: [UUID: GCController], liveControllers: [GCController]) -> Bool {
        guard pair.count == 2 else { return false }
        let spatial = liveControllers.filter { $0.productCategory == GCProductCategorySpatialController }
        guard spatial.count == 2, pair.values.allSatisfy({ member in spatial.contains { $0 === member } }) else { return false }
        if let current = GCController.current, liveControllers.contains(where: { $0 === current }),
           current.productCategory != GCProductCategorySpatialController, current.extendedGamepad != nil { return false }
        return true
    }
}
