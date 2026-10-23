#include <platform/native_input.h>

#include <macros.h>
#include <ctrds.h>

#ifdef __ANDROID__
#include <platform/native_audio.h>
#include <android/log.h>
#define LOG_TAG "CTRNativeInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)
#endif

#include "psx/libpad.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define NATIVE_INPUT_MAX_CONTROLLERS       PLATFORM_INPUT_PAD_COUNT
#define NATIVE_INPUT_PHYSICAL_SLOT_COUNT   2
#define NATIVE_INPUT_PAD_PACKET_BYTES      8
#define NATIVE_INPUT_MULTITAP_HEADER       2
#define NATIVE_INPUT_PAD_DIGITAL           0x41
#define NATIVE_INPUT_PAD_ANALOG            0x73
#define NATIVE_INPUT_PAD_MULTITAP          0x80
#define NATIVE_INPUT_PAD_DISCONNECT        0xff
#define NATIVE_INPUT_AXIS_DEADZONE         500
#define NATIVE_INPUT_MAP_FLAG_AXIS         0x4000
#define NATIVE_INPUT_MAP_FLAG_INVERSE      0x8000
#define NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT 0
// NOTE(aalhendi): Little-endian tag `CTRI` = CTR native Input snapshot.
#define NATIVE_INPUT_STATE_MAGIC           0x49525443
#define NATIVE_INPUT_STATE_VERSION         1

// NOTE(aalhendi): Native input preserves behavior from PsyCross's
// MIT-licensed pad implementation while moving host ownership into ctr-native.
// See THIRD_PARTY_NOTICES.md.

struct NativeInputKeyboardMapping
{
	s32 id;

	s32 kc_square, kc_circle, kc_triangle, kc_cross;

	s32 kc_l1, kc_l2, kc_l3;
	s32 kc_r1, kc_r2, kc_r3;

	s32 kc_start, kc_select;

	s32 kc_dpad_left, kc_dpad_right, kc_dpad_up, kc_dpad_down;
};

struct NativeInputControllerMapping
{
	s32 id;

	s32 gc_square, gc_circle, gc_triangle, gc_cross;

	s32 gc_l1, gc_l2, gc_l3;
	s32 gc_r1, gc_r2, gc_r3;

	s32 gc_start, gc_select;

	s32 gc_dpad_left, gc_dpad_right, gc_dpad_up, gc_dpad_down;

	s32 gc_axis_left_x, gc_axis_left_y;
	s32 gc_axis_right_x, gc_axis_right_y;
};

struct NativeInputController
{
	SDL_JoystickID instanceId;
	SDL_Gamepad *controller;
	s32 analogEnabled;
	s32 switchingAnalog;
	struct PlatformInputPadSnapshot snapshot;
};

struct NativeInputControllerStateSnapshot
{
	struct PlatformInputPadSnapshot snapshot;
	s32 analogEnabled;
	s32 switchingAnalog;
	s32 controllerToSlotMapping;
};

struct NativeInputStateSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	s32 keyboardControllerSlot;
	s32 lastActiveControllerSlot;
	s32 installedSnapshotsActive;
	struct PlatformInputPadSnapshot installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
	struct NativeInputControllerStateSnapshot controllers[NATIVE_INPUT_MAX_CONTROLLERS];
};

global_variable struct NativeInputControllerMapping s_controllerMapping;
#ifdef __ANDROID__
static SDL_SpinLock s_androidMappingLock;
static int s_androidBindings[16]; // Zero keeps the platform default; other values encode binding + 1.

static void NativeInput_ApplyAndroidBindings(struct NativeInputControllerMapping *mapping)
{
	int *fields[] = {&mapping->gc_cross, &mapping->gc_circle, &mapping->gc_square,
		&mapping->gc_triangle, &mapping->gc_l1, &mapping->gc_r1, &mapping->gc_l2,
		&mapping->gc_r2, &mapping->gc_start, &mapping->gc_select, &mapping->gc_l3,
		&mapping->gc_r3, &mapping->gc_dpad_up, &mapping->gc_dpad_down,
		&mapping->gc_dpad_left, &mapping->gc_dpad_right};
	SDL_LockSpinlock(&s_androidMappingLock);
	for (int i = 0; i < 16; i++)
		if (s_androidBindings[i] > 0) *fields[i] = s_androidBindings[i] - 1;
	SDL_UnlockSpinlock(&s_androidMappingLock);
}
#endif
global_variable struct NativeInputKeyboardMapping s_keyboardMapping;
global_variable s32 s_controllerToSlotMapping[NATIVE_INPUT_MAX_CONTROLLERS] = {-1, -1, -1, -1};

global_variable struct NativeInputController s_controllers[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable struct PlatformInputPadSnapshot s_installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable u8 *s_padSlotData[NATIVE_INPUT_PHYSICAL_SLOT_COUNT];
global_variable const bool *s_keyboardState;
global_variable s32 s_inputInitialized;
global_variable s32 s_installedSnapshotsActive;
global_variable u16 s_touchButtons = 0xffff;

// On-screen controls. Active-low like the pad itself: a cleared bit is a press.
// The stick drives both the analog bytes and the d-pad bits, because the d-pad
// path is the one every menu and both steering modes already read.
global_variable int s_touchActive = 0;
global_variable u8 s_touchAnalogLX = 0x80;
global_variable u8 s_touchAnalogLY = 0x80;
global_variable s32 s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
global_variable s32 s_lastActiveControllerSlot = -1;

extern s32 g_padCommEnable;

internal u16 NativeInput_GetSnapshotButtons(const struct PlatformInputPadSnapshot *snapshot)
{
	return (u16)(snapshot->buttons[0] | (snapshot->buttons[1] << 8));
}

internal void NativeInput_SetSnapshotButtons(struct PlatformInputPadSnapshot *snapshot, u16 buttons)
{
	snapshot->buttons[0] = (u8)(buttons & 0xff);
	snapshot->buttons[1] = (u8)(buttons >> 8);
}

internal void NativeInput_ResetSnapshot(s32 slot)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;

	snapshot->connected = slot == 0;
	snapshot->status = snapshot->connected ? 0 : NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = snapshot->connected ? NATIVE_INPUT_PAD_DIGITAL : NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal s32 NativeInput_IsValidControllerSlot(s32 slot)
{
	return (slot >= 0) && (slot < NATIVE_INPUT_MAX_CONTROLLERS);
}

internal s32 NativeInput_NextControllerSlot(s32 slot)
{
	slot++;
	if (slot >= NATIVE_INPUT_MAX_CONTROLLERS)
	{
		slot = 0;
	}

	return slot;
}

internal void NativeInput_MoveKeyboardOffControllerSlot(s32 slot)
{
	if (s_keyboardControllerSlot == slot)
	{
		s_keyboardControllerSlot = NativeInput_NextControllerSlot(s_keyboardControllerSlot);
	}
}

internal void NativeInput_MakeDisconnectedSnapshot(struct PlatformInputPadSnapshot *snapshot)
{
	if (snapshot == NULL)
	{
		return;
	}

	snapshot->connected = 0;
	snapshot->status = NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal void NativeInput_WritePadPacket(u8 *dst, const struct PlatformInputPadSnapshot *snapshot)
{
	if ((dst == NULL) || (snapshot == NULL))
	{
		return;
	}

	dst[0] = snapshot->status;
	dst[1] = snapshot->id;
	dst[2] = snapshot->buttons[0];
	dst[3] = snapshot->buttons[1];
	dst[4] = snapshot->analog[0];
	dst[5] = snapshot->analog[1];
	dst[6] = snapshot->analog[2];
	dst[7] = snapshot->analog[3];
}

internal s32 NativeInput_UseMultitapBus(void)
{
	s32 slot;

	for (slot = NATIVE_INPUT_PHYSICAL_SLOT_COUNT; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].snapshot.connected != 0)
		{
			return 1;
		}
	}

	return 0;
}

internal void NativeInput_WritePadBus(void)
{
	u8 *slot0 = s_padSlotData[0];
	u8 *slot1 = s_padSlotData[1];
	s32 useMultitap = NativeInput_UseMultitapBus();
	s32 slot;

	if (slot0 != NULL)
	{
		if (useMultitap != 0)
		{
			slot0[0] = 0;
			slot0[1] = NATIVE_INPUT_PAD_MULTITAP;
			for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
			{
				NativeInput_WritePadPacket(&slot0[NATIVE_INPUT_MULTITAP_HEADER + (slot * NATIVE_INPUT_PAD_PACKET_BYTES)], &s_controllers[slot].snapshot);
			}
		}
		else
		{
			NativeInput_WritePadPacket(slot0, &s_controllers[0].snapshot);
		}
	}

	if (slot1 != NULL)
	{
		if (useMultitap != 0)
		{
			struct PlatformInputPadSnapshot disconnected;

			NativeInput_MakeDisconnectedSnapshot(&disconnected);
			NativeInput_WritePadPacket(slot1, &disconnected);
		}
		else
		{
			NativeInput_WritePadPacket(slot1, &s_controllers[1].snapshot);
		}
	}
}

internal void NativeInput_WriteInstalledSnapshots(void)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].snapshot = s_installedSnapshots[slot];
	}

	NativeInput_WritePadBus();
}

internal void NativeInput_DefaultMappings(void)
{
	s_keyboardMapping.kc_square = SDL_SCANCODE_X;
	s_keyboardMapping.kc_circle = SDL_SCANCODE_V;
	s_keyboardMapping.kc_triangle = SDL_SCANCODE_Z;
	s_keyboardMapping.kc_cross = SDL_SCANCODE_C;

	s_keyboardMapping.kc_l1 = SDL_SCANCODE_LSHIFT;
	s_keyboardMapping.kc_l2 = SDL_SCANCODE_LCTRL;
	s_keyboardMapping.kc_l3 = SDL_SCANCODE_LEFTBRACKET;

	s_keyboardMapping.kc_r1 = SDL_SCANCODE_RSHIFT;
	s_keyboardMapping.kc_r2 = SDL_SCANCODE_RCTRL;
	s_keyboardMapping.kc_r3 = SDL_SCANCODE_RIGHTBRACKET;

	s_keyboardMapping.kc_dpad_up = SDL_SCANCODE_UP;
	s_keyboardMapping.kc_dpad_down = SDL_SCANCODE_DOWN;
	s_keyboardMapping.kc_dpad_left = SDL_SCANCODE_LEFT;
	s_keyboardMapping.kc_dpad_right = SDL_SCANCODE_RIGHT;

	s_keyboardMapping.kc_select = SDL_SCANCODE_SPACE;
	s_keyboardMapping.kc_start = SDL_SCANCODE_RETURN;

	if (Ctrds_SwapFaceButtons())
	{
		// A/B and X/Y swapped, which is what the Thor's labelling expects.
		s_controllerMapping.gc_square = SDL_GAMEPAD_BUTTON_NORTH;
		s_controllerMapping.gc_circle = SDL_GAMEPAD_BUTTON_SOUTH;
		s_controllerMapping.gc_triangle = SDL_GAMEPAD_BUTTON_WEST;
		s_controllerMapping.gc_cross = SDL_GAMEPAD_BUTTON_EAST;
	}
	else
	{
		s_controllerMapping.gc_square = SDL_GAMEPAD_BUTTON_WEST;
		s_controllerMapping.gc_circle = SDL_GAMEPAD_BUTTON_EAST;
		s_controllerMapping.gc_triangle = SDL_GAMEPAD_BUTTON_NORTH;
		s_controllerMapping.gc_cross = SDL_GAMEPAD_BUTTON_SOUTH;
	}

	s_controllerMapping.gc_l1 = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	s_controllerMapping.gc_l2 = SDL_GAMEPAD_AXIS_LEFT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_l3 = SDL_GAMEPAD_BUTTON_LEFT_STICK;

	s_controllerMapping.gc_r1 = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	s_controllerMapping.gc_r2 = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_r3 = SDL_GAMEPAD_BUTTON_RIGHT_STICK;

	s_controllerMapping.gc_dpad_up = SDL_GAMEPAD_BUTTON_DPAD_UP;
	s_controllerMapping.gc_dpad_down = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	s_controllerMapping.gc_dpad_left = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	s_controllerMapping.gc_dpad_right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

	s_controllerMapping.gc_select = SDL_GAMEPAD_BUTTON_BACK;
	s_controllerMapping.gc_start = SDL_GAMEPAD_BUTTON_START;

	s_controllerMapping.gc_axis_left_x = SDL_GAMEPAD_AXIS_LEFTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_left_y = SDL_GAMEPAD_AXIS_LEFTY | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_right_x = SDL_GAMEPAD_AXIS_RIGHTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_right_y = SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_INPUT_MAP_FLAG_AXIS;
}

internal s32 NativeInput_ControllerButtonState(SDL_Gamepad *controller, s32 buttonOrAxis)
{
	if (controller == NULL)
	{
		return 0;
	}

	if ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
	{
		s32 axis = buttonOrAxis & ~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);
		s32 value = SDL_GetGamepadAxis(controller, (SDL_GamepadAxis)axis);

		if ((abs(value) > NATIVE_INPUT_AXIS_DEADZONE) && ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_INVERSE) != 0))
		{
			value *= -1;
		}

		return value;
	}

	if (buttonOrAxis < 0)
	{
		return 0;
	}

	return SDL_GetGamepadButton(controller, (SDL_GamepadButton)buttonOrAxis) * 32767;
}

internal u8 NativeInput_AxisToByte(s32 axis)
{
	s32 value = (axis / 256) + 128;

	if (value < 0)
	{
		return 0;
	}

	if (value > 0xff)
	{
		return 0xff;
	}

	return (u8)value;
}

internal s32 NativeInput_AxisIsActive(s32 axis)
{
	return abs(axis) > NATIVE_INPUT_AXIS_DEADZONE;
}

internal void NativeInput_ApplyController(s32 slot)
{
	struct NativeInputController *nativeController = &s_controllers[slot];
	struct PlatformInputPadSnapshot *snapshot = &nativeController->snapshot;
	struct NativeInputControllerMapping localMapping = s_controllerMapping;
#ifdef __ANDROID__
	NativeInput_ApplyAndroidBindings(&localMapping);
#endif
	const struct NativeInputControllerMapping *mapping = &localMapping;
	SDL_Gamepad *controller = nativeController->controller;
	u16 buttons = 0xffff;
	s32 rightX;
	s32 rightY;
	s32 leftX;
	s32 leftY;

	if ((controller == NULL) || (SDL_GamepadConnected(controller) == 0))
	{
		return;
	}

	snapshot->connected = 1;
	snapshot->status = 0;
	snapshot->id = nativeController->analogEnabled ? NATIVE_INPUT_PAD_ANALOG : NATIVE_INPUT_PAD_DIGITAL;

	if (NativeInput_ControllerButtonState(controller, mapping->gc_square) > 16384)
	{
		buttons &= ~0x8000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_circle) > 16384)
	{
		buttons &= ~0x2000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_triangle) > 16384)
	{
		buttons &= ~0x1000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_cross) > 16384)
	{
		buttons &= ~0x4000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l1) > 16384)
	{
		buttons &= ~0x400;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r1) > 16384)
	{
		buttons &= ~0x800;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l2) > 16384)
	{
		buttons &= ~0x100;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r2) > 16384)
	{
		buttons &= ~0x200;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_up) > 16384)
	{
		buttons &= ~0x10;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_down) > 16384)
	{
		buttons &= ~0x40;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_left) > 16384)
	{
		buttons &= ~0x80;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_right) > 16384)
	{
		buttons &= ~0x20;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l3) > 16384)
	{
		buttons &= ~0x2;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r3) > 16384)
	{
		buttons &= ~0x4;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_select) > 16384)
	{
		buttons &= ~0x1;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_start) > 16384)
	{
		buttons &= ~0x8;
	}

	rightX = NativeInput_ControllerButtonState(controller, mapping->gc_axis_right_x);
	rightY = NativeInput_ControllerButtonState(controller, mapping->gc_axis_right_y);
	leftX = NativeInput_ControllerButtonState(controller, mapping->gc_axis_left_x);
	leftY = NativeInput_ControllerButtonState(controller, mapping->gc_axis_left_y);

#ifdef __ANDROID__
	int screen = Ctrds_TouchScreenState();
	if (screen != 1 && screen != 18)
	{
		if (leftX < -16384) buttons &= ~0x80;
		if (leftX > 16384) buttons &= ~0x20;
		if (leftY < -16384) buttons &= ~0x10;
		if (leftY > 16384) buttons &= ~0x40;
	}
#endif

	if ((buttons != 0xffff) || NativeInput_AxisIsActive(rightX) || NativeInput_AxisIsActive(rightY) || NativeInput_AxisIsActive(leftX) ||
	    NativeInput_AxisIsActive(leftY))
	{
		s_lastActiveControllerSlot = slot;
	}

	if (((buttons & 0x1) == 0) && ((buttons & 0x8) == 0))
	{
		buttons = 0xffff;
		if (nativeController->switchingAnalog == 0)
		{
			nativeController->analogEnabled = nativeController->analogEnabled == 0;
		}
		nativeController->switchingAnalog = 1;
	}
	else
	{
		nativeController->switchingAnalog = 0;
	}

	NativeInput_SetSnapshotButtons(snapshot, buttons);
	snapshot->analog[0] = NativeInput_AxisToByte(rightX);
	snapshot->analog[1] = NativeInput_AxisToByte(rightY);
	snapshot->analog[2] = NativeInput_AxisToByte(leftX);
	snapshot->analog[3] = NativeInput_AxisToByte(leftY);
}

internal u16 NativeInput_ReadKeyboard(void)
{
	const struct NativeInputKeyboardMapping *mapping = &s_keyboardMapping;
	u16 buttons = 0xffff;

	if (s_keyboardState == NULL)
	{
		return buttons;
	}

	if (s_keyboardState[mapping->kc_square])
	{
		buttons &= ~0x8000;
	}
	if (s_keyboardState[mapping->kc_circle])
	{
		buttons &= ~0x2000;
	}
	if (s_keyboardState[mapping->kc_triangle])
	{
		buttons &= ~0x1000;
	}
	if (s_keyboardState[mapping->kc_cross])
	{
		buttons &= ~0x4000;
	}
	if (s_keyboardState[mapping->kc_l1])
	{
		buttons &= ~0x400;
	}
	if (s_keyboardState[mapping->kc_l2])
	{
		buttons &= ~0x100;
	}
	if (s_keyboardState[mapping->kc_l3])
	{
		buttons &= ~0x2;
	}
	if (s_keyboardState[mapping->kc_r1])
	{
		buttons &= ~0x800;
	}
	if (s_keyboardState[mapping->kc_r2])
	{
		buttons &= ~0x200;
	}
	if (s_keyboardState[mapping->kc_r3])
	{
		buttons &= ~0x4;
	}
	if (s_keyboardState[mapping->kc_dpad_up])
	{
		buttons &= ~0x10;
	}
	if (s_keyboardState[mapping->kc_dpad_down])
	{
		buttons &= ~0x40;
	}
	if (s_keyboardState[mapping->kc_dpad_left])
	{
		buttons &= ~0x80;
	}
	if (s_keyboardState[mapping->kc_dpad_right])
	{
		buttons &= ~0x20;
	}
	if (s_keyboardState[mapping->kc_select])
	{
		buttons &= ~0x1;
	}
	if (s_keyboardState[mapping->kc_start])
	{
		buttons &= ~0x8;
	}

	return buttons;
}

internal s32 NativeInput_KeyboardSuppressed(void)
{
	if (s_keyboardState == NULL)
	{
		return 0;
	}

	return s_keyboardState[SDL_SCANCODE_RALT] || s_keyboardState[SDL_SCANCODE_LALT];
}

internal void NativeInput_ApplyKeyboard(s32 slot, u16 keyboardButtons)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;
	u16 buttons;

	if (slot != s_keyboardControllerSlot)
	{
		return;
	}

	if (snapshot->connected == 0)
	{
		snapshot->connected = 1;
		snapshot->status = 0;
		snapshot->id = NATIVE_INPUT_PAD_DIGITAL;
	}

	buttons = NativeInput_GetSnapshotButtons(snapshot);
	NativeInput_SetSnapshotButtons(snapshot, buttons & keyboardButtons);
}

internal s32 NativeInput_FindActiveControllerSlot(void)
{
	s32 slot;

	if (NativeInput_IsValidControllerSlot(s_lastActiveControllerSlot) && (s_controllers[s_lastActiveControllerSlot].controller != NULL))
	{
		return s_lastActiveControllerSlot;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL)
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_SwapControllerSlots(s32 slotA, s32 slotB)
{
	struct NativeInputController controller;
	s32 mapping;

	if (!NativeInput_IsValidControllerSlot(slotA) || !NativeInput_IsValidControllerSlot(slotB) || (slotA == slotB))
	{
		return;
	}

	controller = s_controllers[slotA];
	s_controllers[slotA] = s_controllers[slotB];
	s_controllers[slotB] = controller;

	mapping = s_controllerToSlotMapping[slotA];
	s_controllerToSlotMapping[slotA] = s_controllerToSlotMapping[slotB];
	s_controllerToSlotMapping[slotB] = mapping;

	if (s_lastActiveControllerSlot == slotA)
	{
		s_lastActiveControllerSlot = slotB;
	}
	else if (s_lastActiveControllerSlot == slotB)
	{
		s_lastActiveControllerSlot = slotA;
	}
}

internal s32 NativeInput_FindSlotForDeviceIndex(Sint32 deviceIndex)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllerToSlotMapping[slot] == deviceIndex)
		{
			return slot;
		}
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((s_controllerToSlotMapping[slot] < 0) && (s_controllers[slot].controller == NULL))
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_CloseController(s32 slot)
{
	struct NativeInputController *controller;

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		SDL_CloseGamepad(controller->controller);
	}

	controller->controller = NULL;
	controller->instanceId = -1;
	controller->analogEnabled = 0;
	controller->switchingAnalog = 0;

	if (s_lastActiveControllerSlot == slot)
	{
		s_lastActiveControllerSlot = -1;
	}
}

internal void NativeInput_OpenController(SDL_JoystickID instanceId, s32 slot)
{
	struct NativeInputController *controller;
	SDL_Joystick *joystick;

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	if (SDL_IsGamepad(instanceId) == 0)
	{
		return;
	}

	controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		return;
	}

	controller->controller = SDL_OpenGamepad(instanceId);
	if (controller->controller == NULL)
	{
		return;
	}

	joystick = SDL_GetGamepadJoystick(controller->controller);
	controller->instanceId = joystick != NULL ? SDL_GetJoystickID(joystick) : instanceId;
	controller->analogEnabled = 1;
	controller->switchingAnalog = 0;
	NativeInput_MoveKeyboardOffControllerSlot(slot);
}

internal void NativeInput_OpenKnownControllers(void)
{
	SDL_JoystickID *gamepads;
	s32 count = 0;
	s32 i;

	gamepads = SDL_GetGamepads(&count);
	for (i = 0; i < count; i++)
	{
		s32 slot = NativeInput_FindSlotForDeviceIndex(gamepads[i]);

		if (slot >= 0)
		{
			NativeInput_OpenController(gamepads[i], slot);
		}
	}
	SDL_free(gamepads);
}

#include <SDL3/SDL_system.h>

#if defined(__ANDROID__)
// NOTE(ctrds): SDL_system.h declares SDL_GetAndroidJNIEnv but does not pull in
// the JNI types these helpers use, so the Android build did not compile.
#include <jni.h>

internal void NativeInput_AndroidVibrate(int device_id, float low, float high, int len)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) {
        LOGI("NativeInput_AndroidVibrate: JNIEnv is NULL");
        return;
    }

    jclass cls = (*env)->FindClass(env, "org/libsdl/app/SDLControllerManager");
    if (!cls) {
        LOGI("NativeInput_AndroidVibrate: Could not find SDLControllerManager class");
        (*env)->ExceptionClear(env);
        return;
    }

    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "hapticRumble", "(IFFI)V");
    if (mid) {
        LOGI("NativeInput_AndroidVibrate: Calling hapticRumble via JNI");
        (*env)->CallStaticVoidMethod(env, cls, mid, device_id, low, high, len);
    } else {
        LOGI("NativeInput_AndroidVibrate: Could not find hapticRumble method");
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, cls);
}

internal void NativeInput_AndroidPollHaptics(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) {
        LOGI("NativeInput_AndroidPollHaptics: JNIEnv is NULL");
        return;
    }

    jclass cls = (*env)->FindClass(env, "org/libsdl/app/SDLControllerManager");
    if (!cls) {
        LOGI("NativeInput_AndroidPollHaptics: Could not find SDLControllerManager class");
        (*env)->ExceptionClear(env);
        return;
    }

    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "pollHapticDevices", "()V");
    if (mid) {
        LOGI("NativeInput_AndroidPollHaptics: Calling pollHapticDevices via JNI");
        (*env)->CallStaticVoidMethod(env, cls, mid);
    } else {
        LOGI("NativeInput_AndroidPollHaptics: Could not find pollHapticDevices method");
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, cls);
}

#endif // __ANDROID__

int Platform_InputInit(void)
{
	s32 slot;

	if (s_inputInitialized != 0)
	{
		return 1;
	}

	memset(s_controllers, 0, sizeof(s_controllers));
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].instanceId = -1;
		NativeInput_ResetSnapshot(slot);
		s_installedSnapshots[slot] = s_controllers[slot].snapshot;
	}

	NativeInput_DefaultMappings();
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	s_lastActiveControllerSlot = -1;
	s_installedSnapshotsActive = 0;
	s_keyboardState = SDL_GetKeyboardState(NULL);

	if (SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC) == 0)
	{
		fprintf(stderr, "[CTR Native] Failed to initialise SDL input subsystem: %s\n", SDL_GetError());
		return 0;
	}

#if defined(__ANDROID__)
    // Force SDL to discover the Android system vibrator
    NativeInput_AndroidPollHaptics();
#endif

	SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
	NativeInput_OpenKnownControllers();

	s_inputInitialized = 1;
	return 1;
}

void Platform_InputShutdown(void)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_CloseController(slot);
	}

	if (s_inputInitialized != 0)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
	}

	s_inputInitialized = 0;
	s_installedSnapshotsActive = 0;
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	s_lastActiveControllerSlot = -1;
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	s_keyboardState = NULL;
}

void Platform_InputUpdate(void)
{
	u16 keyboardButtons;
	s32 slot;

	if (s_inputInitialized == 0)
	{
		return;
	}

	if (s_installedSnapshotsActive != 0)
	{
		// NOTE(aalhendi): replay/state installs PSX-shaped pad bytes here;
		// SDL host state is not serialized.
		NativeInput_WriteInstalledSnapshots();
		return;
	}

	if (g_padCommEnable == 0)
	{
		return;
	}

	SDL_PumpEvents();
	keyboardButtons = NativeInput_KeyboardSuppressed() ? 0xffff : NativeInput_ReadKeyboard();

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_ResetSnapshot(slot);
		NativeInput_ApplyController(slot);
		NativeInput_ApplyKeyboard(slot, keyboardButtons);
		// Touch drives whichever slot the game is actually reading. This device
		// enumerates two pads, so pinning the overlay to slot 0 left the presses
		// going to a pad nothing looks at.
		if ((slot == 0) || s_touchActive) {
			u16 buttons = NativeInput_GetSnapshotButtons(&s_controllers[slot].snapshot);
			NativeInput_SetSnapshotButtons(&s_controllers[slot].snapshot, buttons & s_touchButtons);

			if (s_touchActive)
			{
				// analog[2]/[3] are the left stick; see NativeInput_ApplyController.
				if (!s_controllers[slot].controller || s_touchAnalogLX != 0x80)
					s_controllers[slot].snapshot.analog[2] = s_touchAnalogLX;
				if (!s_controllers[slot].controller || s_touchAnalogLY != 0x80)
					s_controllers[slot].snapshot.analog[3] = s_touchAnalogLY;

				// The overlay is one virtual pad in port 1, and only that slot may
				// be advertised as connected. Marking them all connected -- which
				// is what this did -- put a slot past the physical ports on the
				// bus, NativeInput_UseMultitapBus saw it and switched the bus to
				// multitap, and the game read a multitap header where it expected
				// a pad. The mask arrived correctly and nothing responded to it.
				//
				// The mask still goes to every slot above: that part was needed
				// because a device with real pads may not read slot zero. Only
				// the claim to be connected is restricted.
				if (slot == 0 && !s_controllers[slot].controller)
				{
					s_controllers[slot].snapshot.connected = 1;

					// Zero, not PadStateStable. This byte goes straight into the
					// pad packet, where the game reads it as the transfer status
					// and expects zero for a pad that is present -- which is what
					// ResetSnapshot and every other path write. PadStateStable is
					// a PadGetState() return value, a different API entirely, and
					// putting its 6 here made the game read a pad it would not
					// take input from.
					s_controllers[slot].snapshot.status = 0;
					// The game reads stickLY only when the pad reports analog mode.
					// Keep button-only touch input digital; expose the stick while braking.
					s_controllers[slot].snapshot.id =
					    (s_touchAnalogLX != 0x80 || s_touchAnalogLY != 0x80)
					        ? NATIVE_INPUT_PAD_ANALOG
					        : NATIVE_INPUT_PAD_DIGITAL;
				}
			}
		}
	}
	NativeInput_WritePadBus();
}

void Platform_InputControllerAdded(int deviceIndex)
{
	s32 slot;

	if (s_inputInitialized == 0)
	{
		return;
	}

	slot = NativeInput_FindSlotForDeviceIndex(deviceIndex);
	if (slot >= 0)
	{
		NativeInput_OpenController(deviceIndex, slot);
	}
}

void Platform_InputControllerRemoved(int instanceId)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].instanceId == (SDL_JoystickID)instanceId)
		{
			NativeInput_CloseController(slot);
			return;
		}
	}
}

int Platform_InputGetGamepadCount(void)
{
	s32 count = 0;
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL)
		{
			count++;
		}
	}

	return count;
}

void Platform_InputApplyTouchButtons(int slot, u16 buttons)
{
	(void)slot;

	// One line, once. Touch reaching native at all is the first thing worth
	// knowing when the on-screen pad appears to do nothing, and it separates a
	// Java-side problem from anything below it.
	{
		local_persist int reported = 0;

		if (!reported)
		{
			reported = 1;
			Platform_Log("[CTR Input] first touch mask 0x%04x (active=%d, padComm=%d)\n", (unsigned)buttons, s_touchActive, (int)g_padCommEnable);
		}
	}

	s_touchButtons = buttons;
}

// leftX/leftY are SDL-style axis values, -32768..32767, centre 0.
void Platform_InputApplyTouchAxes(int slot, int leftX, int leftY)
{
	(void)slot;

	s_touchAnalogLX = NativeInput_AxisToByte(leftX);
	s_touchAnalogLY = NativeInput_AxisToByte(leftY);
	s_touchActive = 1;
}

void Platform_InputReleaseTouch(void)
{
	s_touchButtons = 0xffff;
	s_touchAnalogLX = 0x80;
	s_touchAnalogLY = 0x80;
}

void Platform_InputSetTouchActive(int active)
{
	s_touchActive = active;

	if (!active)
	{
		Platform_InputReleaseTouch();
	}
}

int Platform_InputTouchActive(void)
{
	return s_touchActive;
}

int Platform_InputCycleKeyboardController(void)
{
	s_keyboardControllerSlot = NativeInput_NextControllerSlot(s_keyboardControllerSlot);
	return s_keyboardControllerSlot + 1;
}

int Platform_InputCycleGamepadController(void)
{
	s32 slot = NativeInput_FindActiveControllerSlot();
	s32 nextSlot;

	if (slot < 0)
	{
		return 0;
	}

	nextSlot = NativeInput_NextControllerSlot(slot);
	NativeInput_SwapControllerSlots(slot, nextSlot);
	NativeInput_WritePadBus();
	return nextSlot + 1;
}

void Platform_InputPadInit(int slot, unsigned char *padData)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_PHYSICAL_SLOT_COUNT))
	{
		return;
	}

	s_padSlotData[slot] = padData;
	NativeInput_WritePadBus();
}

int Platform_InputPadGetState(int port)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return PadStateDiscon;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return PadStateDiscon;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return PadStateDiscon;
	}

	return s_controllers[slot].snapshot.connected ? PadStateStable : PadStateDiscon;
}

int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count)
{
	s32 slot;

	if ((dst == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		dst[slot] = s_controllers[slot].snapshot;
	}

	return NATIVE_INPUT_MAX_CONTROLLERS;
}

int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count)
{
	s32 slot;

	if ((src == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = src[slot];
	}

	s_installedSnapshotsActive = 1;
	NativeInput_WriteInstalledSnapshots();
	return NATIVE_INPUT_MAX_CONTROLLERS;
}

void Platform_InputClearInstalledPadSnapshots(void)
{
	s_installedSnapshotsActive = 0;
}

int Platform_InputGetStateSize(void)
{
	return (int)sizeof(struct NativeInputStateSnapshot);
}

int Platform_InputCaptureState(void *dst, int dstSize)
{
	struct NativeInputStateSnapshot *snapshot = (struct NativeInputStateSnapshot *)dst;
	s32 slot;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_INPUT_STATE_MAGIC;
	snapshot->version = NATIVE_INPUT_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->keyboardControllerSlot = s_keyboardControllerSlot;
	snapshot->lastActiveControllerSlot = s_lastActiveControllerSlot;
	snapshot->installedSnapshotsActive = s_installedSnapshotsActive;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		snapshot->installedSnapshots[slot] = s_installedSnapshots[slot];
		snapshot->controllers[slot].snapshot = s_controllers[slot].snapshot;
		snapshot->controllers[slot].analogEnabled = s_controllers[slot].analogEnabled;
		snapshot->controllers[slot].switchingAnalog = s_controllers[slot].switchingAnalog;
		snapshot->controllers[slot].controllerToSlotMapping = s_controllerToSlotMapping[slot];
	}

	return 1;
}

int Platform_InputRestoreState(const void *src, int srcSize)
{
	const struct NativeInputStateSnapshot *snapshot = (const struct NativeInputStateSnapshot *)src;
	s32 slot;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_INPUT_STATE_MAGIC) || (snapshot->version != NATIVE_INPUT_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if (!NativeInput_IsValidControllerSlot(snapshot->keyboardControllerSlot))
	{
		return 0;
	}
	if ((snapshot->lastActiveControllerSlot < -1) || (snapshot->lastActiveControllerSlot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}
	if ((snapshot->installedSnapshotsActive < 0) || (snapshot->installedSnapshotsActive > 1))
	{
		return 0;
	}
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((snapshot->controllers[slot].analogEnabled < 0) || (snapshot->controllers[slot].analogEnabled > 1))
		{
			return 0;
		}
		if ((snapshot->controllers[slot].switchingAnalog < 0) || (snapshot->controllers[slot].switchingAnalog > 1))
		{
			return 0;
		}
		if (snapshot->controllers[slot].controllerToSlotMapping < -1)
		{
			return 0;
		}
	}

	s_keyboardControllerSlot = snapshot->keyboardControllerSlot;
	s_lastActiveControllerSlot = snapshot->lastActiveControllerSlot;
	s_installedSnapshotsActive = snapshot->installedSnapshotsActive != 0;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = snapshot->installedSnapshots[slot];
		s_controllers[slot].snapshot = snapshot->controllers[slot].snapshot;
		s_controllers[slot].analogEnabled = snapshot->controllers[slot].analogEnabled;
		s_controllers[slot].switchingAnalog = snapshot->controllers[slot].switchingAnalog;
		s_controllerToSlotMapping[slot] = snapshot->controllers[slot].controllerToSlotMapping;
	}
	NativeInput_WritePadBus();

	return 1;
}

void Platform_InputPadVibrate(int port, unsigned char *table, int len)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;
	struct NativeInputController *controller;
	u16 freqHigh;
	u16 freqLow;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS) || (table == NULL) || (len <= 0))
	{
		return;
	}

	controller = &s_controllers[slot];

    LOGI("Vibrate request: port=%d, slot=%d, table[0]=%d, table[1]=%d, controller=%p",
         port, slot, table[0], table[1], (void*)controller->controller);

    if (table[0] == 0 && table[1] == 0) {
        return;
    }

	freqHigh = table[0] ? (table[0] * 128) : 0;
	freqLow = len > 1 ? (table[1] * 128) : 0;

	if ((freqLow != 0) && (freqLow < 2048))
	{
		freqLow = 2048;
	}

	if (controller->controller != NULL)
	{
        bool hasRumble = SDL_GetBooleanProperty(SDL_GetGamepadProperties(controller->controller), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
        if (hasRumble)
        {
            LOGI("SDL_RumbleGamepad: low=%d, high=%d (orig: 0:%d, 1:%d)", freqLow, freqHigh, table[0], table[1]);
            SDL_RumbleGamepad(controller->controller, freqLow, freqHigh, 32);
            return;
        }
        else
        {
            LOGI("Controller connected but lacks rumble support. Falling back to device vibrator.");
        }
	}

#if defined(__ANDROID__)
    if (slot == 0)
    {
        // Fallback to phone vibrator for player 1 (touch controls or non-rumble controller)
        float highInt = (float)freqHigh / 65535.0f;
        float lowInt = (float)freqLow / 65535.0f;
        LOGI("Fallback to Android_JNI_HapticRumble: low=%.2f, high=%.2f", lowInt, highInt);
        NativeInput_AndroidVibrate(999999, lowInt, highInt, 32);
    }
#endif
}

#if defined(__ANDROID__)
JNIEXPORT jobjectArray JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchSnapshot(JNIEnv *env, jclass cls)
{
	struct CtrdsAndroidTouchSnapshot snapshot;
	char header[96];
	(void)cls;
	Ctrds_CopyAndroidTouchFrame(&snapshot);
	SDL_snprintf(header, sizeof(header), "%d|%u|%u|%llu|%d", snapshot.screen, snapshot.generation,
	             snapshot.frame, snapshot.producedNs, snapshot.inputReady);
	const char *fields[] = {header, snapshot.menu, snapshot.campaign, snapshot.pause,
	                        snapshot.character, snapshot.selection, snapshot.results, snapshot.name};
	jclass stringClass = (*env)->FindClass(env, "java/lang/String");
	if (stringClass == NULL) return NULL;
	jobjectArray result = (*env)->NewObjectArray(env, 8, stringClass, NULL);
	(*env)->DeleteLocalRef(env, stringClass);
	if (result == NULL) return NULL;
	for (int i = 0; i < 8; i++)
	{
		jstring field = (*env)->NewStringUTF(env, fields[i]);
		if (field == NULL) return NULL;
		(*env)->SetObjectArrayElement(env, result, i, field);
		(*env)->DeleteLocalRef(env, field);
		if ((*env)->ExceptionCheck(env)) return NULL;
	}
	return result;
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchTrace(JNIEnv *env, jclass cls, jboolean enabled)
{
	(void)env;
	(void)cls;
	Ctrds_SetAndroidTouchTrace(enabled ? 1 : 0);
}

// Called from the on-screen controls overlay. buttonMask is active-low, matching
// the pad; stickX/stickY are -32768..32767.
JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchInput(JNIEnv *env, jclass cls, jint buttonMask, jint stickX, jint stickY, jlong generation)
{
	(void)env;
	(void)cls;

	Ctrds_ApplyAndroidTouchPad((unsigned int)generation, (u16)buttonMask, (int)stickX, (int)stickY);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeFinishStartupAudio(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	NativeAudio_EndStartupMute();
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchMenuTap(JNIEnv *env, jclass cls, jfloat nx, jfloat ny, jlong generation)
{
	(void)env;
	(void)cls;
	Ctrds_QueueAndroidTouch((unsigned int)generation, 0, 0, (float)nx, (float)ny);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchRootMenuChoice(JNIEnv *env, jclass cls, jint choice, jlong generation)
{
	(void)env;
	(void)cls;
	Ctrds_QueueAndroidTouch((unsigned int)generation, 1, (int)choice, 0, 0);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchMenuChoice(JNIEnv *env, jclass cls, jint choice, jlong generation)
{
	(void)env;
	(void)cls;
	Ctrds_QueueAndroidTouch((unsigned int)generation, 2, (int)choice, 0, 0);
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchMenuSnapshot(JNIEnv *env, jclass cls)
{
	char snapshot[640];
	int selected = 0;
	char result[672];
	(void)cls;
	if (!Ctrds_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot), &selected))
	{
		return (*env)->NewStringUTF(env, "");
	}
	SDL_snprintf(result, sizeof(result), "%d\n%s", selected, snapshot);
	return (*env)->NewStringUTF(env, result);
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchRaceResultsSnapshot(JNIEnv *env, jclass cls)
{
	char snapshot[160];
	(void)cls;
	if (Ctrds_CopyAndroidRaceResultsSnapshot(snapshot, sizeof(snapshot)))
		return (*env)->NewStringUTF(env, snapshot);
	return (*env)->NewStringUTF(env, "");
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchAdventureCampaignSnapshot(JNIEnv *env, jclass cls)
{
	char snapshot[192] = {0};
	(void)cls;
	if (Ctrds_CopyTouchAdventureCampaignSnapshot(snapshot, sizeof(snapshot)))
	{
		return (*env)->NewStringUTF(env, snapshot);
	}
	return (*env)->NewStringUTF(env, "");
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchAdventurePauseSnapshot(JNIEnv *env, jclass cls)
{
	char snapshot[2048] = {0};
	(void)cls;
	if (Ctrds_CopyAndroidAdventurePauseSnapshot(snapshot, sizeof(snapshot)))
	{
		return (*env)->NewStringUTF(env, snapshot);
	}
	return (*env)->NewStringUTF(env, "");
}

JNIEXPORT jint JNICALL Java_com_ctrnative_CTRNativeActivity_nativeGameAudioVolume(JNIEnv *env, jclass cls, jint type)
{
	(void)env;
	(void)cls;
	return (jint)Ctrds_GetAndroidAudioVolume((int)type);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeSetGameAudioVolume(JNIEnv *env, jclass cls, jint type, jint volume)
{
	(void)env;
	(void)cls;
	Ctrds_QueueAndroidAudioVolume((int)type, (int)volume);
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchCharacterSelectName(JNIEnv *env, jclass cls)
{
	char name[64] = {0};
	(void)cls;
	Ctrds_CopyAndroidCharacterSelectName(name, sizeof(name));
	return (*env)->NewStringUTF(env, name);
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchSelectionText(JNIEnv *env, jclass cls)
{
	char snapshot[2048] = {0};
	(void)cls;
	Ctrds_CopyAndroidSelectionText(snapshot, sizeof(snapshot));
	return (*env)->NewStringUTF(env, snapshot);
}

// The frame cap, so the Java side can ask the display for a matching refresh
// rate. Reading ctrds.cfg again from Java would be a second parser of the same
// file that could disagree with this one.
JNIEXPORT jint JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTargetFps(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	return (jint)Ctrds_TargetFps();
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchSetActive(JNIEnv *env, jclass cls, jboolean active)
{
	(void)env;
	(void)cls;

	Platform_InputSetTouchActive(active ? 1 : 0);
}

JNIEXPORT jint JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchControlsMode(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	return (jint)Ctrds_TouchControlsMode();
}

JNIEXPORT jint JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchScreenState(JNIEnv *env, jclass cls)
{
        (void)env;
        (void)cls;

        struct CtrdsAndroidTouchSnapshot snapshot;
        Ctrds_CopyAndroidTouchFrame(&snapshot);
        return (jint)snapshot.screen;
}

JNIEXPORT jstring JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchSubmitNameSnapshot(JNIEnv *env, jclass cls)
{
	char name[17] = {0};
	(void)cls;
	Ctrds_CopyAndroidSubmitNameSnapshot(name, sizeof(name));
	return (*env)->NewStringUTF(env, name);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeTouchSubmitName(JNIEnv *env, jclass cls, jstring name, jboolean save)
{
	const char *utf = (name != NULL) ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
	(void)cls;
	Ctrds_QueueAndroidSubmitName(utf, save ? 1 : 0);
	if ((name != NULL) && (utf != NULL))
	{
		(*env)->ReleaseStringUTFChars(env, name, utf);
	}
}

JNIEXPORT jint JNICALL Java_com_ctrnative_CTRNativeActivity_nativeGetGamepadCount(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;

	return (jint)Platform_InputGetGamepadCount();
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeSetGamepadBindings(JNIEnv *env, jclass cls, jintArray values)
{
	jint bindings[16];
	(void)cls;
	if (!values || (*env)->GetArrayLength(env, values) != 16) return;
	(*env)->GetIntArrayRegion(env, values, 0, 16, bindings);
	if ((*env)->ExceptionCheck(env)) return;
	for (int i = 0; i < 16; i++)
		if (!((bindings[i] >= 0 && bindings[i] <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
			|| bindings[i] == (NATIVE_INPUT_MAP_FLAG_AXIS | SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
			|| bindings[i] == (NATIVE_INPUT_MAP_FLAG_AXIS | SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))) return;
	SDL_LockSpinlock(&s_androidMappingLock);
	for (int i = 0; i < 16; i++) s_androidBindings[i] = bindings[i] + 1;
	SDL_UnlockSpinlock(&s_androidMappingLock);
}

#endif
