using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Agents.CopilotStudio.Client;
using Microsoft.Agents.CopilotStudio.Client.Discovery;
using Microsoft.Agents.Core.Models;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Identity.Client;

namespace LunaFlow.CopilotConnection;

internal static class Program
{
    private const string TestMessage = "연결 테스트입니다. 현재 LunaFlow 편집 도구는 연결되지 않았습니다. 'LunaFlow 연결 확인'이라고만 답해주세요.";

    public static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        using var cancel = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancel.Cancel(); };
        try
        {
            if (args.Contains("--self-test")) return await SelfTest.RunAsync();
            var configPath = Option(args, "--config") ?? Path.Combine(AppContext.BaseDirectory, "settings.local.json");
            if (args.Contains("--worker")) return await BridgeWorker.RunAsync(configPath, cancel.Token);
            var options = JsonSerializer.Deserialize<ClientOptions>(await File.ReadAllTextAsync(configPath, cancel.Token))
                ?? throw new InvalidDataException("설정 파일이 비어 있습니다.");
            options.ValidateAgent();
            var settings = options.CreateSettings();
            Console.WriteLine($"환경: {options.EnvironmentId}\n에이전트: {options.SchemaName}\n연결 방식: SDK 엔드포인트 자동 검색");
            // A copied URL records the selected agent. Let the current SDK discover the
            // create/execute URLs, rather than constructing those routes ourselves.
            options.ValidateIdentity();
            if (args.Contains("--check"))
            {
                Console.WriteLine("설정 형식을 확인했습니다. 로그인과 서버 접속은 아직 하지 않았습니다.");
                return 0;
            }

            Console.WriteLine("브라우저에서 에이전트와 같은 조직의 Microsoft 계정으로 로그인하세요. Ctrl+C로 취소합니다.");
            var auth = PublicClientApplicationBuilder.Create(options.AppClientId)
                .WithAuthority($"https://login.microsoftonline.com/{options.TenantId}")
                .WithRedirectUri("http://localhost")
                .Build();
            // The connection test keeps its token in memory; the editor worker uses
            // Windows-protected MSAL caching to reuse sign-in between requests.
            var token = await auth.AcquireTokenInteractive([CopilotClient.ScopeFromSettings(settings)])
                .WithPrompt(Prompt.SelectAccount).ExecuteAsync(cancel.Token);
            using var transport = new HttpClientHandler { AllowAutoRedirect = false };
            var client = new CopilotClient(settings, new ClientFactory(transport, token.AccessToken), NullLogger.Instance);
            using var turnTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancel.Token);
            turnTimeout.CancelAfter(TimeSpan.FromMinutes(4));
            Console.WriteLine("로그인 완료. 게시된 에이전트에 접속합니다...");
            await foreach (var activity in client.StartConversationAsync(emitStartConversationEvent: false, cancellationToken: turnTimeout.Token))
                PrintActivity(activity.Type, activity.Text);
            Console.WriteLine("연결 확인 메시지를 보냅니다...");
            bool receivedText = false;
            await foreach (var activity in client.AskQuestionAsync(TestMessage, cancellationToken: turnTimeout.Token))
            {
                PrintActivity(activity.Type, activity.Text);
                receivedText |= activity.Type == "message" && !string.IsNullOrWhiteSpace(activity.Text);
            }
            if (!receivedText) throw new InvalidOperationException("대화에서 텍스트 응답을 받지 못했습니다. 게시 상태와 Copilot Studio 실행 기록을 확인하세요.");
            Console.WriteLine("연결 테스트 성공. 이 테스트는 편집 명령을 실행하지 않습니다.");
            return 0;
        }
        catch (OperationCanceledException)
        {
            Console.Error.WriteLine(cancel.IsCancellationRequested ? "취소했습니다." : "응답 시간이 초과되었습니다.");
            return 3;
        }
        catch (MsalException e)
        {
            Console.Error.WriteLine($"Microsoft 로그인 오류 ({e.ErrorCode}): {e.Message}");
            Console.Error.WriteLine("네이티브 앱 등록의 http://localhost 리디렉션, 조직 ID, API 위임 권한을 확인하세요.");
            return 4;
        }
        catch (HttpRequestException e)
        {
            Console.Error.WriteLine($"서버 연결 오류 ({e.StatusCode?.ToString() ?? "network"}): {e.Message}");
            Console.Error.WriteLine("게시 상태, 에이전트 접근 권한, CopilotStudio.Copilots.Invoke 동의 상태와 지역별 SDK 지원 배포를 확인하세요.");
            return 5;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e.Message);
            return 2;
        }
    }

    private static string? Option(string[] args, string name)
    {
        int index = Array.IndexOf(args, name);
        if (index < 0) return null;
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--")) throw new ArgumentException($"{name} 뒤에 값을 입력하세요.");
        return args[index + 1];
    }

    private static void PrintActivity(string type, string? text)
    {
        if (type == "message" && !string.IsNullOrWhiteSpace(text)) Console.WriteLine($"에이전트: {text}");
        else if (type == "typing") Console.WriteLine("에이전트가 응답을 작성하고 있습니다...");
        // Do not dump raw activities: an event can contain authentication data.
    }
}

internal sealed class ClientOptions
{
    public string SuppliedEndpoint { get; set; } = "";
    public string EnvironmentId { get; set; } = "";
    public string SchemaName { get; set; } = "";
    public string TenantId { get; set; } = "";
    public string AppClientId { get; set; } = "";

    public void ValidateAgent()
    {
        if (!Guid.TryParse(EnvironmentId, out _)) throw new InvalidDataException("EnvironmentId에 에이전트 메타데이터의 환경 GUID를 입력하세요.");
        if (!Regex.IsMatch(SchemaName, @"\A[A-Za-z0-9_]+\z")) throw new InvalidDataException("SchemaName에 에이전트의 스키마 이름을 입력하세요.");
        if (string.IsNullOrWhiteSpace(SuppliedEndpoint)) return;
        var (environmentId, schemaName) = ParseEndpoint(SuppliedEndpoint);
        if (Guid.Parse(environmentId) != Guid.Parse(EnvironmentId) || schemaName != SchemaName)
            throw new InvalidDataException("복사한 연결 주소와 EnvironmentId/SchemaName이 다릅니다. 에이전트 메타데이터를 확인하세요.");
    }

    public void ValidateIdentity()
    {
        if (!Guid.TryParse(TenantId, out _) || !Guid.TryParse(AppClientId, out _))
            throw new InvalidDataException("settings.local.json의 TenantId와 AppClientId를 입력하세요. Entra 앱 등록의 디렉터리(테넌트) ID와 애플리케이션(클라이언트) ID입니다.");
    }

    public ConnectionSettings CreateSettings() => new()
    {
        EnvironmentId = EnvironmentId,
        SchemaName = SchemaName,
        Cloud = PowerPlatformCloud.Prod,
        CopilotAgentType = AgentType.Published,
        EnableDiagnostics = false
        // Leave DirectConnectUrl unset to enable the SDK's endpoint discovery.
    };

    public static (string EnvironmentId, string SchemaName) ParseEndpoint(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri) || uri.Scheme != "https" || !uri.IsDefaultPort ||
            !string.IsNullOrEmpty(uri.UserInfo) || !string.IsNullOrEmpty(uri.Fragment))
            throw new InvalidDataException("HTTPS 형식의 Copilot Studio 연결 주소가 필요합니다.");
        var host = Regex.Match(uri.Host, @"\A([a-f0-9]{30})\.([a-f0-9]{2})\.environment\.api\.powerplatform\.com\z", RegexOptions.IgnoreCase);
        var path = Regex.Match(uri.AbsolutePath, @"\A/copilotstudio/(?:agenticruntime/3p/)?dataverse-backed/authenticated/bots/([A-Za-z0-9_]+)/conversations\z");
        if (!host.Success || !path.Success) throw new InvalidDataException("이 테스트가 지원하는 프로덕션 환경의 Copilot Studio 연결 주소가 아닙니다.");
        return (Guid.ParseExact(host.Groups[1].Value + host.Groups[2].Value, "N").ToString(), path.Groups[1].Value);
    }
}

internal sealed class ClientFactory(HttpMessageHandler handler, string? accessToken = null) : IHttpClientFactory
{
    public HttpClient CreateClient(string name)
    {
        var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromMinutes(3) };
        if (accessToken is not null) client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", accessToken);
        return client;
    }
}

internal static class SelfTest
{
    public static async Task<int> RunAsync()
    {
        const string plan = "{\"format\":\"lunaflow.edit.plan\",\"version\":1,\"steps\":[]}";
        Require(BridgeWorker.ExtractPlan("```json\n" + plan + "\n```") is not null, "Extract fenced plan");
        Require(BridgeWorker.ExtractPlan("일반 답변입니다.") is null, "Plain text does not become an edit");
        Require(BridgeWorker.ExtractPlan("{} 설명 " + plan) is not null, "Ignore unrelated JSON");
        try { BridgeWorker.ExtractPlan(plan + plan.Replace("\"version\":1", "\"version\":2")); throw new InvalidOperationException("Ambiguous plans accepted."); }
        catch (InvalidDataException) { }
        const string endpoint = "https://00112233445566778899aabbccddee.ff.environment.api.powerplatform.com/copilotstudio/agenticruntime/3p/dataverse-backed/authenticated/bots/test_Agent/conversations?api-version=2022-03-01-preview";
        var (environment, schema) = ClientOptions.ParseEndpoint(endpoint);
        Require(environment == "00112233-4455-6677-8899-aabbccddeeff" && schema == "test_Agent", "Endpoint metadata");
        var options = new ClientOptions { SuppliedEndpoint = endpoint, EnvironmentId = environment, SchemaName = schema };
        options.ValidateAgent();
        Reject(() => ClientOptions.ParseEndpoint(endpoint.Replace("https://", "http://")), "Reject HTTP");
        Reject(() => ClientOptions.ParseEndpoint(endpoint.Replace("powerplatform.com", "powerplatform.com.example.org")), "Reject unrelated host");
        Reject(() => { options.SchemaName = "other"; options.ValidateAgent(); }, "Reject mixed agent metadata");
        options.SchemaName = schema;
        Reject(options.ValidateIdentity, "Reject missing identity before login");
        HelpChecks();

        // Exercise the actual SDK offline. A hard-coded DirectConnectUrl would skip
        // discovery and incorrectly POST straight to the copied conversations URL.
        using var handler = new CaptureHandler();
        var client = new CopilotClient(options.CreateSettings(), new ClientFactory(handler), NullLogger.Instance);
        try
        {
            await foreach (var ignored in client.StartConversationAsync(emitStartConversationEvent: false)) { }
        }
        catch (HttpRequestException) { }
        Require(handler.Requests.Count == 1 && handler.Requests[0].Method == HttpMethod.Get &&
            handler.Requests[0].Uri.Host.EndsWith(".environment.api.powerplatform.com", StringComparison.Ordinal) &&
            handler.Requests[0].Uri.AbsoluteUri != endpoint, "SDK performs discovery before conversation creation");
        Console.WriteLine("PASS: metadata validation, invalid input rejection, help-agent request/reply parsing, and SDK discovery request (offline; no login/server access).");
        return 0;
    }

    private static void HelpChecks()
    {
        static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement.Clone();
        var parsed = BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"질문: 카메라\",\"messageWithHistory\":\"<history>\\n</history>\\n질문: 카메라\",\"conversationId\":\"conv-1\"}"));
        Require(parsed.Message == "질문: 카메라" && parsed.ConversationId == "conv-1", "Help request fields");
        Require(BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"a\",\"messageWithHistory\":\"b\",\"conversationId\":\"\"}")).ConversationId is null, "Blank conversation starts a new one");
        Require(BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"a\",\"messageWithHistory\":\"b\"}")).ConversationId is null, "Missing conversation starts a new one");
        Reject(() => BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"  \",\"messageWithHistory\":\"b\"}")), "Reject blank help question");
        Reject(() => BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"a\"}")), "Reject missing history message");
        Reject(() => BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"a\",\"messageWithHistory\":\"b\",\"conversationId\":\"x\\ny\"}")), "Reject control characters in conversation id");
        Reject(() => BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"a\",\"messageWithHistory\":\"b\",\"conversationId\":7}")), "Reject non-string conversation id");
        Reject(() => BridgeWorker.ParseHelpRequest(Parse("{\"message\":\"" + new string('x', 64 * 1024 + 1) + "\",\"messageWithHistory\":\"b\"}")), "Reject oversized help message");

        var reply = new BridgeWorker.HelpReply();
        reply.Add(new Activity { Type = "typing", Conversation = new ConversationAccount { Id = "conv-2" } });
        reply.Add(new Activity
        {
            Type = "message", Text = " 첫 문단 ",
            SuggestedActions = new SuggestedActions(new List<string>(), new List<CardAction>
            {
                new() { Title = "카메라 이동" }, new() { Title = "카메라 이동" }, new() { Title = new string('x', 81) },
                new() { Title = "검 휘두르기" }, new() { Title = "모션캡처" }, new() { Title = "Unity" }, new() { Title = "VFX" }
            })
        });
        reply.Add(new Activity { Type = "message", Text = "둘째 문단" });
        reply.Add(new Activity { Type = "event", Text = "ignored" });
        Require(reply.ConversationId == "conv-2" && reply.Text == "첫 문단\n\n둘째 문단", "Help reply text and conversation");
        Require(reply.Suggestions.SequenceEqual(["카메라 이동", "검 휘두르기", "모션캡처", "Unity"]), "Help suggestions are unique, short and capped");
        var start = new BridgeWorker.HelpReply();
        start.Add(new Activity { Type = "message", Text = "greeting", Conversation = new ConversationAccount { Id = "conv-3" } }, collectText: false);
        start.Remember("other");
        Require(!start.HasText && start.ConversationId == "conv-3", "Conversation start keeps the id without greeting text");

        var (body, chips) = BridgeWorker.SplitSuggestions("우클릭을 누른 채 W/A/S/D로 이동합니다.[1]\n\n**추천 질문:** 선택 대상 맞추기 / 정면·측면 보기 / \"전체 단축키\"[2]\n");
        Require(body == "우클릭을 누른 채 W/A/S/D로 이동합니다.[1]" && chips.SequenceEqual(["선택 대상 맞추기", "정면·측면 보기", "전체 단축키"]), "Suggestion line becomes chips");
        (body, chips) = BridgeWorker.SplitSuggestions("Answer\n\nSuggested questions:\n- Camera\n1) Unity/Blender keys\n- Camera");
        Require(body == "Answer" && chips.SequenceEqual(["Camera", "Unity/Blender keys"]), "Bulleted suggestions become chips");
        (body, chips) = BridgeWorker.SplitSuggestions("추천 질문: a / b\n이 줄은 본문입니다.");
        Require(body == "추천 질문: a / b\n이 줄은 본문입니다." && chips.Count == 0, "Prose after the label is kept");
        (body, chips) = BridgeWorker.SplitSuggestions("추천 질문은 메뉴에 없습니다. W/A/S/D를 쓰세요.");
        Require(chips.Count == 0 && body.EndsWith("쓰세요."), "Answers without a suggestion line are unchanged");
        (body, chips) = BridgeWorker.SplitSuggestions("본문[1]\n\n이어서 물어볼 만한 질문:\n- 시점 회전은 어떻게 하나요?\n- 선택 항목에 맞추기(F)\n");
        Require(body == "본문[1]" && chips.SequenceEqual(["시점 회전은 어떻게 하나요?", "선택 항목에 맞추기(F)"]), "Free-form suggestion header becomes chips");
        (body, chips) = BridgeWorker.SplitSuggestions("본문\n\n참고: Shift를 누르면 빨라집니다.\n- 휠로 속도 조절");
        Require(chips.Count == 0 && body.EndsWith("속도 조절"), "Ordinary labelled notes stay in the answer");
    }

    private static void Require(bool success, string name)
    {
        if (!success) throw new InvalidOperationException($"Self-test failed: {name}");
    }
    private static void Reject(Action action, string name)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new InvalidOperationException($"Self-test failed: {name}");
    }
    private sealed class CaptureHandler : HttpMessageHandler
    {
        public List<(HttpMethod Method, Uri Uri)> Requests { get; } = [];
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add((request.Method, request.RequestUri!));
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.Unauthorized)
            {
                Content = new StringContent("Offline authentication boundary test.")
            });
        }
    }
}
