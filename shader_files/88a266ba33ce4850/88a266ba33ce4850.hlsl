// Do-nothing shadow caster — outputs max depth so geometry casts no shadow.
// Used to test whether 88a266ba33ce4850 is the feather shadow culprit.
// If the feather shadow disappears in-game, this is confirmed.

void main(out float4 color : COLOR0, out float depth : DEPTH) {
    color = float4(1, 1, 1, 1);
    depth = 1.0;
}
