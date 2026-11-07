using UnityEngine;

namespace LunaFlow.UnityImporter
{
    [DisallowMultipleComponent, RequireComponent(typeof(Animator))]
    public sealed class LunaFlowPlayer : MonoBehaviour
    {
        [SerializeField] private LunaFlowProjectAsset project;
        [SerializeField] private LunaFlowEffect[] effects;
        private Animator animator;
        private ImportedMotion current;
        private bool paused;
        private float seekTime = -1;
        public LunaFlowProjectAsset Project { get { return project; } }
        public string CurrentClipId { get { RefreshMotion(); return current == null ? "" : current.id; } }
        public float CurrentTime
        {
            get
            {
                RefreshMotion(); if (current == null || animator == null || !animator.isInitialized) return 0;
                if (paused && seekTime >= 0) return seekTime;
                float normalized = animator.GetCurrentAnimatorStateInfo(0).normalizedTime;
                return (current.loop ? Mathf.Repeat(normalized, 1) : Mathf.Clamp01(normalized)) * current.duration;
            }
        }

        public void Configure(LunaFlowProjectAsset asset, LunaFlowEffect[] importedEffects) { project = asset; effects = importedEffects; }
        private void Awake() { animator = GetComponent<Animator>(); }
        private void Start() { if (project != null && !string.IsNullOrEmpty(project.defaultClipId)) Play(project.defaultClipId); }
        public bool Play(string clipIdOrName)
        {
            if (project == null || project.motions == null) return false;
            foreach (var motion in project.motions)
            {
                if (motion.id != clipIdOrName && motion.displayName != clipIdOrName) continue;
                animator = GetComponent<Animator>(); current = motion; paused = false; seekTime = -1; animator.speed = 1;
                animator.Play(Animator.StringToHash("Base Layer." + motion.stateName), 0, 0); animator.Update(0);
                UpdateEffects(true); return true;
            }
            return false;
        }
        public void Pause() { paused = true; if (animator == null) animator = GetComponent<Animator>(); animator.speed = 0; }
        public void Resume() { paused = false; seekTime = -1; if (animator == null) animator = GetComponent<Animator>(); animator.speed = 1; }
        public void Seek(float time)
        {
            RefreshMotion(); if (current == null) return; Pause();
            seekTime = Mathf.Clamp(time, 0, current.duration);
            float normalized = current.duration > 0 ? seekTime / current.duration : 0;
            animator.Play(Animator.StringToHash("Base Layer." + current.stateName), 0, normalized); animator.Update(0);
            // Sampling explicitly keeps the last pose visible when seeking to the end of a looping clip.
            current.clip.SampleAnimation(gameObject, seekTime); UpdateEffects(true);
        }
        private void RefreshMotion()
        {
            if (animator == null) animator = GetComponent<Animator>();
            if (project == null || project.motions == null) return;
            if (animator.isInitialized)
            {
                int hash = animator.GetCurrentAnimatorStateInfo(0).fullPathHash;
                foreach (var motion in project.motions) if (hash == Animator.StringToHash("Base Layer." + motion.stateName)) { current = motion; return; }
            }
            if (current == null && project.motions.Length > 0) current = project.motions[0];
        }
        private void LateUpdate()
        {
            if (paused && seekTime >= 0 && current != null) current.clip.SampleAnimation(gameObject, seekTime);
            UpdateEffects(false);
        }
        private void UpdateEffects(bool forceRestart)
        {
            RefreshMotion(); if (effects == null || current == null) return;
            float time = CurrentTime;
            foreach (var effect in effects) if (effect != null) effect.Sample(current.id, time, paused, forceRestart);
        }
        private void OnDisable() { if (effects != null) foreach (var effect in effects) if (effect != null) effect.Clear(); }
    }
}
