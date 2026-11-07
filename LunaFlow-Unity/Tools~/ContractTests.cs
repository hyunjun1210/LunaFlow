using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using LunaFlow.UnityImporter.Format;

internal static class ContractTests
{
    private static string json;
    private static readonly JsonSerializerOptions Options = new JsonSerializerOptions { IncludeFields = true };
    private static int checks;
    private static ProjectData Fresh() { return JsonSerializer.Deserialize<ProjectData>(json, Options); }
    private static void Check(bool valid, string message) { if (!valid) throw new Exception(message); ++checks; }
    private static void Reject(Action<ProjectData> mutate, string message)
    {
        var data = Fresh(); mutate(data);
        try { ProjectValidator.Validate(data); throw new Exception("Accepted invalid input: " + message); }
        catch (ProjectFormatException) { ++checks; }
    }
    public static int Main(string[] args)
    {
        try
        {
            json = File.ReadAllText(args[0]); var data = Fresh(); ProjectValidator.Validate(data);
            Check(ProjectValidator.CurveBudget(data, 30) > 0, "Curve budget");
            var pose = MotionSampler.Sample(data.clips[0].tracks[0], data.scene.bones[1].bindLocal, .5);
            Check(Math.Abs(pose.rotation[2] - Math.Sin(Math.PI / 8)) < 1e-6, "Slerp midway");
            Check(pose.translation[1] == 1 && pose.scale[0] == 1, "Sparse channels use bind pose");
            var times = MotionSampler.SampleTimes(data.clips[0], data.clips[0].tracks[0], 30);
            Check(times.First() == 0 && times.Last() == 1 && times.Contains(.5), "Speed-aware sampling");
            var sign = MotionSampler.Slerp(new float[] { 0, 0, 0, 1 }, new float[] { 0, 0, 0, -1 }, .5);
            Check(Math.Abs(sign[3] - 1) < 1e-6, "Shortest quaternion arc");
            Reject(d => d.schema_version = 2, "future schema");
            Reject(d => d.scene.bones[0].parent = 2, "parent cycle");
            Reject(d => d.scene.meshes[0].indices[0] = 100, "triangle index");
            Reject(d => d.scene.meshes[0].vertices[0].w = new float[4], "zero weights");
            Reject(d => d.clips[0].tracks[0].bonePath = "unknown", "unknown track");
            Reject(d => d.clips[0].tracks[0].rotations[1].time = 0, "duplicate key time");
            Reject(d => d.clips[0].tracks[0].rotations[0].value = new float[4], "zero quaternion");
            Reject(d => d.clips[0].speed = 0, "zero speed");
            Reject(d => d.scene.materials[0].textureRGBA[0] = 256, "texture byte");
            Reject(d => d.scene.effects[0].particles.seed = 4294967296L, "seed overflow");
            data.scene.effects[0].particles.seed = uint.MaxValue; ProjectValidator.Validate(data); ++checks;
            var large = Fresh(); large.clips[0].duration = 3600; large.clips[0].speed = 1;
            try { ProjectValidator.CurveBudget(large, 120); throw new Exception("Accepted oversized curve budget"); }
            catch (ProjectFormatException) { ++checks; }
            foreach (string golden in args.Skip(1))
            {
                var contract = JsonSerializer.Deserialize<ProjectData>(File.ReadAllText(golden), Options);
                ProjectValidator.Validate(contract); ProjectValidator.CurveBudget(contract, 30); ++checks;
                Console.WriteLine("Golden contract passed: " + Path.GetFileName(golden));
            }
            Console.WriteLine("PASS: " + checks + " contract checks"); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
