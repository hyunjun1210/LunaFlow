using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace LunaFlow.Runtime
{
    [Serializable] public class ClipInfo { public string id, name; public float duration; public bool loop; }
    [Serializable] public class ObjectInfo { public string id, name, path; }
    [Serializable] public class EffectInfo
    {
        public string id, name, clipId, bonePath;
        public int type;
        public float[] translation, rotation, scale, color;
        public float size = .5f, intensity = 1, start, end = 3;
    }
    [Serializable] public class SceneManifest
    {
        public string format, fbx, defaultClipId;
        public int version;
        public float fps;
        public ClipInfo[] clips;
        public ObjectInfo[] objects;
        public EffectInfo[] effects;
    }

    [DisallowMultipleComponent]
    public sealed class LunaFlowScene : MonoBehaviour
    {
        [SerializeField] private TextAsset manifest;
        private SceneManifest data;
        private Animator animator;
        private readonly List<EffectState> effects = new List<EffectState>();
        private float elapsed;

        private sealed class EffectState
        {
            public EffectInfo info;
            public Transform target, root;
            public Material material;
            public Mesh mesh;
            public MeshRenderer renderer;
            public LineRenderer[] lines;
            public readonly List<TrailSample> samples = new List<TrailSample>();
            public float lastTime = -1;
        }
        private struct TrailSample { public float time; public Vector3 a, b; }

        public void SetManifest(TextAsset asset) { manifest = asset; }
        private static Vector3 V3(float[] a, Vector3 fallback)
        {
            return a != null && a.Length == 3 ? new Vector3(a[0], a[1], a[2]) : fallback;
        }
        private static Quaternion Q4(float[] a)
        {
            return a != null && a.Length == 4 ? new Quaternion(a[0], a[1], a[2], a[3]).normalized : Quaternion.identity;
        }
        private static Color ColorOf(EffectInfo info)
        {
            var c = info.color;
            return c != null && c.Length == 4 ? new Color(c[0] * info.intensity, c[1] * info.intensity, c[2] * info.intensity, c[3] * .55f) : Color.magenta;
        }
        private static Transform FindNode(Transform root, string canonical)
        {
            string path = Uri.UnescapeDataString(canonical ?? "");
            return FindNode(root, "", path);
        }
        private static Transform FindNode(Transform node, string prefix, string wanted)
        {
            string path = prefix + "/" + node.name;
            if (path.EndsWith(wanted, StringComparison.Ordinal)) return node;
            foreach (Transform child in node)
            {
                var found = FindNode(child, path, wanted);
                if (found != null) return found;
            }
            return null;
        }
        private void Awake()
        {
            if (manifest == null) return;
            data = JsonUtility.FromJson<SceneManifest>(manifest.text);
            if (data == null || data.format != "lunaflow.unity" || data.version != 1) return;
            animator = GetComponentInChildren<Animator>();
            foreach (var info in data.effects ?? Array.Empty<EffectInfo>())
            {
                var target = FindNode(transform, info.bonePath);
                if (target == null) { Debug.LogWarning("LunaFlow: effect target not found: " + info.bonePath, this); continue; }
                var go = new GameObject("VFX / " + info.name); go.transform.SetParent(transform, false);
                var state = new EffectState { info = info, target = target, root = go.transform };
                var shader = Shader.Find("Universal Render Pipeline/Particles/Unlit") ?? Shader.Find("Particles/Standard Unlit") ?? Shader.Find("Sprites/Default");
                if (shader == null) { Destroy(go); continue; }
                state.material = new Material(shader) { name = "LunaFlow / " + info.name, color = Color.white, renderQueue = 3000 };
                state.material.SetInt("_SrcBlend", (int)BlendMode.SrcAlpha);
                state.material.SetInt("_DstBlend", (int)BlendMode.One);
                state.material.SetInt("_ZWrite", 0);
                if(state.material.HasProperty("_Cull"))state.material.SetInt("_Cull", (int)CullMode.Off);
                if (state.material.HasProperty("_Surface")) state.material.SetFloat("_Surface", 1);
                state.material.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                if (info.type == 0)
                {
                    state.mesh = new Mesh { name = "LunaFlow blade ribbon" }; state.mesh.MarkDynamic();
                    go.AddComponent<MeshFilter>().sharedMesh = state.mesh;
                    state.renderer = go.AddComponent<MeshRenderer>(); state.renderer.sharedMaterial = state.material;
                    state.renderer.shadowCastingMode = ShadowCastingMode.Off;
                }
                else
                {
                    int count = info.type == 1 ? 24 : 3; state.lines = new LineRenderer[count];
                    for (int i = 0; i < count; ++i)
                    {
                        var line = new GameObject("Line " + i).AddComponent<LineRenderer>(); line.transform.SetParent(go.transform, false);
                        line.sharedMaterial = state.material; line.useWorldSpace = true;
                        line.startWidth = line.endWidth = Mathf.Max(.002f, info.size * .008f);
                        line.startColor = line.endColor = ColorOf(info); line.shadowCastingMode = ShadowCastingMode.Off;
                        state.lines[i] = line;
                    }
                }
                effects.Add(state);
            }
        }
        private void LateUpdate()
        {
            if (data == null) return;
            elapsed += Time.deltaTime;
            ClipInfo active = data.clips != null && data.clips.Length > 0 ? data.clips[0] : null;
            float time = active != null && active.duration > 0 ? Mathf.Repeat(elapsed, active.duration) : elapsed;
            if (animator != null && animator.runtimeAnimatorController != null)
            {
                var playing = animator.GetCurrentAnimatorClipInfo(0);
                if (playing.Length > 0)
                {
                    string name = playing[0].clip.name;
                    foreach (var clip in data.clips ?? Array.Empty<ClipInfo>()) if (clip.name == name) { active = clip; break; }
                    var state = animator.GetCurrentAnimatorStateInfo(0);
                    time = (active != null && active.loop ? Mathf.Repeat(state.normalizedTime, 1) : Mathf.Clamp01(state.normalizedTime)) * playing[0].clip.length;
                }
            }
            foreach (var effect in effects)
            {
                bool visible = time >= effect.info.start && time <= effect.info.end && (string.IsNullOrEmpty(effect.info.clipId) || (active != null && effect.info.clipId == active.id));
                effect.root.gameObject.SetActive(visible);
                if (!visible) { effect.samples.Clear(); effect.lastTime = -1; continue; }
                if (time < effect.lastTime) effect.samples.Clear();
                effect.lastTime = time;
                Matrix4x4 matrix = effect.target.localToWorldMatrix * Matrix4x4.TRS(V3(effect.info.translation, Vector3.zero), Q4(effect.info.rotation), V3(effect.info.scale, Vector3.one));
                if (effect.info.type == 0) UpdateTrail(effect, matrix, time);
                else if (effect.info.type == 1) UpdateSparks(effect, matrix, time - effect.info.start);
                else UpdateAura(effect, matrix, time - effect.info.start);
            }
        }
        private static void UpdateTrail(EffectState state, Matrix4x4 matrix, float time)
        {
            state.samples.Add(new TrailSample { time = time, a = matrix.MultiplyPoint3x4(Vector3.zero), b = matrix.MultiplyPoint3x4(Vector3.up * state.info.size) });
            state.samples.RemoveAll(s => s.time < time - .25f);
            var vertices = new List<Vector3>(); var triangles = new List<int>(); var colors = new List<Color>();
            Color color = ColorOf(state.info);
            for (int i = 0; i < state.samples.Count; ++i)
            {
                var sample = state.samples[i]; vertices.Add(state.root.InverseTransformPoint(sample.a)); vertices.Add(state.root.InverseTransformPoint(sample.b));
                Color faded = color; faded.a *= (i + 1f) / state.samples.Count; colors.Add(faded); colors.Add(faded);
                if (i == 0) continue; int a = (i - 1) * 2, b = i * 2;
                triangles.AddRange(new[] { a, a + 1, b + 1, a, b + 1, b });
            }
            state.mesh.Clear(); state.mesh.SetVertices(vertices); state.mesh.SetColors(colors); state.mesh.SetTriangles(triangles, 0); state.mesh.RecalculateBounds();
        }
        private static void UpdateSparks(EffectState state, Matrix4x4 matrix, float time)
        {
            Vector3 origin = matrix.MultiplyPoint3x4(Vector3.zero);
            for (int i = 0; i < state.lines.Length; ++i)
            {
                float age = Mathf.Repeat(time + i * .037f, .65f), angle = i * 2.399963f;
                Vector3 velocity = new Vector3(Mathf.Cos(angle), 1.5f + (i % 5) * .15f, Mathf.Sin(angle)) * state.info.size;
                var line = state.lines[i]; line.positionCount = 2;
                line.SetPosition(0, origin + velocity * age - Vector3.up * (1.8f * age * age));
                float previous = Mathf.Max(0, age - .035f);
                line.SetPosition(1, origin + velocity * previous - Vector3.up * (1.8f * previous * previous));
            }
        }
        private static void UpdateAura(EffectState state, Matrix4x4 matrix, float time)
        {
            float radius = state.info.size * (.9f + .1f * Mathf.Sin(time * 5));
            for (int axis = 0; axis < 3; ++axis)
            {
                var line = state.lines[axis]; line.positionCount = 65;
                for (int i = 0; i <= 64; ++i)
                {
                    float angle = i * Mathf.PI * 2 / 64, c = Mathf.Cos(angle) * radius, s = Mathf.Sin(angle) * radius;
                    var point = axis == 0 ? new Vector3(0, c, s) : axis == 1 ? new Vector3(c, 0, s) : new Vector3(c, s, 0);
                    line.SetPosition(i, matrix.MultiplyPoint3x4(point));
                }
            }
        }
        private void OnDestroy()
        {
            foreach (var effect in effects) { Release(effect.mesh); Release(effect.material); }
        }
        private static void Release(UnityEngine.Object item) { if(item == null)return; if(Application.isPlaying)Destroy(item);else DestroyImmediate(item); }
    }
}
