package com.ctrnative;

public final class CTRTouchSnapshotTest {
    private static String[] packet(String header) {
        return new String[]{header + "|1", "0\nPISTA", "", "", "CRASH", "T\t1\t2\tCOPA", "", "NOME"};
    }

    public static void main(String[] args) {
        assert CTRTouchSnapshot.parse(null) == null;
        assert CTRTouchSnapshot.parse(new String[8]) == null;
        assert CTRTouchSnapshot.parse(new String[7]) == null;
        assert CTRTouchSnapshot.parse(packet("10|0|1|1")) == null;
        assert CTRTouchSnapshot.parse(packet("23|1|1|1")) == null;
        String[] loading = packet("22|1|1|1");
        loading[0] = "22|1|1|1|0";
        assert !CTRTouchSnapshot.parse(loading).inputReady;
        loading[0] = "22|1|1|1|bad";
        assert CTRTouchSnapshot.parse(loading) == null;
        assert CTRTouchSnapshot.parse(packet("10|bad|1|1")) == null;
        assert CTRTouchSnapshot.parse(packet("10|1|1")) == null;
        String[] fields = packet("10|4294967295|123|123456789");
        CTRTouchSnapshot snapshot = CTRTouchSnapshot.parse(fields);
        assert snapshot != null && snapshot.screen == 10 && snapshot.frame == 123;
        assert snapshot.inputReady;
        assert snapshot.generation == 4294967295L && snapshot.producedNs == 123456789;
        fields[5] = "MODIFIED";
        assert snapshot.selection.equals("T\t1\t2\tCOPA");
        assert snapshot.name.equals("NOME");
        assert snapshot.hasSameContent(CTRTouchSnapshot.parse(packet("10|4294967295|124|123456999")));
        assert !snapshot.hasSameContent(CTRTouchSnapshot.parse(packet("10|4294967294|124|123456999")));
        assert !snapshot.hasSameContent(null);
        System.out.println("Java snapshot: malformed/startup packets, metadata and immutable fields OK");
    }
}
