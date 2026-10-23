# Touch Layouts by Game Screen

**Status:** awaiting user review

## Goal

Automatically change the Android touch controls' layout and appearance as CTR Native moves between menus, racing, and pause/results screens. Players should be able to navigate menus and play races with controls suited to each screen.

## Agreed direction

- Layout selection is automatic from the native game state.
- The initial screen groups are menu, race, and pause/results.
- Touch controls continue to feed the existing virtual controller path.
- The app keeps the current touch visibility policy: automatic when no physical pad is connected, always when configured, or disabled when configured.
- The racing layout uses visible directional arrows on the left instead of an analog stick.
- The newer visual references define the control style: gold/yellow circular buttons with subtle shadows, cream-filled directional buttons in menus, and more translucent controls over the race.

## Current implementation

- `CTRTouchOverlay` is an Android `View` that draws a translucent analog stick and PS1-style face/shoulder buttons. It supports multiple simultaneous pointers.
- The overlay sends an active-low PS1 button mask and stick axes through `CTRNativeActivity.nativeTouchInput`.
- `platform/native_input.c` merges that state into the game's controller snapshots.
- The overlay already converts analog stick movement into D-pad bits, but it uses the same race-oriented layout in every game state.
- `CTRNativeActivity` polls touch visibility every 1.5 seconds. It does not currently poll a screen-specific layout state.
- Native game code already exposes `Ctrds_InRace()` and `Ctrds_OnMainMenu()`. `GameTracker.gameMode1` has `PAUSE_ALL` and `END_OF_RACE` flags.

## Proposed behavior

Expose a small native screen-state value for Android to poll. Resolve it in this order so overlays do not receive gameplay controls while paused:

1. **Pause/results:** `PAUSE_ALL` is set, or `END_OF_RACE` is set.
2. **Race:** `Ctrds_InRace()` is true and the pause flag is clear.
3. **Menu:** all other playable states, including the main menu and menu navigation before a race.

Use one built-in layout per group, following the supplied visual references:

- **Menu:** large up/down arrows on the left in pale cream circles with gold borders and soft shadows; add left/right where the game screen needs them. Put separate back and confirm buttons at the top right, using gold circles with clear white symbols. Confirm sends Cross; back sends Circle. The analog stick is hidden.
- **Race:** use visible left/right steering arrows in separate pale cream circles on the lower left instead of the analog stick. Put pause at the upper left. Keep the driving/action cluster on the right: translucent amber/gold circular buttons for mapped actions, with brake and a larger accelerator near the lower right. Retain the current actions: Cross accelerates, Square brakes, Circle uses the current item, Triangle looks back, and L1/R1 provide the existing hop/slide inputs. Do not assign extra controls from icons in the references unless their native action is confirmed.
- **Pause/results:** use a compact menu layout with pale cream directional arrows and gold confirm/back controls. Keep race steering and racing-only actions hidden. Start remains available where the current screen uses it to resume or advance.

Give the three presets distinct placement and button fill/transparency while using the shared gold/yellow palette. Preserve legible white or gold symbols, subtle shadows, and thumb-sized hit areas. Keep dimensions proportional to the display's short edge so the layout works across aspect ratios.

When the native screen state changes, update the overlay on the Android UI thread and release any held touch inputs before activating the new preset. Keep multi-touch for simultaneous movement and action. If the native state cannot be read, retain the current preset and keep the overlay's existing visibility policy.

## Input contract

- Continue sending the same active-low PS1 pad bits through `nativeTouchInput`; do not add a second game input path.
- D-pad directions use the existing PS1 direction bits.
- In the race preset, left/right arrows steer using those direction bits and analog axes stay centered.
- Menu and pause/results buttons use Cross for confirm, Circle for back, and Start where shown.
- Releasing a pointer, cancelling a gesture, hiding the overlay, or changing layouts must clear its held button/axis state.

## Scope for the first version

This version provides fixed, built-in presets that switch automatically. It does not add a drag-and-drop editor or per-user custom themes. Those can be considered separately after the automatic layouts work on the device.

## Acceptance criteria

- The overlay switches to the menu preset outside active racing, the race preset during racing, and the pause/results preset when either corresponding native flag is active.
- Menu D-pad taps move the highlighted item; Cross activates it; Circle backs out where the game supports that action.
- Race controls retain their current button meanings and steer with the on-screen directional pad.
- Pause/results controls navigate those screens without race-only controls being shown.
- A state transition or interrupted gesture never leaves a button or axis stuck.
- Layout drawing and hit targets stay aligned after resize and across supported orientations/aspect ratios.
- `assembleDebug` succeeds, the APK contains ARM64 native libraries, and the updated APK launches on the Galaxy S24 for on-device review.

## Risks and open review points

- Some game menus may use different confirm/back conventions. Verify Cross and Circle on the main menu and pause/results screens on device.
- The racing references include action symbols whose exact meanings are not guaranteed by their appearance alone. Keep each icon tied to a currently supported gamepad action; do not invent a new item-cycle or drift action without confirming its native mapping.
- Native state can change during transitions. The implementation must avoid a stale layout trapping input and must clear touch state during changes.
- The three fixed presets are an assumption for this first iteration; no editor or saved per-screen customization is included.
