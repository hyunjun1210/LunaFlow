using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace LunaFlow.UnityImporter.Editor
{
    [CustomEditor(typeof(LunaFlowProjectImporter))]
    public sealed class LunaFlowImporterInspector : ScriptedImporterEditor
    {
        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            EditorGUILayout.LabelField("LunaFlow 프로젝트", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(serializedObject.FindProperty("sampleRate"), new GUIContent("애니메이션 샘플링 FPS"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("importEffects"), new GUIContent("VFX 가져오기"));
            EditorGUILayout.PropertyField(serializedObject.FindProperty("materialShader"), new GUIContent("모델 셰이더", "비워 두면 Built-in/URP 셰이더를 자동 선택합니다."));
            ApplyRevertGUI();
            var importer = (LunaFlowProjectImporter)target;
            var root = AssetDatabase.LoadAssetAtPath<GameObject>(importer.assetPath);
            if (root == null) return;
            var player = root.GetComponent<LunaFlowPlayer>(); var project = player == null ? null : player.Project;
            if (project == null) return;
            EditorGUILayout.Space(); EditorGUILayout.LabelField("가져온 데이터", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("뼈 / 메시 / VFX", project.boneCount + " / " + project.meshCount + " / " + project.effectCount);
            foreach (var motion in project.motions)
            {
                EditorGUILayout.ObjectField(motion.displayName + " (" + motion.duration.ToString("0.###") + "s)", motion.clip, typeof(AnimationClip), false);
            }
            foreach (string warning in project.warnings) EditorGUILayout.HelpBox(warning, MessageType.Warning);
            EditorGUILayout.HelpBox("에셋을 씬에 드래그하거나 아래 버튼으로 배치하세요. 원본 파일을 수정하면 자동으로 다시 가져옵니다.", MessageType.Info);
            if (GUILayout.Button("현재 씬에 배치"))
            {
                var instance = PrefabUtility.InstantiatePrefab(root) as GameObject; Undo.RegisterCreatedObjectUndo(instance, "Add LunaFlow project"); Selection.activeGameObject = instance;
            }
        }
    }

    [CustomEditor(typeof(LunaFlowPlayer))]
    public sealed class LunaFlowPlayerInspector : UnityEditor.Editor
    {
        private int selected;
        public override void OnInspectorGUI()
        {
            DrawDefaultInspector(); var player = (LunaFlowPlayer)target;
            if (player.Project == null || player.Project.motions == null || player.Project.motions.Length == 0) return;
            var motions = player.Project.motions; var labels = new string[motions.Length]; for (int i = 0; i < labels.Length; ++i) labels[i] = motions[i].displayName;
            selected = EditorGUILayout.Popup("재생할 동작", Mathf.Clamp(selected, 0, labels.Length - 1), labels);
            using (new EditorGUI.DisabledScope(!Application.isPlaying))
            {
                if (GUILayout.Button("선택 동작 재생")) player.Play(motions[selected].id);
                using (new EditorGUILayout.HorizontalScope())
                {
                    if (GUILayout.Button("일시 정지")) player.Pause();
                    if (GUILayout.Button("계속 재생")) player.Resume();
                }
                if (Application.isPlaying)
                {
                    float time = player.CurrentTime;
                    EditorGUI.BeginChangeCheck(); time = EditorGUILayout.Slider("시간", time, 0, motions[selected].duration);
                    if (EditorGUI.EndChangeCheck()) player.Seek(time);
                }
            }
            if (!Application.isPlaying) EditorGUILayout.HelpBox("Play 모드에서 동작을 선택하고 재생·시간 이동을 할 수 있습니다.", MessageType.Info);
        }
    }
}
