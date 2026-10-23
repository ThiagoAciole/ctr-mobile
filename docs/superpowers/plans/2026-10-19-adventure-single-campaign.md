# Adventure Single Campaign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Goal

Implement one persistent Adventure campaign on Android. With no campaign, collect a name, choose a pilot once, create the campaign, and enter Adventure. With an existing campaign, show Continue and Campaign. Campaign management supports rename and confirmed delete. Never offer pilot swapping.

## Data safety

- Store the campaign in its own versioned file under the native app save root; do not migrate, rewrite, or delete any of the four legacy save slots.
- Save updates atomically so an interrupted write cannot destroy the last valid campaign.
- Reject malformed campaign data without modifying it. Delete only the dedicated campaign file.
- Keep non-Android menu behavior unchanged.

## Implementation

1. **Campaign persistence**
   - [x] Add a native campaign storage module and atomic file replacement helper, following the existing native save-root conventions.
   - [x] Persist the existing `AdvProgress` structure in the dedicated `CTR-ADVENTURE-CAMPAIGN` file with format/version validation.
   - [x] Add a focused native self-test for round trip, rename preserving pilot/progress, invalid/truncated data, failed replacement, delete scope, and unchanged legacy fixture. Execution still needs a native host build.

2. **Adventure routing**
   - [x] Route Android Adventure entry based on campaign existence: no record -> name and one-time pilot selection; record -> Continue/Campaign.
   - [x] Continue restores the saved `AdvProgress` and hub location.
   - [x] Campaign management exposes rename, delete with confirmation, and back; never changes the selected pilot.
   - [x] Save the initial campaign before entering the hub and update the dedicated record at the existing Adventure save point. Preserve legacy New/Load behavior outside Android.

3. **Android touch UI**
   - [x] Add touch states/actions for the new entry and campaign-management screens using the existing overlay and button styling.
   - [x] Reuse the existing name-entry UI for rename, keep all actions reachable by touch, and show storage failures to the player.
   - [x] Keep pilot selection out of the existing-campaign management flow.

4. **Validation**
   - [x] Android debug APK build succeeds for arm64-v8a, armeabi-v7a, and x86.
   - [ ] Run the native campaign self-test when a host build is available.
   - [ ] Verify new campaign -> restart -> Continue, rename, confirmed delete, and corrupted-file handling on Android.
   - [ ] Confirm the four legacy save-slot files remain byte-identical on-device and inspect the final diff for unrelated changes.

## Review focus

Check that absent/existing campaign routes are distinct; pilot and progress survive rename; Continue restores hub progress; delete affects only the campaign file; failed writes retain the previous valid record; legacy save files are untouched.
