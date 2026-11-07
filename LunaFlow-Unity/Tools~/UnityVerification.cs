// Copied into an isolated test project's Assets/Editor by Test-Unity.ps1.
// Deliberately outside the installed package's compiled assemblies.
using System;
using System.IO;
using System.Linq;
using LunaFlow.UnityImporter;
using LunaFlow.UnityImporter.Editor;
using LunaFlow.UnityImporter.Format;
using UnityEditor;
using PackageInfo = UnityEditor.PackageManager.PackageInfo;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

[InitializeOnLoad]
public static class LunaFlowUnityVerification
{
    private static int checks;
    private const string Pending = "LunaFlow.UnityVerification.Pending";
    private static LunaFlowPlayer livePlayer;
    private static int playPhase, phaseFrame;
    private static double playDeadline;
    static LunaFlowUnityVerification() { EditorApplication.playModeStateChanged += PlayStateChanged; }
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception("LunaFlow verification: " + message);
        ++checks; Debug.Log("LUNAFLOW_CHECK " + message);
    }
    private static bool Near(Vector3 a, Vector3 b) { return Vector3.Distance(a, b) < .003f; }

    public static void Run()
    {
        try
        {
            string package = PackageInfo.FindForAssembly(typeof(LunaFlowProjectImporter).Assembly).resolvedPath;
            Directory.CreateDirectory("Assets/LunaFlowVerification");
            string path = "Assets/LunaFlowVerification/Wave.lunaflow";
            File.Copy(Path.Combine(package, "Samples~/AnimatedSample/Wave.lunaflow"), path, true);
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate);
            var initialOptions = (LunaFlowProjectImporter)AssetImporter.GetAtPath(path);
            initialOptions.importEffects = true; initialOptions.sampleRate = 30; EditorUtility.SetDirty(initialOptions); initialOptions.SaveAndReimport();
            var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(path);
            Check(prefab != null, "main asset is a prefab");
            var metadata = prefab.GetComponent<LunaFlowPlayer>().Project;
            Check(metadata.boneCount == 3 && metadata.meshCount == 2 && metadata.effectCount == 1, "project metadata");
            Check(metadata.warnings.Length == 0, "sample imports without warnings");
            Check(metadata.motions.Length == 2 && metadata.defaultClipId == "wave", "clips and default motion");
            var wave = metadata.motions.Single(m => m.id == "wave");
            var idle = metadata.motions.Single(m => m.id == "idle");
            Check(Mathf.Abs(wave.clip.length - 1) < .001f && Mathf.Abs(idle.clip.length - 2) < .001f, "clip speed is baked into duration");
            Check(AnimationUtility.GetAnimationClipSettings(wave.clip).loopTime, "loop flag");
            Check(prefab.GetComponent<Animator>().avatar != null && prefab.GetComponent<Animator>().avatar.isValid, "Generic Avatar");
            var instance = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            try
            {
                var arm = instance.transform.Find("Root/Arm"); var hand = arm.Find("Hand");
                var meshes = instance.GetComponentsInChildren<SkinnedMeshRenderer>();
                var armMesh = meshes.Single(m => m.sharedMesh.name == "Arm mesh");
                Check(armMesh.bones.Length == 1 && armMesh.bones[0] == arm, "mesh-local skin palette maps to global bones");
                Check(Near(armMesh.sharedMesh.bindposes[0].MultiplyPoint3x4(new Vector3(0, 1, 0)), Vector3.zero), "DirectX inverse bind matrix transposed");
                var checker = (Texture2D)meshes.Single(m => m.sharedMesh.name == "Body").sharedMaterial.mainTexture;
                Check(checker.GetPixel(0, 0).b > .99f && checker.GetPixel(0, 0).r < .01f, "top-first RGBA texture orientation");
                Check(meshes[0].sharedMesh.uv[0].y == 0, "UV origin conversion");
                wave.clip.SampleAnimation(instance, .25f);
                Check(Quaternion.Angle(arm.localRotation, Quaternion.Euler(0, 0, 45)) < .15f, "quaternion pose at 2x speed");
                Check(Near(hand.position, new Vector3(.353553f, 1.353553f, 0)), "bone hierarchy animation");
                var baked = new Mesh();
                try
                {
                    armMesh.BakeMesh(baked);
                    Check(Near(baked.vertices[2], new Vector3(.353553f, 1.494975f, 0)), "CPU skin deformation");
                }
                finally { Object.DestroyImmediate(baked); }
                idle.clip.SampleAnimation(instance, .5f);
                Check(Quaternion.Angle(arm.localRotation, Quaternion.identity) < .01f, "untracked channels reset bind pose on clip switch");
                var effect = instance.GetComponentInChildren<LunaFlowEffect>();
                var particles = effect.GetComponent<ParticleSystem>();
                Check(effect.transform.parent == hand && particles.main.maxParticles == 64 && particles.randomSeed == 1, "bone-attached ParticleSystem settings");
                effect.Sample("wave", .2f, true, true);
                Check(particles.particleCount > 0 && particles.isPaused, "particle timeline simulation");
                effect.Sample("idle", .2f, true, false);
                Check(particles.particleCount == 0, "effects gated by clip");
                effect.Sample("wave", 1.5f, true, true);
                Check(particles.particleCount == 0, "effects gated by time range");
                var player = instance.GetComponent<LunaFlowPlayer>(); instance.GetComponent<Animator>().Rebind();
                Check(player.Play("wave"), "runtime Play by ID"); player.Seek(.25f);
                Check(Mathf.Abs(player.CurrentTime - .25f) < .001f && Quaternion.Angle(arm.localRotation, Quaternion.Euler(0, 0, 45)) < .15f, "runtime seek");
                player.Seek(1); Check(Mathf.Abs(player.CurrentTime - 1) < .001f, "seek to looping clip endpoint");
                Check(player.Play("Idle") && player.CurrentClipId == "idle", "runtime Play by display name");
            }
            finally { Object.DestroyImmediate(instance); }

            string guid; long fileId; AssetDatabase.TryGetGUIDAndLocalFileIdentifier(wave.clip, out guid, out fileId);
            AssetDatabase.ImportAsset(path, ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate);
            var reimported = AssetDatabase.LoadAssetAtPath<GameObject>(path).GetComponent<LunaFlowPlayer>().Project.motions.Single(m => m.id == "wave").clip;
            string newGuid; long newId; AssetDatabase.TryGetGUIDAndLocalFileIdentifier(reimported, out newGuid, out newId);
            Check(newGuid == guid && newId == fileId, "stable animation reference across reimport");

            var importer = (LunaFlowProjectImporter)AssetImporter.GetAtPath(path); importer.importEffects = false; EditorUtility.SetDirty(importer); importer.SaveAndReimport();
            Check(AssetDatabase.LoadAssetAtPath<GameObject>(path).GetComponentsInChildren<LunaFlowEffect>().Length == 0 && AssetDatabase.LoadAssetAtPath<GameObject>(path).GetComponent<LunaFlowPlayer>().Project.effectCount == 0, "optional effects import");
            importer = (LunaFlowProjectImporter)AssetImporter.GetAtPath(path);
            importer.importEffects = true; EditorUtility.SetDirty(importer); importer.SaveAndReimport();
            var bad = JsonUtility.FromJson<ProjectData>(File.ReadAllText(path)); bad.scene.bones[0].parent = 2;
            try { ProjectValidator.Validate(bad); throw new Exception("cycle accepted"); }
            catch (ProjectFormatException) { ++checks; }

            string golden = Path.Combine(package, "Tools~/Fixtures/EditorSnapshot.lunaflow");
            if (File.Exists(golden))
            {
                string goldenPath = "Assets/LunaFlowVerification/EditorSnapshot.lunaflow";
                File.Copy(golden, goldenPath, true); AssetDatabase.ImportAsset(goldenPath, ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate);
                var original = AssetDatabase.LoadAssetAtPath<GameObject>(goldenPath);
                Check(original != null && original.GetComponent<LunaFlowPlayer>().Project.meshCount == 17, "C++ editor saved project imports");
                Check(original.GetComponentsInChildren<ParticleSystem>().Length == 1, "C++ editor saved VFX imports");
            }
            SessionState.SetInt(Pending + ".Checks", checks); SessionState.SetBool(Pending, true);
            EditorApplication.EnterPlaymode();
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    private static void Fail(Exception e)
    {
        SessionState.SetBool(Pending, false); EditorApplication.update -= PlayUpdate;
        Debug.LogException(e); File.WriteAllText("LunaFlowVerification-result.json", "{\"success\":false}"); EditorApplication.Exit(1);
    }
    private static void PlayStateChanged(PlayModeStateChange state)
    {
        if (state != PlayModeStateChange.EnteredPlayMode || !SessionState.GetBool(Pending, false)) return;
        try
        {
            checks = SessionState.GetInt(Pending + ".Checks", 0);
            livePlayer = Object.Instantiate(AssetDatabase.LoadAssetAtPath<GameObject>("Assets/LunaFlowVerification/Wave.lunaflow")).GetComponent<LunaFlowPlayer>();
            Time.timeScale = .1f; playPhase = 0; phaseFrame = Time.frameCount; playDeadline = EditorApplication.timeSinceStartup + 40;
            EditorApplication.update += PlayUpdate;
        }
        catch (Exception e) { Fail(e); }
    }
    private static void PlayUpdate()
    {
        try
        {
            if (EditorApplication.timeSinceStartup > playDeadline) throw new Exception("Play mode verification timed out.");
            if (Time.frameCount <= phaseFrame + 3) return;
            var arm = livePlayer.transform.Find("Root/Arm");
            var particles = livePlayer.GetComponentInChildren<ParticleSystem>();
            if (playPhase == 0)
            {
                Check(livePlayer.CurrentClipId == "wave" && livePlayer.CurrentTime > 0, "Play mode automatically advances the default animation");
                Check(Quaternion.Angle(arm.localRotation, Quaternion.identity) > .01f, "Play mode Animator changes bone pose");
                Check(particles.particleCount > 0, "Play mode particles follow animation clock");
                livePlayer.Seek(0);
            }
            else if (playPhase == 1)
            {
                Check(livePlayer.CurrentTime == 0, "Play mode seek pauses the clock");
                if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Null) Render(livePlayer.gameObject, 0, "LunaFlowVerification-before.png");
                livePlayer.Seek(.25f);
            }
            else if (playPhase == 2)
            {
                Check(Quaternion.Angle(arm.localRotation, Quaternion.Euler(0, 0, 45)) < .15f, "Play mode paused seek keeps the requested pose");
                if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Null) Render(livePlayer.gameObject, .25f, "LunaFlowVerification-after.png");
                livePlayer.Resume();
            }
            else if (playPhase == 3)
            {
                Check(livePlayer.CurrentTime > .25f, "Play mode resume advances time");
                livePlayer.Play("idle");
            }
            else
            {
                Check(Quaternion.Angle(arm.localRotation, Quaternion.identity) < .01f && particles.particleCount == 0, "Play mode clip switch resets pose and VFX");
                SessionState.SetBool(Pending, false); EditorApplication.update -= PlayUpdate;
                File.WriteAllText("LunaFlowVerification-result.json", "{\"success\":true,\"checks\":" + checks + ",\"unity\":\"" + Application.unityVersion + "\"}");
                Debug.Log("LUNAFLOW_UNITY_PASS " + checks); EditorApplication.Exit(0); return;
            }
            ++playPhase; phaseFrame = Time.frameCount;
        }
        catch (Exception e) { Fail(e); }
    }

    private static void Render(GameObject instance, float time, string filename)
    {
        var cameraObject = new GameObject("Verification camera"); var lightObject = new GameObject("Verification light");
        var target = new RenderTexture(512, 512, 24); var image = new Texture2D(512, 512, TextureFormat.RGB24, false);
        var previous = RenderTexture.active;
        try
        {
            var effect = instance.GetComponentInChildren<LunaFlowEffect>(); effect.Sample("wave", time, true, true);
            var camera = cameraObject.AddComponent<Camera>(); camera.transform.position = new Vector3(0, .8f, -4);
            camera.transform.LookAt(new Vector3(0, .8f, 0)); camera.orthographic = true; camera.orthographicSize = 1.1f;
            camera.clearFlags = CameraClearFlags.SolidColor; camera.backgroundColor = new Color(.045f, .05f, .075f); camera.targetTexture = target;
            var light = lightObject.AddComponent<Light>(); light.type = LightType.Directional; light.intensity = 1.3f; light.transform.rotation = Quaternion.Euler(30, -20, 0);
            camera.Render(); RenderTexture.active = target; image.ReadPixels(new Rect(0, 0, 512, 512), 0, 0); image.Apply();
            File.WriteAllBytes(filename, image.EncodeToPNG());
            var pixels = image.GetPixels(); Check(pixels.Count(c => c.r > .2f || c.g > .2f || c.b > .2f) > 1000, "rendered sample " + time);
        }
        finally
        {
            RenderTexture.active = previous; target.Release(); Object.DestroyImmediate(image); Object.DestroyImmediate(target);
            Object.DestroyImmediate(cameraObject); Object.DestroyImmediate(lightObject);
        }
    }
}
