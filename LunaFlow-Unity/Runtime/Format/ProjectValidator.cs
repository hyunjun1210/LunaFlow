using System;
using System.Collections.Generic;

namespace LunaFlow.UnityImporter.Format
{
    public sealed class ProjectFormatException : Exception
    {
        public ProjectFormatException(string message) : base(message) { }
    }

    public static class ProjectValidator
    {
        public const long MaxFileBytes = 256L * 1024 * 1024;
        public const long MaxCurveKeys = 2000000;
        public static void Validate(ProjectData data)
        {
            Need(data != null && data.format == "lunaflow.project" && data.schema_version == 1, "Expected lunaflow.project schema_version 1.");
            var scene = data.scene;
            Need(scene != null && scene.bones != null && scene.bones.Length > 0 && scene.bones.Length <= 10000, "Invalid bone count.");
            var paths = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < scene.bones.Length; ++i)
            {
                var bone = scene.bones[i]; Need(bone != null, "Null bone.");
                Text(bone.path, "bone path"); Need(paths.Add(bone.path), "Duplicate bone path: " + bone.path);
                Need(bone.parent >= -1 && bone.parent < scene.bones.Length && bone.parent != i, "Invalid parent: " + bone.path);
                Transform(bone.bindLocal, bone.path);
            }
            var state = new byte[scene.bones.Length];
            for (int i = 0; i < state.Length; ++i)
            {
                if (state[i] != 0) continue;
                var chain = new List<int>(); int current = i;
                while (current >= 0 && state[current] == 0) { state[current] = 1; chain.Add(current); current = scene.bones[current].parent; }
                Need(current < 0 || state[current] != 1, "Skeleton parent cycle.");
                foreach (int bone in chain) state[bone] = 2;
            }
            Need(scene.materials != null && scene.materials.Length > 0 && scene.materials.Length <= 4096, "Invalid materials.");
            foreach (var material in scene.materials)
            {
                Need(material != null, "Null material."); Vector(material.baseColor, 4, "baseColor");
                Bytes(material.textureBytes); Bytes(material.textureRGBA);
                Need(material.textureWidth >= 0 && material.textureWidth <= 16384 && material.textureHeight >= 0 && material.textureHeight <= 16384, "Invalid texture dimensions.");
                if (material.textureRGBA != null && material.textureRGBA.Length > 0)
                    Need((long)material.textureWidth * material.textureHeight * 4 == material.textureRGBA.Length, "RGBA size does not match dimensions.");
            }
            Need(scene.meshes != null && scene.meshes.Length <= 4096, "Invalid meshes.");
            long vertices = 0, indices = 0;
            foreach (var mesh in scene.meshes)
            {
                Need(mesh != null && mesh.material >= 0 && mesh.material < scene.materials.Length, "Invalid mesh material.");
                Need(mesh.vertices != null && mesh.vertices.Length > 0 && mesh.indices != null && mesh.indices.Length > 0 && mesh.indices.Length % 3 == 0, "Invalid triangle mesh.");
                vertices += mesh.vertices.Length; indices += mesh.indices.Length;
                Need(vertices <= 2000000 && indices <= 12000000, "Mesh import budget exceeded.");
                Need(mesh.skin != null && mesh.skin.Length > 0 && mesh.skin.Length <= 256, "Invalid skin palette.");
                foreach (var skin in mesh.skin) { Need(skin != null && skin.bone >= 0 && skin.bone < scene.bones.Length, "Unknown skin bone."); Vector(skin.inverseBind, 16, "inverseBind"); }
                foreach (var v in mesh.vertices)
                {
                    Need(v != null, "Null vertex."); Vector(v.p, 3, "position"); Vector(v.n, 3, "normal"); Vector(v.uv, 2, "UV"); Vector(v.w, 4, "weights");
                    Need(v.j != null && v.j.Length == 4, "Expected four joint indices."); float sum = 0;
                    for (int j = 0; j < 4; ++j) { Need(v.j[j] >= 0 && v.j[j] < mesh.skin.Length && v.w[j] >= 0, "Invalid skin influence."); sum += v.w[j]; }
                    Need(Finite(sum) && sum > 1e-8f, "Vertex has no skin weights.");
                }
                foreach (int index in mesh.indices) Need(index >= 0 && index < mesh.vertices.Length, "Triangle index out of range.");
            }
            Need(data.clips != null && data.clips.Length > 0 && data.clips.Length <= 256, "Invalid animation count.");
            var ids = new HashSet<string>(StringComparer.Ordinal);
            foreach (var clip in data.clips)
            {
                Need(clip != null && clip.format == "lunaflow.clip" && clip.version == 1, "Unsupported animation schema.");
                Text(clip.id, "clip ID"); Text(clip.name, "clip name"); Need(ids.Add(clip.id), "Duplicate clip ID.");
                Need(Finite(clip.duration) && clip.duration >= 0 && clip.duration <= 3600 && Finite(clip.speed) && clip.speed > 0 && clip.duration / clip.speed <= 3600, "Invalid animation duration/speed.");
                var c = clip.coordinates;
                Need(c != null && c.handedness == "left" && c.up == "+Y" && c.unit == "meter" && c.quaternion == "xyzw", "Unsupported motion coordinates.");
                Need(clip.tracks != null, "Missing motion tracks."); var tracked = new HashSet<string>(StringComparer.Ordinal);
                foreach (var track in clip.tracks)
                {
                    Need(track != null && paths.Contains(track.bonePath) && tracked.Add(track.bonePath), "Unknown or duplicate bone track.");
                    Keys(track.positions, 3, clip.duration, false); Keys(track.rotations, 4, clip.duration, true); Keys(track.scales, 3, clip.duration, false);
                }
            }
            var effectIds = new HashSet<string>(StringComparer.Ordinal);
            foreach (var effect in scene.effects ?? Array.Empty<EffectData>())
            {
                Need(effect != null, "Null effect."); Text(effect.id, "effect ID"); Need(effectIds.Add(effect.id), "Duplicate effect ID.");
                Need(effect.type >= 0 && effect.type <= 3 && effect.bone >= 0 && effect.bone < scene.bones.Length, "Invalid effect type/bone.");
                Need(string.IsNullOrEmpty(effect.clipId) || ids.Contains(effect.clipId), "Unknown effect clip.");
                Transform(effect.local, "effect transform"); Vector(effect.color, 4, "effect color"); Range(effect.size, .000001f, 10000, "effect size"); Range(effect.intensity, 0, 10000, "effect intensity");
                Need(Finite(effect.start) && Finite(effect.end) && effect.start >= 0 && effect.end >= effect.start && effect.end <= 3600, "Invalid effect time.");
                if (effect.type == 3) Particles(effect.particles);
            }
            foreach (var item in scene.objects ?? Array.Empty<ObjectData>()) Need(item != null && item.root >= 0 && item.root < scene.bones.Length, "Invalid object root.");
        }

        public static long CurveBudget(ProjectData data, int fps)
        {
            Need(fps >= 1 && fps <= 120, "Sample rate must be 1..120."); long total = 0;
            foreach (var clip in data.clips)
            {
                long frames = Math.Max(2, (long)Math.Ceiling(clip.duration / clip.speed * fps) + 1);
                // Untracked bones get two constant keys, preventing stale poses when switching clips.
                total += (frames * clip.tracks.Length + 2L * (data.scene.bones.Length - clip.tracks.Length)) * 10;
                foreach (var track in clip.tracks) total += 10L * ((track.positions == null ? 0 : track.positions.Length) + (track.rotations == null ? 0 : track.rotations.Length) + (track.scales == null ? 0 : track.scales.Length));
                Need(total <= MaxCurveKeys, "Animation sampling budget exceeded. Lower the sample rate or split the project.");
            }
            return total;
        }

        private static void Particles(ParticleData p)
        {
            Need(p != null && p.shape >= 0 && p.shape <= 3, "Invalid particle system.");
            Range(p.duration, .05f, 120, "particle duration"); Range(p.rate, 0, 2000, "rate"); Range(p.lifetime, .05f, 20, "lifetime"); Range(p.lifetimeRandomness, 0, .95f, "lifetime randomness");
            Range(p.speed, 0, 50, "speed"); Range(p.speedRandomness, 0, 1, "speed randomness"); Range(p.gravity, -5, 5, "gravity"); Range(p.radius, 0, 20, "radius"); Range(p.angle, 0, 90, "angle");
            Range(p.startSize, .001f, 10, "start size"); Range(p.endSize, 0, 10, "end size"); Vector(p.box, 3, "particle box"); Vector(p.velocity, 3, "particle velocity"); Vector(p.endColor, 4, "end color");
            foreach (float value in p.box) Range(value, 0, 40, "box"); foreach (float value in p.velocity) Range(value, -50, 50, "velocity"); foreach (float value in p.endColor) Range(value, 0, 1, "end color");
            Need(p.maxParticles > 0 && p.maxParticles <= 4096 && p.burst >= 0 && p.burst <= 4096 && p.seed > 0 && p.seed <= uint.MaxValue, "Invalid particle budget/seed.");
        }
        private static void Keys(KeyData[] keys, int size, double duration, bool quaternion)
        {
            if (keys == null) return; Need(keys.Length <= 100000, "Too many channel keys.");
            foreach (var key in keys)
            {
                Need(key != null && Finite(key.time) && key.time >= 0 && key.time <= duration + 1e-6, "Invalid key time."); Vector(key.value, size, "key value");
                if (quaternion) { double length = 0; foreach (float v in key.value) length += (double)v * v; Need(length > 1e-12, "Zero quaternion."); }
            }
            Array.Sort(keys, (a, b) => a.time.CompareTo(b.time));
            for (int i = 1; i < keys.Length; ++i) Need(keys[i].time - keys[i - 1].time >= 1e-9, "Duplicate channel key times.");
        }
        private static void Transform(TransformData t, string field)
        {
            Need(t != null, "Missing " + field); Vector(t.translation, 3, field); Vector(t.rotation, 4, field); Vector(t.scale, 3, field);
            double length = 0; foreach (float v in t.rotation) length += (double)v * v; Need(length > 1e-12, "Zero bind quaternion.");
        }
        private static void Bytes(int[] bytes) { if (bytes == null) return; Need(bytes.Length <= 64 * 1024 * 1024, "Texture budget exceeded."); foreach (int value in bytes) Need(value >= 0 && value <= 255, "Invalid texture byte."); }
        private static void Vector(float[] value, int size, string field) { Need(value != null && value.Length == size, "Invalid " + field + " vector."); foreach (float v in value) Need(Finite(v), "Non-finite " + field); }
        private static void Text(string text, string field) { Need(!string.IsNullOrEmpty(text) && text.Length <= 4096, "Invalid " + field); }
        private static bool Finite(double v) { return !double.IsNaN(v) && !double.IsInfinity(v); }
        private static void Range(float v, float min, float max, string field) { Need(Finite(v) && v >= min && v <= max, "Out of range: " + field); }
        private static void Need(bool condition, string message) { if (!condition) throw new ProjectFormatException(message); }
    }
}
