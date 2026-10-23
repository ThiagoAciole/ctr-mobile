# Disc Selection Launcher

**Status:** awaiting user review

## Goal

Redesign the Android launcher screen where the player imports the original CTR disc image, matching the supplied blue racing/checkered-flag references while keeping the existing import and game-start flow.

## Agreed direction

- Match the more specific launcher references `screenshot/Frame 31.png` (no disc) and `screenshot/Frame 33.png` (loaded): blue checkerboard background, centered trophy with checkered flags, layered blue panel, angular action buttons, and a separate start button below.
- Show distinct empty and loaded states in the same launcher.
- Place launcher settings behind a gear button so they remain available without crowding the disc panel.
- Keep support for both validated raw BIN and CHD images.

## Current implementation

- `CTRNativeLauncherActivity` builds a vertically scrolling dark launcher in Java.
- The launcher imports a BIN or CHD into app storage, validates it, shows its size, and enables the play button only when valid.
- Settings for display resolution and frame cap are currently shown below the disc panel.
- The app returns to the launcher after the game activity closes.
- No checkerboard, trophy, or launcher-specific artwork assets currently exist in `res`.

## Proposed behavior

### No valid disc loaded

- Show a prominent title equivalent to “CARREGUE O ARQUIVO ORIGINAL DO JOGO”.
- Show brief helper text asking the player to select the game file.
- Primary panel action: **CARREGAR ARQUIVO**; open the existing Android document picker.
- Keep **INICIAR O JOGO** visible but disabled until a supported image has been imported and validated.

### Valid disc loaded

- Change the panel title to **JOGO CARREGADO**.
- Show the selected file's display name and size (including BIN/CHD where useful), rather than the app's internal `ctr-u.*` filename.
- Change the panel action to **ALTERAR ARQUIVO**; use the existing safe import/replace path.
- Enable **INICIAR O JOGO**.

### Importing and errors

- Keep the existing progress reporting while copying large files.
- Keep the current loaded state and play availability safe during a replacement attempt; an invalid replacement must not make a previous valid image appear to have been replaced successfully.
- Present validation/import errors legibly in the themed panel and leave the user able to choose another file.

### Settings

- Add a clearly accessible gear button on the launcher.
- The gear opens a settings view containing the existing resolution and frame-cap controls, with their current cycle/long-press behavior preserved.
- Returning from settings returns to the same disc state.

## Visual and layout direction

- Match the exact blue palette and composition in Frames 31 and 33: broad checker pattern, central layered dark-blue panel, bold playful headings, angular light-blue panel action, and a separate lower start action.
- Keep the trophy/flags centered above and slightly overlapping the main panel without obscuring text or touch targets.
- Scale and place content for the game's landscape-oriented presentation while allowing compact screens to scroll or rearrange without clipping actions.
- Use the loaded-state title in orange/yellow, as shown in Frame 33; use high-contrast text and clear disabled styling for start as shown in Frame 31.
- Place the settings gear discreetly in a top corner, outside the main title, panel, and start action.
- Avoid relying on text baked into a background image, because labels and state need to change and remain localized/accessibile.

## Scope

- This change is limited to the disc launcher and its settings entry point.
- Preserve the current file picker, BIN/CHD validation, app-private import destination, progress behavior, and game launch contract.
- Do not change in-game touch controls as part of this launcher change.

## Acceptance criteria

- Empty state visually follows the first reference and launches the document picker from **CARREGAR ARQUIVO**.
- Loaded state visually follows the second reference, displays the imported filename and size, and allows replacement or game start.
- Invalid files and interrupted imports show useful feedback and do not enable game start without a usable image.
- Gear opens all current settings and returning restores the same launcher state.
- Layout remains legible on the target Android device and does not clip the primary actions.
- `assembleDebug` succeeds and the resulting APK launches on the Galaxy S24 for visual review.

## Review points

- The references say “arquivo original” and show a BIN, while the current launcher also supports CHD. This spec keeps both formats and reports the actual imported format.
- The exact trophy/flag artwork needs a project asset or a small custom-drawn equivalent; there is no existing launcher art in Android resources.
- The current import implementation replaces the old image only after the new file has copied and validated; keep that safety behavior when reflecting a replacement in the UI.
