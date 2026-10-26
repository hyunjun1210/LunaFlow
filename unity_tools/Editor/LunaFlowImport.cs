using System;
using System.IO;
using System.Linq;
using LunaFlow.Runtime;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace LunaFlow.Editor
{
    public sealed class LunaFlowImport : AssetPostprocessor
    {
        private static string ManifestPath(string fbx)
        {
            string preferred = Path.ChangeExtension(fbx, ".lunaflow.json").Replace('\\', '/');
            if (File.Exists(preferred)) return preferred;
            string folder = Path.GetDirectoryName(fbx);
            if (string.IsNullOrEmpty(folder) || !Directory.Exists(folder)) return preferred;
            foreach (string candidate in Directory.GetFiles(folder, "*.json"))
            {
                try { var data = JsonUtility.FromJson<SceneManifest>(File.ReadAllText(candidate)); if (data != null && data.format == "lunaflow.unity" && data.fbx == Path.GetFileName(fbx)) return candidate.Replace('\\', '/'); }
                catch { }
            }
            return preferred;
        }
        private static SceneManifest Read(string path)
        {
            if (!File.Exists(path)) return null;
            try { var data = JsonUtility.FromJson<SceneManifest>(File.ReadAllText(path)); return data != null && data.format == "lunaflow.unity" && data.version == 1 ? data : null; }
            catch (Exception error) { Debug.LogError("LunaFlow manifest: " + error.Message); return null; }
        }
        private void OnPreprocessModel()
        {
            if (Read(ManifestPath(assetPath)) == null) return;
            var model = (ModelImporter)assetImporter;
            model.animationType = ModelImporterAnimationType.Generic;
            model.avatarSetup = ModelImporterAvatarSetup.CreateFromThisModel;
            model.importAnimation = true; model.resampleCurves = true;
            model.globalScale = 1; model.useFileScale = true; model.preserveHierarchy = true;
        }
        private void OnPreprocessAnimation()
        {
            var data = Read(ManifestPath(assetPath)); if (data == null) return;
            var model = (ModelImporter)assetImporter;
            var clips = model.defaultClipAnimations;
            for (int i = 0; i < clips.Length; ++i)
            {
                var info = (data.clips ?? Array.Empty<ClipInfo>()).FirstOrDefault(c => c.name == clips[i].name || c.name == clips[i].takeName);
                if (info == null && data.clips != null && i < data.clips.Length) info = data.clips[i];
                if (info != null) { clips[i].name = info.name; clips[i].loopTime = info.loop; }
            }
            model.clipAnimations = clips;
        }
        private static void OnPostprocessAllAssets(string[] imported, string[] deleted, string[] moved, string[] movedFrom)
        {
            foreach (string path in imported)
            {
                if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                {
                    var metadata = Read(path);
                    if (metadata != null && !string.IsNullOrEmpty(metadata.fbx))
                    {
                        string fbx = Path.Combine(Path.GetDirectoryName(path), Path.GetFileName(metadata.fbx)).Replace('\\', '/');
                        if (File.Exists(fbx)) EditorApplication.delayCall += () => { AssetDatabase.ImportAsset(fbx, ImportAssetOptions.ForceUpdate); BuildPrefab(fbx); };
                    }
                    continue;
                }
                if (!path.EndsWith(".fbx", StringComparison.OrdinalIgnoreCase) || Read(ManifestPath(path)) == null) continue;
                string target = path;
                EditorApplication.delayCall += () => BuildPrefab(target);
            }
        }
        [MenuItem("LunaFlow/Rebuild selected scene prefab")]
        private static void RebuildSelected()
        {
            string path = AssetDatabase.GetAssetPath(Selection.activeObject);
            if (path.EndsWith(".fbx", StringComparison.OrdinalIgnoreCase)) BuildPrefab(path);
            else Debug.Log("Select a LunaFlow FBX in the Project window.");
        }
        public static void BuildPrefab(string path)
        {
            string manifestPath = ManifestPath(path); var data = Read(manifestPath); if (data == null) return;
            var model = AssetDatabase.LoadAssetAtPath<GameObject>(path); var text = AssetDatabase.LoadAssetAtPath<TextAsset>(manifestPath);
            if (model == null || text == null) return;
            string controllerPath = Path.ChangeExtension(path, ".controller").Replace('\\', '/');
            var controller = AssetDatabase.LoadAssetAtPath<AnimatorController>(controllerPath);
            if (controller == null) controller = AnimatorController.CreateAnimatorControllerAtPath(controllerPath);
            var machine = controller.layers[0].stateMachine;
            foreach (var state in machine.states) machine.RemoveState(state.state);
            var clips = AssetDatabase.LoadAllAssetsAtPath(path).OfType<AnimationClip>().Where(c => !c.name.StartsWith("__preview__", StringComparison.Ordinal)).ToArray();
            var defaultClip = (data.clips ?? Array.Empty<ClipInfo>()).FirstOrDefault(c => c.id == data.defaultClipId);
            foreach (var clip in clips)
            {
                var state = machine.AddState(clip.name); state.motion = clip;
                if (machine.states.Length == 1 || (defaultClip != null && clip.name == defaultClip.name)) machine.defaultState = state;
            }
            var instance = (GameObject)PrefabUtility.InstantiatePrefab(model);
            try
            {
                var animator = instance.GetComponent<Animator>() ?? instance.AddComponent<Animator>();
                animator.runtimeAnimatorController = controller; animator.applyRootMotion = false;
                var runtime = instance.GetComponent<LunaFlowScene>() ?? instance.AddComponent<LunaFlowScene>();
                runtime.SetManifest(text);
                string prefabPath = Path.ChangeExtension(path, ".prefab").Replace('\\', '/');
                PrefabUtility.SaveAsPrefabAsset(instance, prefabPath);
                EditorUtility.SetDirty(controller); AssetDatabase.SaveAssets();
                Debug.Log("LunaFlow prefab ready: " + prefabPath);
            }
            finally { UnityEngine.Object.DestroyImmediate(instance); }
        }
    }
}
