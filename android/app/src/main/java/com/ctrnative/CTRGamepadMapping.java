package com.ctrnative;

import android.view.KeyEvent;

/** SDL logical bindings shared by capture, menu navigation and the native pad. */
final class CTRGamepadMapping {
    static final String PREFIX = "gamepad_binding_";
    static final String[] ACTIONS = {"Acelerar / Confirmar (X)", "Usar item / Voltar (Círculo)",
            "Frear / Ré (Quadrado)", "Câmera / Voltar (Triângulo)", "Pular / Derrapar (L1)",
            "Pular / Derrapar (R1)", "L2", "R2", "Pausar (Start)", "Select", "L3", "R3",
            "Cima", "Baixo", "Esquerda", "Direita"};
    static final int[] DEFAULTS = {0, 1, 2, 3, 9, 10, 0x4004, 0x4005, 6, 4, 7, 8, 11, 12, 13, 14};

    static int bindingForKey(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: return 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1;
            case KeyEvent.KEYCODE_BUTTON_X: return 2;
            case KeyEvent.KEYCODE_BUTTON_Y: return 3;
            case KeyEvent.KEYCODE_BUTTON_SELECT: case KeyEvent.KEYCODE_BACK: return 4;
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_MENU: return 6;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 7;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 8;
            case KeyEvent.KEYCODE_BUTTON_L1: return 9;
            case KeyEvent.KEYCODE_BUTTON_R1: return 10;
            case KeyEvent.KEYCODE_DPAD_UP: return 11;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 12;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 13;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 14;
            case KeyEvent.KEYCODE_BUTTON_L2: return 0x4004;
            case KeyEvent.KEYCODE_BUTTON_R2: return 0x4005;
            default: return -1;
        }
    }

    static boolean isValid(int binding) {
        for (int value : DEFAULTS) if (binding == value) return true;
        return false;
    }

    static int actionForBinding(int[] bindings, int binding) {
        if (binding < 0) return -1;
        for (int i = 0; i < bindings.length; i++) if (bindings[i] == binding) return i;
        return -1;
    }

    static void assign(int[] bindings, int action, int binding) {
        if (action < 0 || action >= bindings.length || !isValid(binding)) return;
        int previous = actionForBinding(bindings, binding);
        if (previous >= 0) bindings[previous] = bindings[action];
        bindings[action] = binding;
    }

    static String name(int binding) {
        String[] names = {"A / Cross", "B / Círculo", "X / Quadrado", "Y / Triângulo",
                "Select", "Guia", "Start", "L3", "R3", "L1", "R1", "D-pad ↑", "D-pad ↓",
                "D-pad ←", "D-pad →"};
        if (binding == 0x4004) return "L2";
        if (binding == 0x4005) return "R2";
        return binding >= 0 && binding < names.length ? names[binding] : "Padrão";
    }
}
