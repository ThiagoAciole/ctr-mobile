package com.ctrnative;

public class CTRGamepadMappingTest {
    public static void main(String[] args) {
        int[] bindings = CTRGamepadMapping.DEFAULTS.clone();
        CTRGamepadMapping.assign(bindings, 0, 9);
        assert bindings[0] == 9 && bindings[4] == 0;
        assert CTRGamepadMapping.actionForBinding(bindings, 9) == 0;
        CTRGamepadMapping.assign(bindings, 0, -1);
        assert bindings[0] == 9;
        CTRGamepadMapping.assign(bindings, 6, 0x4005);
        assert bindings[6] == 0x4005 && bindings[7] == 0x4004;
        assert CTRGamepadMapping.bindingForKey(96) == 0;
        assert CTRGamepadMapping.bindingForKey(102) == 9;
        assert CTRGamepadMapping.bindingForKey(104) == 0x4004;
        assert CTRGamepadMapping.bindingForKey(24) == -1;
        assert !CTRGamepadMapping.isValid(0x4000);
        assert CTRGamepadMapping.actionForBinding(bindings, -1) == -1;
        System.out.println("Gamepad binding capture and swaps: OK");
    }
}
