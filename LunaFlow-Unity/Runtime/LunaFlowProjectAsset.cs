using System;
using UnityEngine;

namespace LunaFlow.UnityImporter
{
    [Serializable] public sealed class ImportedMotion
    {
        public string id, displayName, stateName;
        public AnimationClip clip;
        public float duration;
        public bool loop;
    }

    public sealed class LunaFlowProjectAsset : ScriptableObject
    {
        public int schemaVersion, boneCount, meshCount, effectCount;
        public string defaultClipId;
        public ImportedMotion[] motions;
        public string[] warnings;
    }
}
