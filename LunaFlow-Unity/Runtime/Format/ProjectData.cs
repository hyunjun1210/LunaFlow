using System;

namespace LunaFlow.UnityImporter.Format
{
    // Mirrors schema_version 1 written by LunaFlow's ProjectIO. No Unity types
    // here, so the contract validator is also testable outside the Editor.
    [Serializable] public sealed class ProjectData
    {
        public string format;
        public int schema_version;
        public string model_path;
        public SceneData scene;
        public ClipData[] clips;
        public EditorData editor;
    }
    [Serializable] public sealed class SceneData
    {
        public BoneData[] bones;
        public MaterialData[] materials;
        public MeshData[] meshes;
        public ObjectData[] objects;
        public EffectData[] effects;
    }
    [Serializable] public sealed class TransformData
    {
        public float[] translation;
        public float[] rotation;
        public float[] scale;
    }
    [Serializable] public sealed class BoneData
    {
        public string name, path;
        public int parent;
        public TransformData bindLocal;
    }
    [Serializable] public sealed class MaterialData
    {
        public string name, texturePath;
        public float[] baseColor;
        // JSON stores integer arrays, not the base64 encoding used by some serializers.
        public int[] textureBytes, textureRGBA;
        public int textureWidth, textureHeight;
    }
    [Serializable] public sealed class MeshData
    {
        public string name;
        public int material;
        public int[] indices;
        public SkinData[] skin;
        public VertexData[] vertices;
    }
    [Serializable] public sealed class SkinData { public int bone; public float[] inverseBind; }
    [Serializable] public sealed class VertexData { public float[] p, n, uv, w; public int[] j; }
    [Serializable] public sealed class ObjectData { public string id, name; public int root; }
    [Serializable] public sealed class ClipData
    {
        public string format, id, name;
        public int version;
        public double duration, speed;
        public bool loop;
        public long revision;
        public CoordinateData coordinates;
        public TrackData[] tracks;
    }
    [Serializable] public sealed class CoordinateData { public string handedness, up, unit, quaternion; }
    [Serializable] public sealed class TrackData { public string bonePath; public KeyData[] positions, rotations, scales; }
    [Serializable] public sealed class KeyData { public double time; public float[] value; }
    [Serializable] public sealed class EditorData { public string clipId, bonePath; public double time; }
    [Serializable] public sealed class EffectData
    {
        public string id, name, clipId, sourcePrompt;
        public int type, bone;
        public TransformData local;
        public float[] color;
        public float size, intensity;
        public double start, end;
        public bool enabled;
        public ParticleData particles;
    }
    [Serializable] public sealed class ParticleData
    {
        public int shape, maxParticles, burst;
        public long seed;
        public float duration, rate, lifetime, lifetimeRandomness, speed, speedRandomness;
        public float gravity, radius, angle, startSize, endSize;
        public float[] box, velocity, endColor;
        public bool loop, worldSpace, additive;
    }
}
