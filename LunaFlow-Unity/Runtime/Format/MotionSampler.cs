using System;
using System.Collections.Generic;

namespace LunaFlow.UnityImporter.Format
{
    public static class MotionSampler
    {
        public static TransformData Sample(TrackData track, TransformData rest, double sourceTime)
        {
            return new TransformData
            {
                translation = Channel(track == null ? null : track.positions, rest.translation, sourceTime, false),
                rotation = Channel(track == null ? null : track.rotations, rest.rotation, sourceTime, true),
                scale = Channel(track == null ? null : track.scales, rest.scale, sourceTime, false)
            };
        }

        public static double[] SampleTimes(ClipData clip, TrackData track, int fps)
        {
            var times = new SortedSet<double> { 0, clip.duration / clip.speed };
            if (track != null)
            {
                int frames = (int)Math.Ceiling(clip.duration / clip.speed * fps);
                for (int i = 1; i < frames; ++i) times.Add(i / (double)fps);
                foreach (var channel in new[] { track.positions, track.rotations, track.scales })
                    if (channel != null) foreach (var key in channel) times.Add(Math.Min(clip.duration, key.time) / clip.speed);
            }
            var result = new double[times.Count]; times.CopyTo(result); return result;
        }

        private static float[] Channel(KeyData[] keys, float[] fallback, double time, bool rotation)
        {
            if (keys == null || keys.Length == 0) return rotation ? Normalize(fallback) : (float[])fallback.Clone();
            if (time <= keys[0].time) return rotation ? Normalize(keys[0].value) : (float[])keys[0].value.Clone();
            if (time >= keys[keys.Length - 1].time) return rotation ? Normalize(keys[keys.Length - 1].value) : (float[])keys[keys.Length - 1].value.Clone();
            int low = 0, high = keys.Length - 1;
            while (high - low > 1) { int middle = (low + high) / 2; if (keys[middle].time <= time) low = middle; else high = middle; }
            var a = keys[low]; var b = keys[high]; double t = (time - a.time) / (b.time - a.time);
            if (rotation) return Slerp(a.value, b.value, t);
            var result = new float[fallback.Length]; for (int i = 0; i < result.Length; ++i) result[i] = (float)(a.value[i] + (b.value[i] - a.value[i]) * t);
            return result;
        }

        public static float[] Normalize(float[] q)
        {
            double length = Math.Sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);
            if (length < 1e-12) throw new ProjectFormatException("Zero quaternion.");
            return new[] { (float)(q[0] / length), (float)(q[1] / length), (float)(q[2] / length), (float)(q[3] / length) };
        }

        public static float[] Slerp(float[] from, float[] to, double t)
        {
            var a = Normalize(from); var b = Normalize(to); double dot = 0;
            for (int i = 0; i < 4; ++i) dot += (double)a[i] * b[i];
            if (dot < 0) { dot = -dot; for (int i = 0; i < 4; ++i) b[i] = -b[i]; }
            dot = Math.Max(-1, Math.Min(1, dot)); double wa = 1 - t, wb = t;
            if (dot < .9995) { double angle = Math.Acos(dot), sine = Math.Sin(angle); wa = Math.Sin((1 - t) * angle) / sine; wb = Math.Sin(t * angle) / sine; }
            var result = new float[4]; for (int i = 0; i < 4; ++i) result[i] = (float)(a[i] * wa + b[i] * wb);
            return Normalize(result);
        }
    }
}
