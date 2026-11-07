# LunaFlow 도우미 Copilot Studio 에이전트 만들기

LunaFlow 도우미는 편집기 안의 채팅 창(**Shift+F1**)이다. 웹사이트나 별도 서버 없이,
이미 쓰고 있는 SDK 클라이언트(`agent_service/copilot_client`)와 Microsoft 로그인을 그대로 재사용한다.
애니메이션·VFX 에이전트는 건드리지 않고 **도우미 전용 에이전트를 하나 더** 만든다.

> 설정 전에도 도우미 창은 바로 쓸 수 있다. **내장 도움말** 모드가 `Knowledge/LunaFlow_Help.ko.md`에서
> 로그인 없이 답한다. 아래 설정은 대화형 Copilot 답변을 켜는 과정이다.

## 1. 새 에이전트 만들기

1. [Copilot Studio](https://copilotstudio.microsoft.com/)에 기존 LunaFlow 에이전트를 만든 조직 계정으로 로그인한다.
2. 기존 에이전트와 **같은 테넌트·환경**을 고른다. 기존 에이전트를 복사하거나 덮어쓰지 않는다.
3. **에이전트 만들기**를 선택한다. 이름은 `LunaFlow Helper`, 언어는 한국어로 둔다.
4. 설명에 아래 내용을 붙여 넣는다.

```text
LunaFlow 편집기의 사용 도우미입니다. 기능 위치, 단축키, 작업 순서, 설정, 문제 해결을
LunaFlow 사용 설명서를 근거로 짧게 안내합니다. 장면을 직접 편집하지 않습니다.
```

## 2. 지침 붙여 넣기

**Overview → Instructions → Edit**(새 경험이면 지침 편집 영역)를 열고
이 폴더의 **[AgentInstructions.ko.txt](AgentInstructions.ko.txt) 전체**를 붙여 넣은 뒤 저장한다.

Topic, Power Automate 흐름, HTTP 도구, MCP 서버는 만들 필요가 없다.
LunaFlow가 질문마다 관련 설명서 발췌와 현재 편집기 상태를 함께 보낸다.

## 3. 사용 설명서를 지식으로 추가 (권장)

> **에이전트가 만들어져 편집 화면이 열린 뒤에** 지식을 추가한다. 만들기 화면에서 파일을 같이 올리면
> 저장과 생성이 같은 파일을 두 번 등록하려다 `botcomponent ... database constraint ... schemaname: ….file.lunaflowhelpkomd_…` 오류가 날 수 있다.

1. **Knowledge → Add knowledge → Files**(파일 업로드)를 고른다. **Add to agent**는 한 번만 누른다.
2. 이 폴더의 **`Knowledge/LunaFlow_Help.ko.md`**를 올린다(`.md`는 공식 지원 형식). 같은 파일을 다시 올려야 하면 기존 항목을 먼저 지우거나 `LunaFlow_Help_v2.md`처럼 이름을 바꿔 올린다.
3. 처리 상태가 **Ready**가 될 때까지 기다린다.
4. 생성형 AI 설정의 **일반 지식(General knowledge) 사용**과 **웹 검색**은 끄는 것을 권장한다. 메뉴 위치는 Copilot Studio 버전에 따라 다르다. 끄면 설명서에 없는 기능을 지어내는 일이 줄어든다.

설명서를 고친 뒤에는 Copilot Studio의 지식 파일도 새 파일로 교체해야 Copilot 답변에 반영된다.
(내장 도움말 모드는 빌드할 때 자동으로 새 파일을 복사한다.)

## 4. Copilot Studio 안에서 시험

테스트 창에서 아래처럼 묻는다.

| 질문 | 기대하는 답 |
|---|---|
| `카메라를 WASD로 움직이려면?` | 우클릭을 누른 채 W/A/S/D, Q/E로 위아래. 우클릭 없이 W/E/R은 도구 전환 |
| `검 휘두르는 동작 만들어 줘` | 직접 만들지 않고, 오른쪽 AI 대화에 보낼 문장을 제안 |
| `오늘 날씨 어때?` | LunaFlow 관련 질문만 돕는다고 짧게 안내 |

JSON 편집 계획이나 "적용했습니다" 같은 답이 나오면 지침이 저장·게시되지 않은 것이다.

## 5. 게시하고 메타데이터 확인

**Publish**를 실행한다. 지침이나 지식을 바꾼 뒤에도 다시 게시해야 LunaFlow에 반영된다.
**Settings → Advanced → Metadata**에서 두 값을 복사한다.

| 설정 | 가져올 값 |
|---|---|
| `EnvironmentId` | 도우미 에이전트가 있는 환경의 GUID |
| `SchemaName` | **도우미 에이전트**의 스키마 이름(표시 이름 아님) |

`TenantId`와 `AppClientId`는 애니메이션 연결에서 그대로 재사용한다.
처음 구성한다면 [Copilot_Studio_Setup.ko.md](../LunaFlow/Docs/Copilot_Studio_Setup.ko.md)의 Entra 앱 등록 절차를 먼저 따른다
(공용 네이티브 클라이언트, 리디렉션 `http://localhost`, 위임 권한 `CopilotStudio.Copilots.Invoke`).

## 6. LunaFlow 설정 파일 만들기

LunaFlow 루트에서 PowerShell을 열고 실행한다.

```powershell
.\LunaFlow-HelpBot\Configure.ps1 `
  -EnvironmentId '<도우미 환경 GUID>' `
  -SchemaName '<도우미 에이전트 스키마 이름>' `
  -ReuseAnimationSignIn
```

`LunaFlow-HelpBot\settings.local.json`이 만들어진다. 애니메이션·VFX 설정은 바뀌지 않는다.
애니메이션 설정이 없으면 `-ReuseAnimationSignIn` 대신 `-TenantId '<GUID>' -AppClientId '<GUID>'`를 쓴다.
이미 설정이 있으면 덮어쓰지 않는다. 의도적으로 바꿀 때만 `-Force`를 붙인다.

**클라이언트 비밀, 비밀번호, 토큰은 필요 없고 설정 파일에 넣지 않는다.** `settings.local.json`은 Git에 올라가지 않는다.

## 7. 연결 확인과 사용

```powershell
# 설정 형식만 검사(로그인·네트워크 없음)
.\LunaFlow-HelpBot\Test-Connection.ps1 -CheckOnly

# Microsoft 로그인 후 도우미 에이전트와 실제로 한 번 대화
.\LunaFlow-HelpBot\Test-Connection.ps1
```

1. `Start-LunaFlow.cmd`로 편집기를 실행한다.
2. **Shift+F1**, 상태 줄 오른쪽 **도우미** 버튼, 또는 **도움말 → LunaFlow 도우미**로 창을 연다.
3. 창 위쪽에서 **Copilot**을 고른다. 주황색 "설정되지 않았습니다" 문구가 보이면 **다시 확인**을 누른다.
4. 질문을 입력하고 Enter로 보낸다. 첫 질문 때 브라우저 로그인 창이 열릴 수 있다. 애니메이션 AI에 이미 로그인했다면 같은 로그인을 재사용한다.
5. 답을 기다리는 동안 "생각 중…"과 경과 시간이 보이고, **중지**로 취소할 수 있다. 답변 아래 추천 질문을 누르면 이어서 묻는다.

## 보내는 데이터

질문마다 다음만 보낸다: 질문, 최근 대화 최대 8개(8 KiB 이내), 관련 설명서 발췌(6 KB 이내),
편집기 상태 요약(작업 탭, 캐릭터·프로젝트 **파일 이름**, 뼈·동작·효과 개수와 선택 이름, 저장 여부, AI 연결 여부).
메시, 키프레임, 파일 경로 전체, 로그인 정보는 보내지 않는다. 형식은 [README.ko.md](README.ko.md)에 있다.

## 연결이 안 될 때

| 증상 | 확인할 내용 |
|---|---|
| Copilot Studio에서 "We couldn't create the agent" / "Some of your changes couldn't be saved" + `violates a database constraint … schemaname: ….file.lunaflowhelpkomd_…` | 지식 파일이 두 번 등록되려 한 것. 새로고침 후 에이전트 목록에 `LunaFlow Helper`가 이미 있으면 그것을 열어 지침·지식이 있는지만 확인한다(지식 파일이 있으면 다시 올리지 않음). 없거나 열리지 않으면 지식 없이 다시 만들고, 열린 뒤 3절대로 파일을 한 번 추가한다. 같은 이름 에이전트가 두 개면 쓰지 않는 쪽을 삭제한다 |
| 주황색 "설정되지 않았습니다" | `LunaFlow-HelpBot\settings.local.json`과 `Build-CopilotClient.ps1`로 만든 SDK 클라이언트가 있는지 |
| 답 대신 "Copilot Studio 응답 실패" + 내장 답변 | 게시 여부, SchemaName, 에이전트 사용 권한, 로그인 계정. 실패해도 내장 도움말 답이 함께 나온다 |
| "이전 대화가 만료되어 새 대화를 시작합니다." | 오래된 대화가 서버에서 만료됨. 자동으로 새 대화로 이어서 답한다 |
| 403 | 에이전트 사용 권한, API 위임 권한, 조직 동의, 게시 상태 |
| 설명서에 없는 내용을 지어냄 | 지식 파일 업로드 상태, 일반 지식·웹 검색 끄기, 지침 재게시 |
| JSON이나 "적용했습니다"가 나옴 | AgentInstructions.ko.txt 전체를 다시 붙여 넣고 재게시 |

공식 SDK 안내: [Integrate with web or native apps using Microsoft 365 Agents SDK](https://learn.microsoft.com/en-us/microsoft-copilot-studio/publication-integrate-web-or-native-app-m365-agents-sdk)
