import Foundation
import simd

@main struct SenseGesturesValidation {
    static var checks = 0
    static func require(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(value(), message)
    }
    static func transform(_ position: SIMD3<Float>) -> simd_float4x4 {
        var result = matrix_identity_float4x4; result.columns.3 = SIMD4(position, 1); return result
    }
    struct Run {
        var recognizer = EngineSenseGestures()
        var configuration = EngineSenseGestures.Configuration()
        var frame = EngineSenseGestures.Frame(timestamp: 0,
            source: .init(session: 1, controllerGeneration: 1, inputGeneration: 1,
                left: .init(accessoryID: UUID(), generation: 1),
                right: .init(accessoryID: UUID(), generation: 1), recenter: 0),
            eligible: true, clutch: false, supportGrip: false, blockingButtons: false,
            triggers: .zero, leftStick: .zero, rightStick: .zero,
            originFromHead: matrix_identity_float4x4,
            originFromLeftGrip: transform(SIMD3(-0.25,-0.50,-0.40)),
            originFromRightGrip: transform(SIMD3(0.20,-0.25,-0.45)),
            bodyFromOrigin: matrix_identity_float4x4)
        var actions: [EngineSenseGestures.Action] = []
        var sequences: [UInt64] = []
        var output = EngineSenseGestures.Output(action: nil, sequence: 0, intentActive: false, phase: .idle)

        mutating func step(_ dt: Double = 1.0 / 90) {
            frame.timestamp += dt
            output = recognizer.update(frame, configuration: configuration)
            if let action = output.action { actions.append(action); sequences.append(output.sequence) }
        }
        mutating func begin() { frame.clutch = false; step(); frame.clutch = true; step() }
        mutating func hold(_ seconds: Double = 0.18, fps: Int = 90) {
            let count = max(1, Int(ceil(seconds * Double(fps))))
            for _ in 0..<count { step(seconds / Double(count)) }
        }
        mutating func move(left: SIMD3<Float>? = nil, right: SIMD3<Float>? = nil,
                           head: SIMD3<Float>? = nil, seconds: Double = 0.30, fps: Int = 90) {
            let leftStart = simd_make_float3(frame.originFromLeftGrip.columns.3)
            let rightStart = simd_make_float3(frame.originFromRightGrip.columns.3)
            let headStart = simd_make_float3(frame.originFromHead.columns.3)
            let count = max(1, Int(ceil(seconds * Double(fps))))
            for index in 1...count {
                let a = Float(index) / Float(count)
                if let left { frame.originFromLeftGrip.columns.3 = SIMD4(leftStart + (left-leftStart) * a, 1) }
                if let right { frame.originFromRightGrip.columns.3 = SIMD4(rightStart + (right-rightStart) * a, 1) }
                if let head { frame.originFromHead.columns.3 = SIMD4(headStart + (head-headStart) * a, 1) }
                step(seconds / Double(count))
            }
        }
        var tapPosition: SIMD3<Float> {
            simd_make_float3(frame.originFromRightGrip * SIMD4<Float>(-0.08,-0.04,0,1))
        }
        mutating func tap(fps: Int = 90) {
            move(left: tapPosition, seconds: 0.24, fps: fps); hold(0.07, fps: fps)
        }
        mutating func reload(fps: Int = 90) {
            let withdrawal = simd_make_float3(frame.originFromLeftGrip.columns.3)
            tap(fps: fps); move(left: withdrawal, seconds: 0.22, fps: fps); tap(fps: fps)
        }
        mutating func swap(fps: Int = 90, back: Bool = false) {
            let initial = simd_make_float3(frame.originFromRightGrip.columns.3)
            let g = frame.originFromRightGrip
            let rotation = simd_float3x3(simd_make_float3(g.columns.0), simd_make_float3(g.columns.1), simd_make_float3(g.columns.2))
            // Local offsets from the standard ready pose, transformed with the
            // actual physical world to exercise heading and recline equally.
            let offset: SIMD3<Float> = back ? SIMD3(0.15,-0.30,0.65) : SIMD3(0.15,0.18,0.35)
            move(right: initial + rotation * offset, seconds: 0.40, fps: fps); hold(0.12, fps: fps)
            move(right: initial, seconds: 0.40, fps: fps); hold(fps: fps)
        }
        mutating func pickup(fps: Int = 90) {
            move(left: SIMD3(-0.25,-0.50,-0.70), fps: fps); hold(fps: fps)
        }
    }

    static func naturalGestures() {
        for fps in [30,60,90,120,144] {
            var swap=Run(); swap.hold(0.30,fps:fps); swap.swap(fps:fps)
            require(swap.actions == [.swap], "Automatic shoulder swap requires no Create at \(fps)Hz")
            swap.hold(1.0,fps:fps)
            require(swap.actions == [.swap], "Staying ready does not repeat swap")
            swap.swap(fps:fps)
            require(swap.actions == [.swap,.swap], "Settled return and cooldown permit a fresh reach")
            var back=Run(); back.hold(0.30,fps:fps); back.swap(fps:fps,back:true)
            require(back.actions == [.swap], "Buttonless tracked back reach swaps once")
            for hand in 0..<2 {
                var strike=Run(); strike.hold(0.30,fps:fps)
                if hand==0 { strike.move(left:SIMD3(-0.25,-0.50,-0.78),seconds:0.18,fps:fps) }
                else { strike.move(right:SIMD3(0.20,-0.25,-0.83),seconds:0.18,fps:fps) }
                require(strike.actions == [.melee], "Hand\(hand) forward strike causes one melee at\(fps)Hz")
                strike.hold(0.4,fps:fps)
                require(strike.actions == [.melee], "Held end pose never repeats melee")
            }
            var support=Run();support.frame.originFromLeftGrip=transform(SIMD3(0.1,-0.25,-0.65));support.hold(0.3,fps:fps)
            support.move(left:SIMD3(0.1,-0.25,-1.0),right:SIMD3(0.2,-0.25,-0.80),seconds:0.18,fps:fps)
            require(support.actions == [.melee], "A support hand on the rifle does not block weapon swing")
        }
        for end:SIMD3<Float> in [SIMD3(-0.15,-0.25,-0.45),SIMD3(0.20,-0.60,-0.45)] {
            var swing=Run();swing.hold(0.3);swing.move(right:end,seconds:0.18)
            require(swing.actions == [.melee], "Side/down strike uses original melee")
        }
        var slow=Run();slow.hold(0.3);slow.move(right:SIMD3(0.20,-0.25,-0.85),seconds:1.2);slow.hold()
        require(slow.actions.isEmpty,"Slow aim repositioning is not melee")
        var jitter=Run();jitter.hold(0.3)
        for i in 0..<90 { jitter.frame.originFromRightGrip=transform(SIMD3(0.20+Float(i%2)*0.01,-0.25,-0.45));jitter.step() }
        require(jitter.actions.isEmpty,"Small high-speed jitter cannot meet strike travel")
        var spawn=Run();spawn.move(right:SIMD3(0.20,-0.25,-0.85),seconds:0.18)
        require(spawn.actions.isEmpty,"Motion on entry needs settled front-ready arming")
        var reacquire=Run();reacquire.hold(0.3);_ = reacquire.recognizer.update(nil)
        reacquire.move(right:SIMD3(0.20,-0.25,-0.85),seconds:0.18)
        require(reacquire.actions.isEmpty,"Tracking reacquisition cannot infer a strike")
        var jump=Run();jump.hold(0.3);jump.frame.originFromRightGrip=transform(SIMD3(0.20,-0.25,-0.90));jump.step()
        require(jump.actions.isEmpty,"Tracking teleport is rejected")
        var noReturn=Run();noReturn.hold(0.3);noReturn.move(right:SIMD3(0.35,-0.07,-0.10),seconds:0.4);noReturn.hold(1)
        require(noReturn.actions == [.swap],"Visible shoulder crossing commits before back occlusion")
        _ = noReturn.recognizer.update(nil)
        noReturn.hold(0.2)
        noReturn.move(right:SIMD3(0.2,-0.25,-0.45),seconds:0.4);noReturn.hold()
        require(noReturn.actions == [.swap],"Post-commit tracking loss and return cannot swap twice")
        for blocker in 0..<8 {
            var run=Run();run.hold(0.3)
            switch blocker {
            case 0:run.frame.triggers.y=1
            case 1:run.frame.triggers.x=1
            case 2:run.frame.supportGrip=true
            case 3:run.frame.blockingButtons=true
            case 4:run.frame.leftStick=SIMD2(.nan,1)
            case 5:run.frame.rightStick=SIMD2(1,0)
            case 6:run.frame.eligible=false
            default:run.configuration.enabled=false
            }
            run.move(right:SIMD3(0.2,-0.25,-0.85),seconds:0.18)
            require(run.actions.isEmpty,"Automatic melee respects blocker\(blocker)")
        }
        for changed in 0..<4 {
            var run=Run();run.hold(0.3);run.move(right:SIMD3(0.35,-0.07,-0.24),seconds:0.3);run.hold(0.02)
            switch changed {
            case 0:run.frame.source.inputGeneration+=1
            case 1:run.frame.source.recenter+=1
            case 2:run.step(0.2)
            default:_ = run.recognizer.update(nil)
            }
            run.move(right:SIMD3(0.2,-0.25,-0.45),seconds:0.18);run.hold()
            require(run.actions.isEmpty,"Source/loss\(changed) cancels automatic holster return")
        }
        var rotation=Run();rotation.hold(0.3)
        for i in 1...18 {
            let yaw=Float(i)/18 * Float.pi/2
            let matrix=simd_float4x4(simd_quatf(angle:yaw,axis:SIMD3(0,1,0)))
            rotation.frame.bodyFromOrigin=matrix.inverse
            rotation.frame.originFromHead=matrix
            rotation.frame.originFromLeftGrip=matrix * transform(SIMD3(-0.25,-0.5,-0.4))
            rotation.frame.originFromRightGrip=matrix * transform(SIMD3(0.2,-0.25,-0.45))
            rotation.step(0.02)
        }
        require(rotation.actions.isEmpty,"Turning body with hands carried is not a strike")
        for distance: Float in [0.9, 1.1] {
            for degrees: Float in [-14, -12, 12, 14] {
                var carried=Run()
                let l=SIMD3<Float>(-0.25,-0.3,-distance),r=SIMD3<Float>(0.2,-0.25,-distance)
                carried.frame.originFromLeftGrip=transform(l);carried.frame.originFromRightGrip=transform(r)
                carried.hold(0.3)
                for i in 1...12 {
                    let m=simd_float4x4(simd_quatf(angle:Float(i)/12 * degrees * .pi/180,axis:SIMD3(0,1,0)))
                    carried.frame.bodyFromOrigin=m.inverse;carried.frame.originFromHead=m
                    carried.frame.originFromLeftGrip=m * transform(l);carried.frame.originFromRightGrip=m * transform(r)
                    carried.step(0.01)
                }
                require(carried.actions.isEmpty,"Fast small body turn carrying extended hands is not melee (\(distance)m,\(degrees)deg)")
            }
        }
        var strikeAndLook=Run();strikeAndLook.hold(0.3)
        for i in 1...18 {
            let fraction=Float(i)/18
            let m=simd_float4x4(simd_quatf(angle:fraction * 10 * .pi/180,axis:SIMD3(0,1,0)))
            strikeAndLook.frame.bodyFromOrigin=m.inverse;strikeAndLook.frame.originFromHead=m
            strikeAndLook.frame.originFromLeftGrip=m * transform(SIMD3(-0.25,-0.5,-0.4))
            strikeAndLook.frame.originFromRightGrip=m * transform(SIMD3(0.2,-0.25,-0.45-fraction*0.4))
            strikeAndLook.step(0.01)
        }
        require(strikeAndLook.actions == [.melee],"Real strike survives a small simultaneous physical turn")
        var fixedHandsLook=Run();fixedHandsLook.hold(0.3)
        for i in 1...12 {
            let m=simd_float4x4(simd_quatf(angle:Float(i)/12 * 14 * .pi/180,axis:SIMD3(0,1,0)))
            fixedHandsLook.frame.bodyFromOrigin=m.inverse;fixedHandsLook.frame.originFromHead=m;fixedHandsLook.step(0.01)
        }
        require(fixedHandsLook.actions.isEmpty,"Current physical basis alone cannot manufacture a strike")
        var moving=Run();moving.frame.leftStick=SIMD2(0,1);moving.hold(0.3)
        moving.move(right:SIMD3(0.2,-0.25,-0.85),seconds:0.18)
        require(moving.actions == [.melee],"Stick locomotion does not block physical melee")
        var movingSwap=Run();movingSwap.frame.leftStick=SIMD2(1,0);movingSwap.hold(0.3);movingSwap.swap()
        require(movingSwap.actions == [.swap],"Stick locomotion does not block shoulder swap")
        var scoped=Run();scoped.configuration.reload=false;scoped.configuration.pickup=false;scoped.hold(0.3)
        scoped.move(right:SIMD3(0.2,-0.25,-0.85),seconds:0.18)
        require(scoped.actions == [.melee],"Scoped configuration keeps natural melee")
        var scopedSwap=Run();scopedSwap.configuration.reload=false;scopedSwap.configuration.pickup=false;scopedSwap.hold(0.3);scopedSwap.swap()
        require(scopedSwap.actions == [.swap],"Scoped configuration keeps natural swap")
        var lean=Run();lean.hold(0.3);lean.move(head:SIMD3(0,0,0.3),seconds:0.18)
        require(lean.actions.isEmpty,"Head motion with fixed hands cannot trigger melee")
        var body=Run();body.hold(0.3)
        body.move(left:SIMD3(-0.25,-0.5,-0.75),right:SIMD3(0.2,-0.25,-0.80),head:SIMD3(0,0,-0.35),seconds:0.18)
        require(body.actions.isEmpty,"Whole body motion is not a punch")
    }
    static func main() {
        naturalGestures()
        for fps in [30, 60, 90, 120, 144] {
            var reload = Run(); reload.begin(); reload.reload(fps: fps)
            require(reload.actions == [.reload], "Two separate taps reload once at \(fps)Hz")
            require(reload.output.intentActive && reload.output.phase == .completed, "Completion holds turn suppression until clutch release")
            reload.hold(3, fps: fps)
            require(reload.actions == [.reload], "Held clutch never repeats after completion")
            reload.frame.clutch = false; reload.step()
            require(!reload.output.intentActive, "Releasing Create restores turn control")
            var swap = Run(); swap.begin(); swap.swap(fps: fps)
            require(swap.actions == [.swap], "Shoulder-and-return motion swaps once at \(fps)Hz")
            var back = Run(); back.begin(); back.swap(fps: fps, back: true)
            require(back.actions == [.swap], "Continuously tracked back-and-return swaps once at \(fps)Hz")
            var pickup = Run(); pickup.begin(); pickup.pickup(fps: fps)
            require(pickup.actions == [.pickup], "Forward reach requests pickup once at \(fps)Hz")
        }
        var noClutch = Run(); noClutch.reload(); noClutch.swap(); noClutch.pickup()
        require(noClutch.actions.isEmpty && !noClutch.output.intentActive, "Ordinary physical motions without Create never fire gestures")
        var entryHeld = Run(); entryHeld.frame.clutch = true; entryHeld.step(); entryHeld.reload()
        require(entryHeld.actions.isEmpty, "Create held on entry requires release")
        entryHeld.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40))
        entryHeld.begin(); entryHeld.reload()
        require(entryHeld.actions == [.reload], "Release then deliberate new motion rearms")

        var oneTap = Run(); oneTap.begin(); oneTap.tap(); oneTap.hold(0.5)
        require(oneTap.actions.isEmpty, "One tap or sustained contact cannot reload")
        var upwardLift = Run(); upwardLift.begin()
        upwardLift.move(left: SIMD3(0.18,-0.31,-0.45), seconds: 0.4); upwardLift.hold(0.5)
        require(upwardLift.actions.isEmpty, "Old upward lift alone cannot reload")
        var chestDraw = Run(); chestDraw.begin()
        chestDraw.move(right: SIMD3(0.20,-0.25,-0.20)); chestDraw.hold(0.2)
        chestDraw.move(right: SIMD3(0.20,-0.25,-0.55)); chestDraw.hold()
        require(chestDraw.actions.isEmpty, "Old chest draw no longer swaps")
        var shoulderOnly = Run(); shoulderOnly.begin()
        shoulderOnly.move(right: SIMD3(0.35,-0.07,-0.10), seconds: 0.4); shoulderOnly.hold(0.4)
        require(shoulderOnly.actions.isEmpty, "Shoulder reach without return cannot swap")
        shoulderOnly.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.4); shoulderOnly.hold()
        require(shoulderOnly.actions == [.swap], "Tracked return completes the shoulder swap")
        var shoulderBrush = Run(); shoulderBrush.begin()
        shoulderBrush.move(right: SIMD3(0.35,-0.07,-0.10), seconds: 0.25)
        shoulderBrush.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.25); shoulderBrush.hold()
        require(shoulderBrush.actions.isEmpty, "Brief shoulder brush without dwell cannot swap")
        var visibleShoulder = Run(); visibleShoulder.begin()
        visibleShoulder.move(right: SIMD3(0.30,-0.10,-0.11), seconds: 0.4); visibleShoulder.hold(0.12)
        visibleShoulder.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.4); visibleShoulder.hold()
        require(visibleShoulder.actions == [.swap], "Visible front edge of shoulder works without behind-head tracking")
        var middleBack = Run(); middleBack.begin()
        middleBack.move(right: SIMD3(0,-0.65,0.20), seconds: 0.5); middleBack.hold(0.12)
        middleBack.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.5); middleBack.hold()
        require(middleBack.actions == [.swap], "Tracked reach behind middle of back is supported")
        var initialShoulder = Run()
        initialShoulder.frame.originFromRightGrip = transform(SIMD3(0.30,-0.10,-0.10))
        initialShoulder.begin(); initialShoulder.move(right: SIMD3(0.20,-0.25,-0.45)); initialShoulder.hold()
        require(initialShoulder.actions.isEmpty, "Starting at shoulder then returning cannot count an outward reach")
        var behindFace = Run(); behindFace.begin()
        behindFace.move(right: SIMD3(0,-0.05,0.1), seconds: 0.4); behindFace.hold(0.2)
        behindFace.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.4); behindFace.hold()
        require(behindFace.actions.isEmpty, "Moving beside the face without a shoulder or back holster cannot swap")
        var shortReach = Run(); shortReach.begin()
        shortReach.move(left: SIMD3(-0.25,-0.50,-0.55)); shortReach.hold()
        require(shortReach.actions.isEmpty, "Short reach cannot request pickup")
        var nearGun = Run()
        nearGun.frame.originFromLeftGrip = transform(SIMD3(0.18,-0.31,-0.15))
        nearGun.begin(); nearGun.move(left: SIMD3(0.18,-0.31,-0.45)); nearGun.hold()
        require(nearGun.actions.isEmpty, "A reach into the gun grip is not a pickup")
        var contactEntry = Run()
        contactEntry.frame.originFromLeftGrip = transform(contactEntry.tapPosition)
        contactEntry.begin(); contactEntry.hold(0.2)
        contactEntry.move(left: SIMD3(-0.25,-0.50,-0.40)); contactEntry.tap()
        require(contactEntry.actions.isEmpty, "Starting Create with hands together does not count an initial tap")
        var jitter = Run(); jitter.begin(); jitter.tap()
        for _ in 0..<4 {
            jitter.move(left: SIMD3(0.04,-0.25,-0.45), seconds: 0.08)
            jitter.move(left: jitter.tapPosition, seconds: 0.08); jitter.hold(0.06)
        }
        require(jitter.actions.isEmpty, "Jitter across contact threshold never counts withdrawal")
        var lateTap = Run(); lateTap.begin(); lateTap.tap()
        lateTap.move(left: SIMD3(-0.25,-0.50,-0.40)); lateTap.hold(1.3); lateTap.tap()
        require(lateTap.actions.isEmpty, "Second tap after the sequence timeout cannot reload")
        var movingGun = Run(); movingGun.begin()
        movingGun.move(right: SIMD3(-0.18,-0.45,-0.40)); movingGun.hold()
        movingGun.move(right: SIMD3(0.20,-0.25,-0.45)); movingGun.hold()
        require(movingGun.actions.isEmpty, "Moving gun into a stationary left hand is not a reload")
        var sideTap = Run()
        sideTap.frame.originFromLeftGrip = transform(SIMD3(-0.30,-0.25,-0.45))
        sideTap.begin(); sideTap.reload()
        require(sideTap.actions == [.reload], "Sideways tap works without the old upward stroke")
        var nearMiss = Run(); nearMiss.begin()
        for _ in 0..<2 {
            nearMiss.move(left: SIMD3(0.04,-0.25,-0.45), seconds: 0.22); nearMiss.hold(0.06)
            nearMiss.move(left: SIMD3(-0.25,-0.50,-0.40), seconds: 0.22)
        }
        require(nearMiss.actions.isEmpty, "Two near misses outside14cm cannot reload")
        var tinyWithdrawal = Run(); tinyWithdrawal.begin(); tinyWithdrawal.tap()
        tinyWithdrawal.move(left: SIMD3(0.02,-0.25,-0.45), seconds: 0.10); tinyWithdrawal.tap()
        require(tinyWithdrawal.actions.isEmpty, "Second approach without23cm withdrawal cannot reload")

        for blocker in 0..<9 {
            var run = Run(); run.begin()
            switch blocker {
            case 0: run.frame.triggers.x = 0.8
            case 1: run.frame.triggers.y = 0.8
            case 2: run.frame.supportGrip = true
            case 3: run.frame.blockingButtons = true
            case 4: run.frame.leftStick = SIMD2(0,1)
            case 5: run.frame.rightStick = SIMD2(1,0)
            case 6: run.frame.leftStick = SIMD2(.nan,0)
            case 7: run.frame.triggers.y = .nan
            default: run.frame.eligible = false
            }
            run.reload()
            require(run.actions.isEmpty && !run.output.intentActive, "Shooting/support/buttons/menu blocker \(blocker) wins over gesture intent")
            run.frame.triggers = .zero; run.frame.supportGrip = false; run.frame.blockingButtons = false
            run.frame.leftStick = .zero; run.frame.rightStick = .zero; run.frame.eligible = true
            run.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); run.step(); run.reload()
            require(run.actions.isEmpty, "Removing blocker \(blocker) with held Create does not resume old motion")
            run.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); run.begin(); run.reload()
            require(run.actions == [.reload], "Release and new motion recover from blocker \(blocker)")
        }
        var utility = Run(); utility.begin(); utility.frame.leftStick = SIMD2(1,0); utility.step()
        require(!utility.output.intentActive, "Create+direction remains available to ordinary utility mapping")

        for change in 0..<9 {
            var run = Run(); run.begin()
            run.move(left: SIMD3(-0.10,-0.42,-0.42), seconds: 0.20)
            switch change {
            case 0: run.frame.source.session += 1
            case 1: run.frame.source.controllerGeneration += 1
            case 2: run.frame.source.inputGeneration += 1
            case 3: run.frame.source.left = .init(accessoryID: run.frame.source.left.accessoryID, generation: 2)
            case 4: run.frame.source.right = .init(accessoryID: UUID(), generation: 1)
            case 5: run.frame.source.recenter += 1
            case 6: run.step(0.5)
            case 7: run.frame.timestamp -= 0.5
            default: _ = run.recognizer.update(nil)
            }
            run.reload()
            require(run.actions.isEmpty, "Source/recenter/tracking/clock discontinuity \(change) cancels pending gesture")
            run.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); run.begin(); run.reload()
            require(run.actions == [.reload], "Fresh released attempt recovers after discontinuity \(change)")
        }
        for motion in 0..<2 {
            for loss in 0..<4 {
                var run = Run(); run.begin()
                if motion == 0 {
                    run.tap(); run.move(left: SIMD3(-0.25,-0.50,-0.40), seconds: 0.22)
                } else {
                    run.move(right: SIMD3(0.35,-0.07,0.10), seconds: 0.4); run.hold(0.12)
                }
                switch loss {
                case 0: _ = run.recognizer.update(nil)
                case 1: run.frame.source.controllerGeneration += 1
                case 2: run.frame.eligible = false; run.step(); run.frame.eligible = true
                default: run.step(0.2)
                }
                if motion == 0 { run.tap() }
                else { run.move(right: SIMD3(0.20,-0.25,-0.45), seconds: 0.4); run.hold() }
                require(run.actions.isEmpty, "Loss\(loss) in motion\(motion) cannot infer a tap or shoulder return")
                run.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40))
                run.frame.originFromRightGrip = transform(SIMD3(0.20,-0.25,-0.45))
                run.begin()
                if motion == 0 { run.reload() } else { run.swap() }
                require(run.actions == [motion == 0 ? .reload : .swap], "Fresh released attempt recovers after interrupted motion\(motion)")
            }
        }
        var duplicate = Run(); duplicate.begin(); duplicate.tap()
        duplicate.move(left: SIMD3(-0.25,-0.50,-0.40), seconds: 0.22)
        duplicate.move(left: duplicate.tapPosition, seconds: 0.15)
        for _ in 0..<20 { duplicate.step(0) }
        require(duplicate.actions.isEmpty, "Duplicate drawable times cannot finish the second contact debounce")
        duplicate.hold(0.07)
        require(duplicate.actions == [.reload], "Duplicate drawables preserve valid pending tap contact")
        for _ in 0..<20 { duplicate.step(0) }
        require(duplicate.actions == [.reload], "Duplicate drawables never re-emit the event")

        var timeout = Run(); timeout.begin(); timeout.hold(3.1); timeout.reload()
        require(timeout.actions.isEmpty, "Expired clutch attempt cannot complete after waiting")
        var jump = Run(); jump.begin()
        jump.frame.originFromLeftGrip = transform(SIMD3(0.18,-0.31,-0.45)); jump.step(); jump.hold()
        require(jump.actions.isEmpty, "Unphysical tracking jump cannot complete a gesture")
        var rear = Run(); rear.begin(); rear.move(left: SIMD3(-0.25,-0.50,0.1)); rear.reload()
        require(rear.actions.isEmpty, "Going behind the head cancels and cannot return into an action")
        var invalid = Run(); invalid.begin(); invalid.frame.originFromLeftGrip.columns.0.x = .nan; invalid.step()
        invalid.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); invalid.reload()
        require(invalid.actions.isEmpty, "Invalid rigid pose cannot be rescued into the old gesture")

        // A physical lean with hands fixed must not create apparent hand travel.
        for distance: Float in [0.08, 0.16, 0.24] {
            var lean = Run(); lean.begin()
            lean.move(head: SIMD3(0,0,-distance)); lean.move(head: .zero); lean.hold()
            require(lean.actions.isEmpty, "Head lean in/out \(distance)m cannot create a swap")
            var lift = Run(); lift.begin()
            lift.move(head: SIMD3(0,-distance,0)); lift.move(head: .zero); lift.hold()
            require(lift.actions.isEmpty, "Head duck with fixed hands cannot create reload or pickup")
        }
        var body = Run(); body.begin()
        body.move(left: SIMD3(-0.25,-0.50,-0.65), right: SIMD3(0.20,-0.25,-0.70), head: SIMD3(0,0,-0.25))
        body.hold()
        require(body.actions.isEmpty, "Whole-body translation cancels before satisfying a gesture")
        var looking = Run(); looking.begin()
        for index in 1...90 {
            looking.frame.originFromHead = simd_float4x4(simd_quatf(angle: Float(index) * .pi / 180, axis: SIMD3(0,1,0)))
            looking.step()
        }
        require(looking.actions.isEmpty, "Looking around with fixed hands never creates a gesture")
        looking.reload()
        require(looking.actions == [.reload], "Head rotation does not change the frozen torso gesture coordinates")

        // A reclined/rotated calibrated physical basis must behave exactly as
        // upright. No panorama/virtual-yaw transform enters the recognizer.
        for rotation in [simd_quatf(angle: .pi/3, axis: SIMD3<Float>(1,0,0)),
                         simd_quatf(angle: .pi/2, axis: SIMD3<Float>(0,1,0))] {
            var run = Run()
            let originFromBody = simd_float4x4(rotation)
            run.frame.bodyFromOrigin = simd_inverse(originFromBody)
            run.frame.originFromHead = originFromBody
            run.frame.originFromLeftGrip = originFromBody * run.frame.originFromLeftGrip
            run.frame.originFromRightGrip = originFromBody * run.frame.originFromRightGrip
            run.begin()
            run.reload()
            require(run.actions == [.reload], "Calibrated reclined/yawed torso preserves the same reload motion")
        }
        for heading: Float in [.pi/2, .pi, -.pi/2] {
            for recline: Float in [0, .pi/3] {
                var run = Run()
                let originFromNeutral = simd_float4x4(simd_quatf(angle: recline, axis: SIMD3<Float>(1,0,0)))
                let neutralFromHead = simd_float4x4(simd_quatf(angle: -heading, axis: SIMD3<Float>(0,1,0)))
                let originFromHead = originFromNeutral * neutralFromHead
                // Caller computes heading before virtual turn, then removes
                // that heading from physical positions for this new attempt.
                run.frame.bodyFromOrigin = simd_float4x4(simd_quatf(angle: heading, axis: SIMD3<Float>(0,1,0)))
                    * simd_inverse(originFromNeutral)
                run.frame.originFromHead = originFromHead
                run.frame.originFromLeftGrip = originFromHead * run.frame.originFromLeftGrip
                run.frame.originFromRightGrip = originFromHead * run.frame.originFromRightGrip
                run.begin()
                run.reload()
                require(run.actions == [.reload], "Gesture works after physical heading \(heading) with recline \(recline)")
                for back in [false, true] {
                    var swapped = Run()
                    swapped.frame.bodyFromOrigin = run.frame.bodyFromOrigin
                    swapped.frame.originFromHead = originFromHead
                    swapped.frame.originFromLeftGrip = originFromHead * swapped.frame.originFromLeftGrip
                    swapped.frame.originFromRightGrip = originFromHead * swapped.frame.originFromRightGrip
                    swapped.begin(); swapped.swap(back: back)
                    require(swapped.actions == [.swap], "Shoulder/back\(back) swap at physical heading\(heading), recline\(recline)")
                }
            }
        }
        var frozen = Run(); frozen.begin()
        frozen.frame.bodyFromOrigin = simd_float4x4(simd_quatf(angle: .pi/2, axis: SIMD3(0,1,0)))
        frozen.reload()
        require(frozen.actions == [.reload], "Attempt basis stays frozen; later view rotation cannot reinterpret hand travel")

        for disabled in 0..<4 {
            var run = Run()
            switch disabled {
            case 0: run.configuration.enabled = false
            case 1: run.configuration.reload = false
            case 2: run.configuration.swap = false
            default: run.configuration.pickup = false
            }
            run.begin()
            if disabled < 2 { run.reload() } else if disabled == 2 { run.swap() } else { run.pickup() }
            require(run.actions.isEmpty, "Disabled gesture option \(disabled) never emits")
        }
        var configChange = Run(); configChange.begin()
        configChange.move(left: SIMD3(-0.10,-0.42,-0.42), seconds: 0.20)
        configChange.configuration.pickup = false; configChange.reload()
        require(configChange.actions.isEmpty, "Configuration change requires a new clutch release")
        var repeated = Run(); repeated.begin(); repeated.reload()
        repeated.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); repeated.begin(); repeated.pickup()
        repeated.frame.source.session += 1
        repeated.frame.originFromLeftGrip = transform(SIMD3(-0.25,-0.50,-0.40)); repeated.begin(); repeated.swap()
        require(repeated.actions == [.reload,.pickup,.swap] && repeated.sequences == [1,2,3],
                "Separate deliberate attempts emit globally monotonic one-shot sequences across sessions")
        print("PASS \(checks) Sense quick gesture sequence checks")
    }
}
