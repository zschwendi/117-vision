# Feature status

117 currently runs **Halo: Combat Evolved retail PC 1.10**. Other Halo games are not implemented.

| Area | Implementation | Acceptance boundary |
| --- | --- | --- |
| Vision Pro | Native Metal renderer, window and immersive presentation | Build120 was played on physical hardware; build121 changes need a new physical test |
| PSVR2 Sense | Tracked aiming, support hand, gestures, turning and optional dual wielding | Test each action and weapon on the headset |
| Grenades | L1 hold/release with hand direction and Halo-scaled strength; 121 adds held model | Original throw animation delay remains |
| Animation takeover | Melee; 121 adds reload and equip/put-away | Physical feel requires testing |
| Launcher/settings | Native touch/controller UI, pause settings and confirmed quit | 121 interface changes require device acceptance |
| iPad/iPhone | Native mobile target; controller or keyboard/mouse, plus touch aiming and click gestures | Movement needs a keyboard or controller; physical input/gameplay acceptance is pending |
| Mobile display | Native 4:3 viewport resolution and display-maximum, 60 or 30 FPS requests | Resolution applies at the next fresh launch; achieved frame rate needs hardware measurement |
| Cloud saves | Immutable snapshots, recovery and explicit conflicts | Sync only while engine is idle in a fresh app process |
| Graphics | Local optional texture/shader packs and graphics settings | Packs retain their own notices; performance depends on settings and hardware |

Source, build, installation and physical gameplay are separate proof levels. See [validation](VALIDATION.md).
