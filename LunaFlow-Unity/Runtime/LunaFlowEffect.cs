using UnityEngine;

namespace LunaFlow.UnityImporter
{
    public sealed class LunaFlowEffect : MonoBehaviour
    {
        [SerializeField] private string clipId;
        [SerializeField] private float startTime, endTime;
        [SerializeField] private bool sourceEnabled;
        [SerializeField] private ParticleSystem particles;
        [SerializeField] private TrailRenderer trail;
        private float previous = -1;
        private string previousClip;
        public void Configure(string motionId, float start, float end, bool enabledInSource, ParticleSystem system, TrailRenderer renderer)
        {
            clipId = motionId; startTime = start; endTime = end; sourceEnabled = enabledInSource; particles = system; trail = renderer;
        }
        public void Sample(string activeClip, float time, bool paused, bool restart)
        {
            bool active = sourceEnabled && (string.IsNullOrEmpty(clipId) || activeClip == clipId) && time >= startTime && time <= endTime;
            if (!active) { Clear(); return; }
            float local = time - startTime;
            bool reset = restart || previous < 0 || previousClip != activeClip || local < previous || local - previous > .25f;
            if (particles != null)
            {
                if (reset) particles.Simulate(local, true, true, false);
                else if (local > previous) particles.Simulate(local - previous, true, false, false);
                particles.Pause(true); // The Animator clock owns simulation; don't advance twice.
            }
            if (trail != null) { if (reset) trail.Clear(); trail.emitting = !paused; }
            previous = local; previousClip = activeClip;
        }
        public void Clear()
        {
            if (particles != null) particles.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            if (trail != null) { trail.emitting = false; trail.Clear(); }
            previous = -1; previousClip = null;
        }
    }
}
