using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using LunaFlow.UnityImporter.Format;
using UnityEditor;
using UnityEditor.Animations;
using UnityEditor.AssetImporters;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace LunaFlow.UnityImporter.Editor
{
    [ScriptedImporter(1, "lunaflow")]
    public sealed class LunaFlowProjectImporter : ScriptedImporter
    {
        [Range(1, 120)] public int sampleRate = 30;
        public bool importEffects = true;
        public Shader materialShader;

        public override void OnImportAsset(AssetImportContext ctx)
        {
            // Validate before allocating native objects or registering any output.
            var info = new FileInfo(ctx.assetPath);
            if (info.Length > ProjectValidator.MaxFileBytes) throw new ProjectFormatException("LunaFlow project exceeds 256 MB.");
            var data = JsonUtility.FromJson<ProjectData>(File.ReadAllText(ctx.assetPath, Encoding.UTF8));
            ProjectValidator.Validate(data); ProjectValidator.CurveBudget(data, sampleRate);
            var owned = new List<Object>();
            var warnings = new List<string>();
            try
            {
                var root = Own(owned, new GameObject(Path.GetFileNameWithoutExtension(ctx.assetPath)));
                var bones = BuildSkeleton(data.scene.bones, root.transform);
                var materials = BuildMaterials(data.scene.materials, ctx, owned, warnings);
                BuildMeshes(data.scene.meshes, root.transform, bones, materials, ctx, owned);
                var metadata = Own(owned, ScriptableObject.CreateInstance<LunaFlowProjectAsset>());
                metadata.name = "LunaFlow Project"; metadata.schemaVersion = 1; metadata.boneCount = bones.Length;
                metadata.meshCount = data.scene.meshes.Length;
                var motions = BuildAnimations(data, root.transform, bones, ctx, owned);
                metadata.motions = motions;
                metadata.defaultClipId = data.editor == null ? null : data.editor.clipId;
                if (Array.Find(motions, m => m.id == metadata.defaultClipId) == null) metadata.defaultClipId = motions[0].id;
                var controller = BuildController(motions, metadata.defaultClipId, ctx, owned);
                var animator = root.AddComponent<Animator>(); animator.runtimeAnimatorController = controller;
                animator.applyRootMotion = false; animator.cullingMode = AnimatorCullingMode.AlwaysAnimate;
                var avatar = Own(owned, AvatarBuilder.BuildGenericAvatar(root, ""));
                if (avatar != null && avatar.isValid) { avatar.name = root.name + " Generic Avatar"; animator.avatar = avatar; ctx.AddObjectToAsset("avatar", avatar); }
                else
                {
                    if (avatar != null) { owned.Remove(avatar); Object.DestroyImmediate(avatar); }
                    warnings.Add("Generic Avatar could not be built. Transform animation curves remain available.");
                }
                var effects = importEffects ? EffectBuilder.Build(data.scene.effects, bones, ctx, owned, warnings) : Array.Empty<LunaFlowEffect>();
                metadata.effectCount = effects.Length;
                root.AddComponent<LunaFlowPlayer>().Configure(metadata, effects);
                metadata.warnings = warnings.ToArray();
                ctx.AddObjectToAsset("project-info", metadata); ctx.AddObjectToAsset("project", root); ctx.SetMainObject(root);
                foreach (string warning in warnings) ctx.LogImportWarning(warning);
            }
            catch
            {
                // Unity destroys successful import outputs; explicitly clean partially built outputs on failure.
                for (int i = owned.Count - 1; i >= 0; --i) if (owned[i] != null) Object.DestroyImmediate(owned[i]);
                throw;
            }
        }

        internal static T Own<T>(List<Object> owned, T value) where T : Object { if (value != null) owned.Add(value); return value; }
        internal static Vector3 V3(float[] a) { return new Vector3(a[0], a[1], a[2]); }
        internal static Color Color4(float[] a) { return new Color(a[0], a[1], a[2], a[3]); }
        internal static void ApplyTransform(Transform target, TransformData value)
        {
            target.localPosition = V3(value.translation); var q = MotionSampler.Normalize(value.rotation);
            target.localRotation = new Quaternion(q[0], q[1], q[2], q[3]); target.localScale = V3(value.scale);
        }
        internal static string Key(string prefix, string value)
        {
            using (var hash = SHA256.Create()) return prefix + BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(value))).Replace("-", "").ToLowerInvariant();
        }
        internal static string NodeName(string name)
        {
            return string.IsNullOrEmpty(name) ? "Node" : name.Replace('/', '_').Replace('\\', '_').Replace('\0', '_');
        }

        private static Transform[] BuildSkeleton(BoneData[] source, Transform root)
        {
            var result = new Transform[source.Length]; var siblings = new Dictionary<int, HashSet<string>>();
            for (int i = 0; i < source.Length; ++i)
            {
                HashSet<string> names;
                if (!siblings.TryGetValue(source[i].parent, out names)) siblings[source[i].parent] = names = new HashSet<string>(StringComparer.Ordinal);
                string name = NodeName(source[i].name);
                if (!names.Add(name)) { name += "__" + Key("", source[i].path).Substring(0, 12); while (!names.Add(name)) name += "_"; }
                result[i] = new GameObject(name).transform;
                result[i].SetParent(root, false);
            }
            for (int i = 0; i < source.Length; ++i)
            {
                result[i].SetParent(source[i].parent < 0 ? root : result[source[i].parent], false);
                ApplyTransform(result[i], source[i].bindLocal);
            }
            return result;
        }

        private Material[] BuildMaterials(MaterialData[] source, AssetImportContext ctx, List<Object> owned, List<string> warnings)
        {
            Shader shader = materialShader;
            if (shader == null)
            {
                bool srp = GraphicsSettings.currentRenderPipeline != null;
                shader = srp ? Shader.Find("Universal Render Pipeline/Lit") : Shader.Find("Standard");
                if (shader == null) shader = Shader.Find("Standard") ?? Shader.Find("Universal Render Pipeline/Unlit") ?? Shader.Find("Unlit/Texture");
            }
            if (shader == null) throw new ProjectFormatException("No compatible material shader. Set Material Shader in the importer Inspector.");
            var result = new Material[source.Length];
            for (int i = 0; i < source.Length; ++i)
            {
                var input = source[i]; var material = Own(owned, new Material(shader) { name = string.IsNullOrEmpty(input.name) ? "Material " + i : input.name });
                var color = Color4(input.baseColor);
                if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
                if (material.HasProperty("_Color")) material.SetColor("_Color", color);
                if (color.a < .999f)
                {
                    material.renderQueue = 3000;
                    if (material.HasProperty("_Surface")) material.SetFloat("_Surface", 1);
                    if (material.HasProperty("_Mode")) material.SetFloat("_Mode", 3);
                    if (material.HasProperty("_SrcBlend")) material.SetInt("_SrcBlend", (int)BlendMode.SrcAlpha);
                    if (material.HasProperty("_DstBlend")) material.SetInt("_DstBlend", (int)BlendMode.OneMinusSrcAlpha);
                    if (material.HasProperty("_ZWrite")) material.SetInt("_ZWrite", 0);
                    material.EnableKeyword("_ALPHABLEND_ON"); material.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                }
                var texture = LoadTexture(input, i, ctx, owned, warnings);
                if (texture != null) { if (material.HasProperty("_BaseMap")) material.SetTexture("_BaseMap", texture); if (material.HasProperty("_MainTex")) material.SetTexture("_MainTex", texture); }
                result[i] = material; ctx.AddObjectToAsset("material-" + i, material);
            }
            return result;
        }

        private static Texture2D LoadTexture(MaterialData input, int index, AssetImportContext ctx, List<Object> owned, List<string> warnings)
        {
            bool encoded = input.textureBytes != null && input.textureBytes.Length > 0;
            bool raw = input.textureRGBA != null && input.textureRGBA.Length > 0;
            if (encoded || raw)
            {
                var texture = Own(owned, new Texture2D(raw ? input.textureWidth : 2, raw ? input.textureHeight : 2, TextureFormat.RGBA32, true) { name = "Texture " + index });
                if (encoded)
                {
                    var bytes = Array.ConvertAll(input.textureBytes, v => (byte)v);
                    if (!ImageConversion.LoadImage(texture, bytes, false))
                    {
                        owned.Remove(texture); Object.DestroyImmediate(texture);
                        warnings.Add("An embedded texture could not be decoded; its base color is used."); return null;
                    }
                }
                else
                {
                    // WIC/DX11 rows are top-first; Unity raw texture rows are bottom-first.
                    var bytes = new byte[input.textureRGBA.Length]; int row = input.textureWidth * 4;
                    for (int y = 0; y < input.textureHeight; ++y) for (int x = 0; x < row; ++x) bytes[(input.textureHeight - 1 - y) * row + x] = (byte)input.textureRGBA[y * row + x];
                    // Only mip 0 is stored in the project; Apply builds the remaining mip levels.
                    texture.SetPixelData(bytes, 0); texture.Apply(true, false);
                }
                ctx.AddObjectToAsset("texture-" + index, texture); return texture;
            }
            if (!string.IsNullOrEmpty(input.texturePath))
            {
                // Embedded textures are portable. External fallback is intentionally restricted
                // to existing Unity assets, never arbitrary paths or URLs in an imported file.
                string full = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(ctx.assetPath), input.texturePath));
                string project = Path.GetFullPath(".") + Path.DirectorySeparatorChar;
                if (full.StartsWith(project, StringComparison.OrdinalIgnoreCase))
                {
                    string assetPath = full.Substring(project.Length).Replace('\\', '/');
                    if (assetPath.StartsWith("Assets/", StringComparison.Ordinal) || assetPath.StartsWith("Packages/", StringComparison.Ordinal))
                    {
                        ctx.DependsOnSourceAsset(assetPath); var existing = AssetDatabase.LoadAssetAtPath<Texture2D>(assetPath); if (existing != null) return existing;
                    }
                }
                warnings.Add("External texture unavailable. Embed the image in LunaFlow or copy it beside the project inside Assets.");
            }
            return null;
        }

        public static Matrix4x4 FromRowVectorMatrix(float[] values)
        {
            // LunaFlow writes a row-vector DirectX matrix. Unity uses column vectors.
            Matrix4x4 matrix = new Matrix4x4();
            for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) matrix[row, column] = values[column * 4 + row];
            return matrix;
        }
        private static void BuildMeshes(MeshData[] source, Transform root, Transform[] bones, Material[] materials, AssetImportContext ctx, List<Object> owned)
        {
            var container = new GameObject(GameObjectUtility.GetUniqueNameForSibling(root, "__LunaFlowMeshes")).transform; container.SetParent(root, false);
            for (int i = 0; i < source.Length; ++i)
            {
                var data = source[i]; var mesh = Own(owned, new Mesh { name = string.IsNullOrEmpty(data.name) ? "Mesh " + i : data.name });
                if (data.vertices.Length > 65535) mesh.indexFormat = IndexFormat.UInt32;
                var positions = new Vector3[data.vertices.Length]; var normals = new Vector3[positions.Length]; var uv = new Vector2[positions.Length]; var weights = new BoneWeight[positions.Length];
                for (int j = 0; j < positions.Length; ++j)
                {
                    var v = data.vertices[j]; positions[j] = V3(v.p); normals[j] = V3(v.n); uv[j] = new Vector2(v.uv[0], 1 - v.uv[1]);
                    float sum = v.w[0] + v.w[1] + v.w[2] + v.w[3];
                    weights[j] = new BoneWeight { boneIndex0 = v.j[0], boneIndex1 = v.j[1], boneIndex2 = v.j[2], boneIndex3 = v.j[3], weight0 = v.w[0] / sum, weight1 = v.w[1] / sum, weight2 = v.w[2] / sum, weight3 = v.w[3] / sum };
                }
                var palette = new Transform[data.skin.Length]; var bindposes = new Matrix4x4[palette.Length];
                for (int j = 0; j < palette.Length; ++j) { palette[j] = bones[data.skin[j].bone]; bindposes[j] = FromRowVectorMatrix(data.skin[j].inverseBind); }
                mesh.vertices = positions; mesh.normals = normals; mesh.uv = uv; mesh.boneWeights = weights; mesh.bindposes = bindposes; mesh.triangles = data.indices; mesh.RecalculateBounds();
                var renderer = new GameObject(NodeName(mesh.name)).AddComponent<SkinnedMeshRenderer>(); renderer.transform.SetParent(container, false);
                renderer.sharedMesh = mesh; renderer.sharedMaterial = materials[data.material]; renderer.bones = palette; renderer.rootBone = root; renderer.updateWhenOffscreen = true;
                ctx.AddObjectToAsset("mesh-" + i, mesh);
            }
        }

        private ImportedMotion[] BuildAnimations(ProjectData data, Transform root, Transform[] bones, AssetImportContext ctx, List<Object> owned)
        {
            var result = new ImportedMotion[data.clips.Length]; var usedNames = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < result.Length; ++i)
            {
                var source = data.clips[i]; string name = source.name;
                if (!usedNames.Add(name)) { name += " (" + Key("", source.id).Substring(0, 8) + ")"; while (!usedNames.Add(name)) name += "_"; }
                var clip = Own(owned, new AnimationClip { name = name, frameRate = sampleRate, legacy = false, wrapMode = source.loop ? WrapMode.Loop : WrapMode.ClampForever });
                var tracks = new Dictionary<string, TrackData>(StringComparer.Ordinal); foreach (var track in source.tracks) tracks.Add(track.bonePath, track);
                for (int bone = 0; bone < bones.Length; ++bone)
                {
                    TrackData track; tracks.TryGetValue(data.scene.bones[bone].path, out track);
                    var times = MotionSampler.SampleTimes(source, track, sampleRate); var channels = new Keyframe[10][];
                    for (int c = 0; c < 10; ++c) channels[c] = new Keyframe[times.Length];
                    float[] previous = null;
                    for (int t = 0; t < times.Length; ++t)
                    {
                        var pose = MotionSampler.Sample(track, data.scene.bones[bone].bindLocal, times[t] * source.speed);
                        if (previous != null) { float dot = 0; for (int q = 0; q < 4; ++q) dot += previous[q] * pose.rotation[q]; if (dot < 0) for (int q = 0; q < 4; ++q) pose.rotation[q] = -pose.rotation[q]; }
                        previous = pose.rotation;
                        for (int c = 0; c < 3; ++c) channels[c][t] = new Keyframe((float)times[t], pose.translation[c]);
                        for (int c = 0; c < 4; ++c) channels[c + 3][t] = new Keyframe((float)times[t], pose.rotation[c]);
                        for (int c = 0; c < 3; ++c) channels[c + 7][t] = new Keyframe((float)times[t], pose.scale[c]);
                    }
                    string path = AnimationUtility.CalculateTransformPath(bones[bone], root);
                    var properties = new[] { "m_LocalPosition.x", "m_LocalPosition.y", "m_LocalPosition.z", "m_LocalRotation.x", "m_LocalRotation.y", "m_LocalRotation.z", "m_LocalRotation.w", "m_LocalScale.x", "m_LocalScale.y", "m_LocalScale.z" };
                    for (int c = 0; c < 10; ++c)
                    {
                        var curve = new AnimationCurve(channels[c]);
                        for (int key = 0; key < curve.length; ++key) { AnimationUtility.SetKeyLeftTangentMode(curve, key, AnimationUtility.TangentMode.Linear); AnimationUtility.SetKeyRightTangentMode(curve, key, AnimationUtility.TangentMode.Linear); }
                        AnimationUtility.SetEditorCurve(clip, EditorCurveBinding.FloatCurve(path, typeof(Transform), properties[c]), curve);
                    }
                }
                clip.EnsureQuaternionContinuity();
                var settings = AnimationUtility.GetAnimationClipSettings(clip); settings.loopTime = source.loop; settings.stopTime = (float)(source.duration / source.speed); AnimationUtility.SetAnimationClipSettings(clip, settings);
                result[i] = new ImportedMotion { id = source.id, displayName = source.name, stateName = Key("motion_", source.id), clip = clip, duration = (float)(source.duration / source.speed), loop = source.loop };
                ctx.AddObjectToAsset(Key("clip-", source.id), clip);
            }
            return result;
        }

        private static AnimatorController BuildController(ImportedMotion[] motions, string defaultClipId, AssetImportContext ctx, List<Object> owned)
        {
            var controller = Own(owned, new AnimatorController { name = "LunaFlow Animator" });
            var machine = Own(owned, new AnimatorStateMachine { name = "Base Layer" });
            controller.AddLayer(new AnimatorControllerLayer { name = "Base Layer", defaultWeight = 1, stateMachine = machine });
            foreach (var motion in motions)
            {
                var state = Own(owned, new AnimatorState { name = motion.stateName, motion = motion.clip, writeDefaultValues = true });
                machine.AddState(state, Vector3.zero); if (machine.defaultState == null || motion.id == defaultClipId) machine.defaultState = state;
                ctx.AddObjectToAsset(Key("state-", motion.id), state);
            }
            ctx.AddObjectToAsset("state-machine", machine); ctx.AddObjectToAsset("controller", controller); return controller;
        }
    }
}
