# LunaFlow 도우미 (Help Bot)

LunaFlow 편집기 안에서 사용법을 묻고 답하는 **대화형 도움말 창**이다. 루트 `CMakeLists.txt`가 `HelpBot.cmake`를 포함해 함께 빌드된다.

- **여는 법:** **Shift+F1**, 상태 줄 오른쪽 **도우미** 버튼, 또는 **도움말 → LunaFlow 도우미**.
- **두 가지 모드**
  - **내장 도움말**: 로그인·인터넷 없이 [Knowledge/LunaFlow_Help.ko.md](Knowledge/LunaFlow_Help.ko.md)(20개 주제)에서 가장 관련 있는 절을 바로 보여 준다. 설정 전 기본값.
  - **Copilot**: 전용 Copilot Studio 에이전트 `LunaFlow Helper`와 대화한다. 같은 대화를 이어 가며, 실패하면 내장 답변을 함께 표시한다.
- **Copilot 연결:** [SETUP.ko.md](SETUP.ko.md) → 에이전트 생성, [AgentInstructions.ko.txt](AgentInstructions.ko.txt) 붙여 넣기, 설명서 업로드, 게시, `Configure.ps1`.

## 창 기능

| 기능 | 동작 |
|---|---|
| 생각 중 표시 | 답을 기다리는 동안 반짝이는 "생각 중…", 진행 단계, 경과 시간을 보여 준다. **중지**로 취소한다 |
| 답변 출처 | 답 위에 `Copilot Studio · 3초 동안 생각함` 또는 `내장 도움말`을 표시한다 |
| 추천 질문 | 답 아래 칩을 누르면 바로 이어서 묻는다 (Copilot 제안 최대 4개, 내장 답변은 관련 주제) |
| 입력 | Enter 보내기, Ctrl+Enter 줄바꿈, 최대 2000바이트 |
| 새 대화 | 기록과 Copilot 대화 ID를 지우고 새로 시작 |
| 화면 배율 | 100–200% DPI, 1000×700 이상 창에서 잘리지 않게 다시 배치 |

도우미는 **장면을 편집하지 않는다.** 동작은 애니메이션 탭의 AI 대화, 효과는 VFX AI로 안내한다.
애니메이션 AI가 작업 중이어도 도우미에게 질문할 수 있다(별도 실행기 사용).

## 요청 형식

편집기는 질문마다 공용 SDK 실행기(`CopilotConnection.exe --worker --config LunaFlow-HelpBot\settings.local.json`)에 아래 JSON을 보낸다.

```json
{
  "format": "lunaflow.help.request",
  "version": 1,
  "agentId": "lunaflow.help",
  "requestId": "help-…",
  "sessionId": "help-session-…",
  "conversationId": "",
  "language": "ko",
  "question": "카메라를 WASD로 움직이려면?",
  "knowledgeTopics": ["뷰포트 카메라 조작"],
  "message": "[LunaFlow 사용 도우미 요청]\n…<app_state>…</app_state>\n<manual>…</manual>\n질문: 카메라를 WASD로 움직이려면?",
  "messageWithHistory": "… <history>사용자: … / 도우미: …</history> … 질문: …"
}
```

- `<manual>`: 질문과 가장 관련 있는 설명서 주제 최대 3개(6000바이트 이내).
- `<app_state>`: 작업 탭, 캐릭터·프로젝트 **파일 이름**, 뼈·동작·효과 수와 선택 이름, 저장 여부, 자동 리깅 필요 여부, 모션캡처·AI 상태. 최대 24줄, 줄당 120자.
- `<history>`: 최근 8개 메시지(8 KiB, 메시지당 1200바이트 이내). 실행기는 기존 대화가 살아 있으면 `message`만 보내고,
  처음이거나 서버에서 만료되었으면 새 대화를 열어 `messageWithHistory`를 보낸다.
- 응답: `{ "status": "completed", "message": "…", "plan": null, "conversationId": "…", "suggestions": ["…"] }`.
  진행 중에는 `status: "running"` 이벤트가 단계 문구를 갱신한다. `failed`면 내장 도움말로 대신 답한다.
- 로그인 캐시는 애니메이션 에이전트와 같은 `agent_service/output/auth`(Windows 보호 MSAL 캐시)를 써서 한 번 로그인으로 둘 다 쓴다.

## 파일

| 파일 | 역할 |
|---|---|
| `HelpBot.h`, `HelpBot.cpp` | 설명서 파싱, 관련도 순위, 내장 답변, 요청 생성, Markdown → 텍스트 변환 |
| `EditorHelpBot.cpp` | 도우미 창 UI, 실행기 이벤트 처리, 편집기 상태 요약 |
| `Knowledge/LunaFlow_Help.ko.md` | 사용 설명서. 빌드 때 `LunaFlow.exe` 옆 `HelpBot\`로 복사되고, Copilot Studio 지식으로도 업로드 |
| `HelpBot.cmake` | 소스·설명서 복사·검사 등록 |
| `Tests/HelpBotTests.cpp` | 파서, 순위, 요청 한도, 표시 변환, DPI·언어별 창 배치, Copilot 흐름 검사 |
| `AgentInstructions.ko.txt` | Copilot Studio에 붙여 넣을 지침 |
| `Configure.ps1`, `settings.example.json` | 도우미 에이전트 연결 설정 생성 |
| `Test-Connection.ps1` | 설정 형식 검사(`-CheckOnly`) 또는 실제 로그인·대화 시험 |
| `settings.local.json` | 이 PC의 연결 정보. Git에서 제외 |

설명서 형식: `## 제목 {#id}` 다음 줄에 `키워드: a, b, c`, 그 아래 본문. 주제를 추가하면 내장 도움말에 바로 반영되고,
Copilot 답변에는 Copilot Studio 지식 파일을 교체·재게시해야 반영된다.

## 개발 확인

```powershell
cmake --build .\build --config Release --target LunaFlowHelpBotTests
ctest --test-dir .\build -C Release -R LunaFlow.HelpBot --output-on-failure
.\LunaFlow-HelpBot\Test-Connection.ps1 -SelfTest
```

검사는 로컬 소프트웨어 DX11 장치와 가상 실행기 이벤트로 수행하며 로그인하지 않는다.
실제 Copilot Studio 답변은 에이전트를 게시하고 `Test-Connection.ps1`로 확인한다.
