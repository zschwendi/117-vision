# Keyboard, mouse/trackpad and touch (iPad and iPhone)

A controller is not required to start Halo. A hardware keyboard moves; a
mouse, trackpad or the touchscreen aims and clicks. Touch alone aims and
clicks but cannot move. Physical acceptance of all of this is still pending.

## Bindings

Keys and buttons reach Halo as the original PC DirectInput keyboard and mouse,
so Halo's own control settings apply and can rebind them. The defaults below
are the ones the existing controller bridge already emits (`dinput8.c`
`keyboard_base_state`/`host_dinput_mouse_buttons_state`, `d3d9.c` PAD2KEY).

| Input | Halo default |
| --- | --- |
| W A S D | Move |
| Space / Ctrl | Jump / crouch |
| E / R | Action (use) / reload |
| F / Q / G | Melee / flashlight / change grenade |
| Tab / X | Switch weapon / exchange weapon |
| Z | Cycle zoom |
| F1 | Scores |
| Esc, ⌘. or the overlay **Pause** button | Pause / back |
| Return, arrow keys | Menu confirm / navigation |
| Primary (left) button held | Fire, continuing while held |
| Primary click or one-finger tap | One trigger press |
| Secondary click (right button) or two-finger tap | Throw grenade (not zoom) |
| Mouse/trackpad motion, one-finger drag | Look and aim |

Command (⌘) chords stay with iPadOS and never reach Halo, except ⌘. which is
Escape for keyboards without an Esc key (iPadOS can also remap Caps Lock to
Escape in Settings › General › Keyboard › Hardware Keyboard › Modifier Keys).
⌘L releases or recaptures the pointer. The mouse wheel is not mapped.

## Gameplay prompts

With a controller active, verified gameplay prompts name its controls: for
example, Square for use/reload and Triangle for weapon switching on a Sony
controller. An accepted keyboard press, mouse click or meaningful pointer
movement switches back to Halo's key names; deliberate controller input
switches back. Idle tracking and stick drift do not select an input method.
Direct touch uses the keyboard/mouse prompt family. Unknown or ambiguous
controls and the binding editor keep the original text; bindings are unchanged.

## Touch

- One-finger drag: aim (in Halo menus it moves Halo's own cursor).
- One-finger tap: primary click, sent when the finger lifts. In Halo menus it
  clicks at Halo's cursor, not under the finger.
- Two fingers landing together and lifting without moving: one secondary
  click. This is not a double tap. No primary fires when the second finger
  joins, and nothing fires after a drag, a cancellation, a long hold or a
  third finger. There is no touch hold-to-fire and no touch movement.

## Mouse and trackpad

When a mouse or trackpad appears as a GameController mouse, the game asks for
pointer lock and uses raw relative motion while iPadOS actually grants it
(observed from the scene's pointer lock state). Otherwise the visible pointer
aims by its movement and clicks through UIKit. On a Magic Keyboard trackpad,
tap-to-click and the two-finger secondary click are iPadOS settings
(Settings › General › Trackpad, or Trackpad & Mouse): a two-finger trackpad
click is the system's secondary click, not finger counting by the game.

The overlay **Library**/**Settings** buttons stay usable: tap them, or press
⌘L (or the overlay pointer button) to release the pointer and click them. A
click on the game recaptures the pointer without firing. Opening any app
screen releases the lock automatically.

## Ownership and release

Input reaches Halo only while the scene is active (not inactive or in the
background), the full-screen game is presented and visible with no app sheet,
importer or alert over it, and Halo is running (not while it is still
starting). Leaving that state, losing keyboard focus or pointer lock, releasing
the pointer with ⌘L, connecting or disconnecting a keyboard or mouse, or
switching input source releases every held key, button, pending click and
motion. A key or button held across that change must be released and pressed
again; nothing resumes on its own. App text fields and sheet navigation keep
the keyboard. Controller input and its separate app lease are unchanged.

By design, short presses are queued and held down until Halo's 30 Hz
simulation has run once with them, then released before the next queued press,
and a tap is not meant to interrupt a button that is physically held. In Halo's menus (where game time
can stop) each press lasts two input pumps and at least 50 ms instead, a stalled
clock never holds a press longer than 250 ms, and at most four presses queue per
key or button. This behavior is checked with synthetic tests only; whether
every quick tap registers in play still needs hardware acceptance.

## Physical acceptance still pending

1. Magic Keyboard and a Bluetooth keyboard: WASD, Space, Ctrl, E, R, F, Q, G,
   Tab, X, Z, F1, Esc, ⌘. and arrows/Return in menus; Tab/Space/Return never
   activate overlay buttons during play.
2. Mouse: pointer locks in play, aim direction (up looks up) and sensitivity,
   held primary fires continuously, right-click throws one grenade, and
   whether quick single clicks register at 60 and 120 Hz.
3. Magic Keyboard trackpad: with Tap to Click / secondary click enabled and
   disabled; whether it appears as a GameController mouse; lock or fallback.
4. Touch: drag aims; one-finger tap fires once; two-finger tap throws one
   grenade; no shot after drags, cancels or three fingers.
5. ⌘L release/recapture; overlay Library/Settings/Pause by touch and mouse;
   recapture click does not fire.
6. Control Center, app switcher, notification, Settings sheet, Library and
   disconnect while keys/buttons are held: nothing stays pressed or fires on
   return.
7. Keyboard + touch only, keyboard + mouse only, and controller + keyboard
   together; controller and Sense behavior unchanged; frame rate and picture
   unchanged.
