using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Microsoft.Agents.Core.Models;
using Stopwatch = System.Diagnostics.Stopwatch;

namespace LunaFlow.CopilotConnection;

// Sign-in-free connection for agents published with Copilot Studio "No authentication".
// Only the agent's public token endpoint is used: no secret, account or token is stored.
internal sealed class DirectLineChannel(HttpMessageHandler handler) : IDisposable
{
    private static readonly Uri DefaultDirectLine = new("https://directline.botframework.com/");
    private readonly HttpClient http = new(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(90) };
    private readonly string userId = "lunaflow-" + Guid.NewGuid().ToString("N");
    internal TimeSpan PollInterval { get; init; } = TimeSpan.FromSeconds(1);
    // Copilot Studio sends no end marker for a turn; the reply is complete after this much silence.
    internal TimeSpan QuietPeriod { get; init; } = TimeSpan.FromSeconds(2.5);

    public void Dispose() => http.Dispose();

    // Starts a new conversation, sends one message and returns the agent's activities for that turn.
    public async Task<List<Activity>> AskAsync(Uri tokenEndpoint, string text, CancellationToken cancel)
    {
        var issued = await SendAsync(HttpMethod.Get, tokenEndpoint, null, null, cancel, tokenRequest: true);
        var token = Text(issued, "token") ?? throw new InvalidDataException("Copilot Studio 토큰 엔드포인트가 대화 토큰을 반환하지 않았습니다.");
        var directLine = await DirectLineAsync(tokenEndpoint, cancel);
        var conversation = await SendAsync(HttpMethod.Post, new Uri(directLine, "v3/directline/conversations"), token, null, cancel);
        var conversationId = Text(conversation, "conversationId") ?? Text(issued, "conversationId")
            ?? throw new InvalidDataException("Direct Line 대화를 시작하지 못했습니다.");
        token = Text(conversation, "token") ?? token;
        var activities = new Uri(directLine, $"v3/directline/conversations/{Uri.EscapeDataString(conversationId)}/activities");
        var sent = await SendAsync(HttpMethod.Post, activities, token,
            new { type = "message", from = new { id = userId, role = "user" }, text, textFormat = "plain" }, cancel);
        var sentId = Text(sent, "id");

        var replies = new List<Activity>();
        string? watermark = null;
        bool replied = false;
        var silence = Stopwatch.StartNew();
        while (true)
        {
            await Task.Delay(PollInterval, cancel);
            var page = await SendAsync(HttpMethod.Get,
                watermark is null ? activities : new Uri(activities.AbsoluteUri + "?watermark=" + Uri.EscapeDataString(watermark)), token, null, cancel);
            watermark = Text(page, "watermark") ?? watermark;
            bool received = false;
            if (page.ValueKind == JsonValueKind.Object && page.TryGetProperty("activities", out var list) && list.ValueKind == JsonValueKind.Array)
                foreach (var item in list.EnumerateArray())
                {
                    if (Text(item, "from", "id") == userId) continue;
                    var replyTo = Text(item, "replyToId");
                    if (sentId is not null && replyTo is not null && replyTo != sentId) continue;
                    var activity = ToActivity(item, conversationId);
                    received = true;
                    if (activity.Type == "endOfConversation") return replies;
                    if (activity.Type != "message") continue;
                    replies.Add(activity);
                    replied = true;
                    if (replies.Count > 512) throw new InvalidDataException("AI 응답 크기 제한을 초과했습니다.");
                }
            if (received) silence.Restart();
            else if (replied && silence.Elapsed >= QuietPeriod) return replies;
        }
    }

    // Copilot Studio reports the Direct Line host for the agent's region; only Bot Framework hosts are accepted.
    private async Task<Uri> DirectLineAsync(Uri tokenEndpoint, CancellationToken cancel)
    {
        try
        {
            var settings = await SendAsync(HttpMethod.Get,
                new Uri(tokenEndpoint.GetLeftPart(UriPartial.Authority) + "/powervirtualagents/regionalchannelsettings?api-version=2022-03-01-preview"), null, null, cancel);
            var value = Text(settings, "channelUrlsById", "directline");
            if (value is not null && Uri.TryCreate(value.EndsWith('/') ? value : value + "/", UriKind.Absolute, out var uri) && uri.Scheme == "https" &&
                uri.IsDefaultPort && string.IsNullOrEmpty(uri.UserInfo) && string.IsNullOrEmpty(uri.Query) &&
                (uri.Host == "directline.botframework.com" || uri.Host.EndsWith(".directline.botframework.com", StringComparison.OrdinalIgnoreCase) ||
                 uri.Host.EndsWith(".botframework.azure.us", StringComparison.OrdinalIgnoreCase)))
                return uri;
        }
        catch (HttpRequestException) { }
        catch (JsonException) { }
        return DefaultDirectLine;
    }

    private async Task<JsonElement> SendAsync(HttpMethod method, Uri uri, string? token, object? body, CancellationToken cancel, bool tokenRequest = false)
    {
        using var request = new HttpRequestMessage(method, uri);
        if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        if (body is not null) request.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
        HttpResponseMessage response;
        try { response = await http.SendAsync(request, cancel); }
        catch (HttpRequestException e) { throw new HttpRequestException("Copilot Studio에 연결하지 못했습니다. 인터넷 연결과 방화벽을 확인하세요. (" + e.Message + ")", e); }
        using (response)
        {
            var text = await response.Content.ReadAsStringAsync(cancel);
            if (!response.IsSuccessStatusCode) throw new HttpRequestException(Describe(response.StatusCode, tokenRequest), null, response.StatusCode);
            if (text.Length > 4 * 1024 * 1024) throw new InvalidDataException("AI 응답 크기 제한을 초과했습니다.");
            if (text.Length == 0) return default;
            using var document = JsonDocument.Parse(text);
            return document.RootElement.Clone();
        }
    }

    internal static string Describe(HttpStatusCode status, bool tokenRequest) => (tokenRequest, (int)status) switch
    {
        (true, 404) => "로그인 없는 접속을 에이전트가 허용하지 않습니다 (404). Copilot Studio에서 이 에이전트의 설정 > 보안 > 인증을 '인증 안 함'으로 저장하고 다시 게시하세요.",
        (true, 401 or 403) => $"로그인 없는 접속이 거부되었습니다 ({(int)status}). 에이전트 인증을 '인증 안 함'으로 게시했는지, 조직 데이터 정책이 이를 막는지 확인하세요.",
        (false, 413) => "요청이 너무 큽니다 (413). 동작 키가 아주 많다면 일부를 정리한 뒤 다시 요청하세요.",
        (_, 429) => "요청이 너무 많습니다 (429). 잠시 뒤 다시 시도하세요.",
        _ => $"Copilot Studio 서버 오류 ({(int)status}). 잠시 뒤 다시 시도하세요."
    };

    private static Activity ToActivity(JsonElement item, string conversationId)
    {
        var activity = new Activity
        {
            Type = Text(item, "type") ?? "",
            Text = Text(item, "text"),
            Conversation = new ConversationAccount { Id = conversationId }
        };
        if (item.TryGetProperty("attachments", out var attachments) && attachments.ValueKind == JsonValueKind.Array)
            foreach (var attachment in attachments.EnumerateArray())
            {
                var type = Text(attachment, "contentType") ?? "";
                if (type is "application/vnd.microsoft.card.oauth" or "application/vnd.microsoft.card.signin")
                    throw new InvalidOperationException("에이전트가 로그인을 요구합니다. Copilot Studio에서 인증을 '인증 안 함'으로 바꾸고 다시 게시하세요.");
            }
        if (item.TryGetProperty("suggestedActions", out var suggested) && suggested.ValueKind == JsonValueKind.Object &&
            suggested.TryGetProperty("actions", out var actions) && actions.ValueKind == JsonValueKind.Array)
        {
            var cards = new List<CardAction>();
            foreach (var action in actions.EnumerateArray())
            {
                var title = Text(action, "title") ?? Text(action, "value");
                if (title is not null && cards.Count < 16) cards.Add(new CardAction { Title = title });
            }
            activity.SuggestedActions = new SuggestedActions(new List<string>(), cards);
        }
        return activity;
    }

    private static string? Text(JsonElement element, params string[] path)
    {
        foreach (var name in path)
        {
            if (element.ValueKind != JsonValueKind.Object || !element.TryGetProperty(name, out element)) return null;
        }
        return element.ValueKind == JsonValueKind.String ? element.GetString() : null;
    }
}
