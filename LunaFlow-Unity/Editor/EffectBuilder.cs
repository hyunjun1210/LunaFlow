using System;
using System.Collections.Generic;
using LunaFlow.UnityImporter.Format;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace LunaFlow.UnityImporter.Editor
{
    internal static class EffectBuilder
    {
        public static LunaFlowEffect[] Build(EffectData[] source, Transform[] bones, AssetImportContext ctx, List<Object> owned, List<string> warnings)
        {
            if (source == null || source.Length == 0) return Array.Empty<LunaFlowEffect>();
            Shader shader = Shader.Find("LunaFlow/Imported Particle");
            if (shader == null)
                foreach (string guid in AssetDatabase.FindAssets("LunaFlowParticle t:Shader"))
                {
                    string path = AssetDatabase.GUIDToAssetPath(guid); shader = AssetDatabase.LoadAssetAtPath<Shader>(path);
                    if (shader != null) { ctx.DependsOnSourceAsset(path); break; }
                }
            if (shader == null) shader = Shader.Find("Sprites/Default");
            if (shader == null) throw new ProjectFormatException("A particle shader is required to import effects.");
            string shaderPath = AssetDatabase.GetAssetPath(shader);
            if (shaderPath.StartsWith("Assets/", StringComparison.Ordinal) || shaderPath.StartsWith("Packages/", StringComparison.Ordinal)) ctx.DependsOnSourceAsset(shaderPath);
            var texture = LunaFlowProjectImporter.Own(owned, new Texture2D(32, 32, TextureFormat.RGBA32, false) { name = "LunaFlow Soft Particle", wrapMode = TextureWrapMode.Clamp });
            var pixels = new Color[32 * 32];
            for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            {
                float dx = (x + .5f) / 16 - 1, dy = (y + .5f) / 16 - 1, alpha = Mathf.Clamp01(1 - dx * dx - dy * dy);
                pixels[y * 32 + x] = new Color(1, 1, 1, alpha * alpha);
            }
            texture.SetPixels(pixels); texture.Apply(); ctx.AddObjectToAsset("particle-texture", texture);
            var alphaMaterial = Material(shader, texture, false, owned); var additiveMaterial = Material(shader, texture, true, owned);
            ctx.AddObjectToAsset("particle-alpha", alphaMaterial); ctx.AddObjectToAsset("particle-additive", additiveMaterial);
            var result = new List<LunaFlowEffect>(); bool legacy = false;
            foreach (var input in source)
            {
                Transform parent = bones[input.bone];
                string name = GameObjectUtility.GetUniqueNameForSibling(parent, "VFX - " + LunaFlowProjectImporter.NodeName(input.name ?? input.id));
                var node = new GameObject(name); node.transform.SetParent(parent, false); LunaFlowProjectImporter.ApplyTransform(node.transform, input.local);
                ParticleSystem system = null; TrailRenderer trail = null;
                if (input.type == 0)
                {
                    legacy = true;
                    trail = node.AddComponent<TrailRenderer>(); trail.sharedMaterial = additiveMaterial; trail.time = .3f;
                    trail.startWidth = Mathf.Max(.002f, input.size * .12f); trail.endWidth = 0; trail.minVertexDistance = .005f; trail.emitting = false;
                    var c = LunaFlowProjectImporter.Color4(input.color); c.r *= input.intensity; c.g *= input.intensity; c.b *= input.intensity;
                    trail.startColor = c; c.a = 0; trail.endColor = c; trail.shadowCastingMode = ShadowCastingMode.Off;
                }
                else
                {
                    ParticleData p = input.type == 3 ? input.particles : Legacy(input);
                    if (input.type != 3) legacy = true;
                    system = node.AddComponent<ParticleSystem>(); system.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
                    var main = system.main; main.playOnAwake = false; main.duration = p.duration; main.loop = p.loop;
                    main.startLifetime = new ParticleSystem.MinMaxCurve(p.lifetime * (1 - p.lifetimeRandomness), p.lifetime);
                    main.startSpeed = new ParticleSystem.MinMaxCurve(p.speed * (1 - p.speedRandomness), p.speed);
                    main.startSize = p.startSize * input.size; main.maxParticles = p.maxParticles; main.gravityModifier = p.gravity;
                    main.simulationSpace = p.worldSpace ? ParticleSystemSimulationSpace.World : ParticleSystemSimulationSpace.Local;
                    main.startColor = Color.white; main.cullingMode = ParticleSystemCullingMode.AlwaysSimulate;
                    system.useAutoRandomSeed = false; system.randomSeed = (uint)p.seed;
                    var emission = system.emission; emission.enabled = true; emission.rateOverTime = p.rate;
                    if (p.burst > 0) emission.SetBursts(new[] { new ParticleSystem.Burst(0, (short)p.burst) });
                    var shape = system.shape; shape.enabled = true; shape.radius = p.radius; shape.angle = p.angle;
                    shape.shapeType = p.shape == 1 ? ParticleSystemShapeType.Sphere : p.shape == 2 ? ParticleSystemShapeType.Box : p.shape == 3 ? ParticleSystemShapeType.Circle : ParticleSystemShapeType.Cone;
                    if (p.shape == 2) shape.scale = LunaFlowProjectImporter.V3(p.box);
                    var velocity = system.velocityOverLifetime; velocity.enabled = true; velocity.space = ParticleSystemSimulationSpace.Local;
                    velocity.x = p.velocity[0]; velocity.y = p.velocity[1]; velocity.z = p.velocity[2];
                    var colors = system.colorOverLifetime; colors.enabled = true;
                    Color from = LunaFlowProjectImporter.Color4(input.color), to = LunaFlowProjectImporter.Color4(p.endColor);
                    from.r *= input.intensity; from.g *= input.intensity; from.b *= input.intensity; to.r *= input.intensity; to.g *= input.intensity; to.b *= input.intensity;
                    var gradient = new Gradient(); gradient.SetKeys(new[] { new GradientColorKey(from, 0), new GradientColorKey(to, 1) }, new[] { new GradientAlphaKey(from.a, 0), new GradientAlphaKey(to.a, 1) }); colors.color = gradient;
                    var size = system.sizeOverLifetime; size.enabled = true; size.size = new ParticleSystem.MinMaxCurve(1, AnimationCurve.Linear(0, 1, 1, p.endSize / p.startSize));
                    var renderer = system.GetComponent<ParticleSystemRenderer>(); renderer.sharedMaterial = p.additive ? additiveMaterial : alphaMaterial; renderer.renderMode = ParticleSystemRenderMode.Billboard; renderer.shadowCastingMode = ShadowCastingMode.Off;
                }
                var effect = node.AddComponent<LunaFlowEffect>(); effect.Configure(input.clipId, (float)input.start, (float)input.end, input.enabled, system, trail); effect.Clear(); result.Add(effect);
            }
            if (legacy) warnings.Add("Legacy Trail/Sparks/Aura are approximated with Unity TrailRenderer/ParticleSystem components.");
            return result.ToArray();
        }
        private static Material Material(Shader shader, Texture2D texture, bool additive, List<Object> owned)
        {
            var material = LunaFlowProjectImporter.Own(owned, new Material(shader) { name = additive ? "LunaFlow Additive" : "LunaFlow Alpha", mainTexture = texture, renderQueue = 3000 });
            if (material.HasProperty("_SrcBlend")) material.SetInt("_SrcBlend", (int)BlendMode.SrcAlpha);
            if (material.HasProperty("_DstBlend")) material.SetInt("_DstBlend", (int)(additive ? BlendMode.One : BlendMode.OneMinusSrcAlpha));
            return material;
        }
        private static ParticleData Legacy(EffectData effect)
        {
            bool sparks = effect.type == 1;
            return new ParticleData
            {
                shape = 1, duration = 1, rate = sparks ? 120 : 40, lifetime = sparks ? .25f : 1,
                speed = sparks ? 1.6f : .1f, radius = sparks ? .03f : .6f, startSize = .04f, endSize = 0,
                maxParticles = 512, seed = 1, loop = true, additive = true,
                box = new[] { 1f, 1f, 1f }, velocity = new float[3], endColor = new[] { effect.color[0], effect.color[1], effect.color[2], 0f }
            };
        }
    }
}
