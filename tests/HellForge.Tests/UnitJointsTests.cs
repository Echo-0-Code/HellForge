using System.Buffers.Binary;
using HellForge.Core;
using HellForge.Core.GameData;

// A unit copy's joints moved (a data spec's "joints"): the moved joint's local translation puts it where asked under a
// rotated parent, and the joints below it move with it; the others stay.
static class UnitJointsTests
{
    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject)
    {
        test("A unit copy's joint moves to its new rest position, with the joints below it", () =>
        {
            // root (0), arm (1, under the root, turned 90 degrees about z, at (1, 0, 0)), seat (2, under the arm, at
            // (1, 1, 0.5)), lamp (3, under the root, at (0, 2, 0)).
            const int at = 0x40, n = 4;
            var m = new byte[at + 16 + 128 * n + 8 * n];
            BinaryPrimitives.WriteInt32LittleEndian(m.AsSpan(0x34), at);
            BinaryPrimitives.WriteInt32LittleEndian(m.AsSpan(at), n);
            int local = at + 16, world = local + 64 * n, parents = world + 64 * n, names = parents + 4 * n;
            float[] identity = [1, 0, 0, 0, 1, 0, 0, 0, 1], turned = [0, 1, 0, -1, 0, 0, 0, 0, 1];
            void Joint(int i, int parent, string name, float[] rotation, float[] localT, float[] worldT)
            {
                for (var k = 0; k < 9; k++) BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(local + 64 * i + 4 * k), rotation[k]);
                for (var k = 0; k < 3; k++) BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(local + 64 * i + 36 + 4 * k), localT[k]);
                for (var k = 0; k < 3; k++) BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(local + 64 * i + 48 + 4 * k), 1);
                // world matrix rows: the rotation's rows, then the translation
                for (var r = 0; r < 3; r++)
                    for (var c = 0; c < 3; c++) BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(world + 64 * i + 16 * r + 4 * c), rotation[3 * r + c]);
                for (var k = 0; k < 3; k++) BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(world + 64 * i + 48 + 4 * k), worldT[k]);
                BinaryPrimitives.WriteSingleLittleEndian(m.AsSpan(world + 64 * i + 60), 1);
                BinaryPrimitives.WriteUInt32LittleEndian(m.AsSpan(parents + 4 * i), (uint)(parent << 16) | 1);
                BinaryPrimitives.WriteUInt32LittleEndian(m.AsSpan(names + 4 * i), name.StartsWith('#') ? UnitJoints.Hash(name) : EngineHash.Id32(name));
            }
            Joint(0, 0, "root", identity, [0, 0, 0], [0, 0, 0]);
            Joint(1, 0, "arm", turned, [1, 0, 0], [1, 0, 0]);
            Joint(2, 1, "seat", identity, [1, 0, 0.5f], [1, 1, 0.5f]); // (1, 0, 0.5) turned is (0, 1, 0.5), plus the arm's (1, 0, 0)
            Joint(3, 0, "#0badf00d", identity, [0, 2, 0], [0, 2, 0]);
            float W(int i, int k) => BinaryPrimitives.ReadSingleLittleEndian(m.AsSpan(world + 64 * i + 48 + 4 * k));
            float L(int i, int k) => BinaryPrimitives.ReadSingleLittleEndian(m.AsSpan(local + 64 * i + 36 + 4 * k));
            bool Near(float a, double b) => Math.Abs(a - b) < 1e-5;

            check(UnitJoints.Move(m, new Dictionary<string, double[]> { ["seat"] = [0, 3, 1] }) == 1, "Only the seat should move.");
            // seat world (0, 3, 1) = local * turned + (1, 0, 0): local = (3, 1, 1), since (3, 1, 1) turned is (-1, 3, 1)
            check(Near(W(2, 0), 0) && Near(W(2, 1), 3) && Near(W(2, 2), 1) && Near(L(2, 0), 3) && Near(L(2, 1), 1) && Near(L(2, 2), 1),
                $"The seat is at ({W(2, 0)}, {W(2, 1)}, {W(2, 2)}), local ({L(2, 0)}, {L(2, 1)}, {L(2, 2)}).");
            check(UnitJoints.Move(m, new Dictionary<string, double[]> { ["arm"] = [2, 0, 0], ["#0badf00d"] = [0, 2, -5] }) == 3, "The arm, its seat and the lamp move.");
            check(Near(W(2, 0), 1) && Near(W(2, 1), 3) && Near(W(1, 0), 2) && Near(L(1, 0), 2) && Near(W(3, 2), -5) && Near(L(3, 2), -5),
                "The seat did not follow its arm, or the hashed lamp did not move.");
            check(Near(W(0, 0), 0) && Near(L(2, 0), 3), "A joint that was not moved changed.");
            reject(() => UnitJoints.Move(m, new Dictionary<string, double[]> { ["wheel"] = [0, 0, 0] }));
            reject(() => UnitJoints.Move(m, new Dictionary<string, double[]> { ["root"] = [0, 0, 1] }));
            reject(() => UnitJoints.Move(new byte[0x40], new Dictionary<string, double[]> { ["seat"] = [0, 0, 1] }));

            // The lamp (3) hung from the arm (1, turned 90 degrees): it keeps its world rotation, so its local rotation is
            // the arm's undone, and at (0, 2, 0) its local translation is (2, 2, 0): (2, 2, 0) turned is (-2, 2, 0), plus the arm's (2, 0, 0).
            check(UnitJoints.Move(m, new Dictionary<string, double[]> { ["#0badf00d"] = [0, 2, 0] }, new Dictionary<string, string> { ["#0badf00d"] = "arm" }) == 1,
                "Only the lamp should move.");
            float R(int i, int k) => BinaryPrimitives.ReadSingleLittleEndian(m.AsSpan(local + 64 * i + 4 * k));
            check(BinaryPrimitives.ReadUInt32LittleEndian(m.AsSpan(parents + 4 * 3)) == (1u << 16 | 1), "The lamp's parent is not the arm.");
            check(Near(L(3, 0), 2) && Near(L(3, 1), 2) && Near(L(3, 2), 0) && Near(W(3, 1), 2) && Near(W(3, 2), 0),
                $"The hung lamp's local translation is ({L(3, 0)}, {L(3, 1)}, {L(3, 2)}).");
            check(Near(R(3, 0), 0) && Near(R(3, 1), -1) && Near(R(3, 3), 1) && Near(R(3, 4), 0) && Near(R(3, 8), 1), "The hung lamp's local rotation does not undo the arm's.");
            reject(() => UnitJoints.Move(m, new Dictionary<string, double[]> { ["arm"] = [0, 0, 0] }, new Dictionary<string, string> { ["arm"] = "seat" })); // a parent after it
        });
    }
}
