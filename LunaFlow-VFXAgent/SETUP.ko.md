# VFX 전용 Copilot Studio 에이전트 만들기

이 안내는 **애니메이션 에이전트는 그대로 두고 VFX 에이전트를 새로 만드는 방법**이다.
로그인 모드는 Microsoft 로그인과 기존 SDK 프로그램만 재사용한다. 첨부 HTML의 토큰 브로커, SEALED 값은 사용하지 않는다.

> **로그인 없이 쓰기:** 에이전트를 **인증 안 함**으로 게시하고 이 폴더의 `settings.public.json`을 두면
> Copilot Studio가 발급하는 Direct Line 토큰으로 로그인 없이 연결된다(5~6단계의 설정·Entra 준비 불필요).
> 자세한 내용은 [로그인 없이 쓰기](../LunaFlow/Docs/Copilot_Studio_Setup.ko.md#10-로그인-없이-쓰기-시연배포용)를 본다.

## 1. 새 에이전트 생성

1. [Copilot Studio](https://copilotstudio.microsoft.com/)에 기존 애니메이션 에이전트를 사용하는 학교/조직 계정으로 로그인한다.
2. 기존 애니메이션 에이전트와 **같은 테넌트·환경**을 선택한다. 기존 에이전트를 이름만 바꾸거나 덮어쓰지 않는다.
3. 홈에서 에이전트 만들기를 선택한다. **이름은 `LunaFlow VFX Agent`**, 언어는 한국어로 설정한다.
4. 설명에 아래 내용을 붙여 넣는다.

```text
LunaFlow의 VFX 제작을 돕는 전용 에이전트입니다.
현재 장면과 선택한 뼈·효과 정보를 읽고 파티클 생성, 기존 효과 수정,
뼈 부착 및 미리 보기를 위한 JSON 편집 계획을 작성합니다.
실제 편집은 LunaFlow가 수행하며 애니메이션 키프레임과 동작 길이는 변경하지 않습니다.
```

기존 연결이 잘 되는 경험/하네스를 우선 사용한다. **빌드·미리 보기·평가**가 있는 새 경험과 **Overview·Topics·Tools**가 있는 표준 경험은 메뉴 위치가 다르다.
표준 경험의 공식 생성 절차는 [Microsoft 빠른 시작](https://learn.microsoft.com/en-us/microsoft-copilot-studio/fundamentals-get-started)을 참고한다.
새 경험의 SDK 연결은 환경별 제공 상태가 다를 수 있으므로, 아래 실제 연결 시험으로 확인한다.
새 에이전트를 만들기 위해 기존 애니메이션 에이전트의 모델이나 환경을 바꿀 필요는 없다.

## 2. 지침 붙여 넣기

새 에이전트의 **지침 / Instructions** 편집 영역을 연다. 표준 경험에서는 **Overview → Instructions → Edit**다.
이 폴더의 **[AgentInstructions.ko.txt](AgentInstructions.ko.txt) 전체**를 붙여 넣고 저장한다.

별도의 Prompt 도구, Power Automate 흐름, HTTP 호출 도구, MCP 서버를 만들 필요는 없다.
현재 LunaFlow SDK 클라이언트가 요청마다 계획 규격과 장면 데이터를 보내고, 반환된 계획은 C++ 편집기가 실행한다.
공식 웹 문서를 지식 소스로 추가하는 것도 이 기능의 필수 조건이 아니다.

지침의 핵심은 **새 요청에는 생성, 기존 효과 수정에는 업데이트, 모호하면 질문, 실행 완료를 허위로 주장하지 않기**다.

## 3. Copilot Studio 안에서 시험

미리 보기/테스트 대화에서 먼저 `너는 어떤 VFX 작업을 도와줄 수 있어?`라고 보낸다.
이 단계에는 실제 LunaFlow 장면이 없으므로 기능 설명만 나와야 한다.

그다음 **[Examples\StudioRequest.json](Examples/StudioRequest.json)** 전체를 테스트 대화에 붙여 넣는다.
응답은 `format: lunaflow.edit.plan`인 JSON 객체 하나여야 한다.
`requestId`는 `studio-test-request`, `sessionId`는 `studio-test-session`이어야 하며,
단계에는 `create_particles`와 선택적인 `preview_motion`만 들어가야 한다.

이 파일의 뼈 경로와 ID는 **Studio 시험 전용 가상 데이터**다. 이 응답을 실제 프로젝트에 가져다 적용하지 않는다.
실제 사용 시에는 LunaFlow가 현재 장면의 정확한 ID를 자동으로 보낸다.

## 4. 게시하고 메타데이터 확인

**게시 / Publish**를 실행한다. 지침을 수정한 뒤에도 다시 게시해야 LunaFlow에 반영된다.
체험 라이선스나 조직 정책 때문에 게시할 수 없다면 게시 권한/라이선스를 먼저 확보해야 한다.

표준 경험에서는 **Settings → Advanced → Metadata**에서 아래 값을 복사한다.
다른 경험이라면 해당 에이전트의 연결/메타데이터 화면에서 같은 값을 확인한다.

| 설정 | 가져올 값 |
|---|---|
| `EnvironmentId` | **새 VFX 에이전트가 있는 환경**의 GUID |
| `SchemaName` | **새 VFX 에이전트**의 스키마 이름. 화면에 보이는 표시 이름이 아님 |
| `TenantId` | Entra 앱 등록의 디렉터리(테넌트) ID |
| `AppClientId` | Entra 공용 네이티브 앱 등록의 애플리케이션(클라이언트) ID |

같은 테넌트라면 기존 애니메이션 연결의 `TenantId`와 `AppClientId`를 재사용할 수 있다.
**SchemaName은 반드시 새 VFX 에이전트 값이어야 한다.** 기존 에이전트의 스키마 이름을 넣으면 새 VFX 에이전트가 아닌 기존 에이전트에 접속하게 된다.
SDK는 EnvironmentId와 SchemaName으로 엔드포인트를 검색하므로 `SuppliedEndpoint`는 빈 문자열로 두어도 된다.
채널에서 복사한 연결 문자열 전체를 EnvironmentId나 SchemaName에 넣지 않는다.

## 5. LunaFlow 설정 파일 만들기

LunaFlow 루트에서 PowerShell을 열고 실행한다. `<...>` 부분은 실제 값으로 바꾼다.

```powershell
.\LunaFlow-VFXAgent\Configure.ps1 `
  -EnvironmentId '<새 VFX 환경 GUID>' `
  -SchemaName '<새 VFX 에이전트 스키마 이름>' `
  -ReuseAnimationSignIn
```

이 명령은 기존 애니메이션 설정에서 **TenantId와 AppClientId만 읽어 재사용**하고,
새 `LunaFlow-VFXAgent\settings.local.json`을 만든다. 기존 애니메이션 설정은 수정하지 않는다.
이미 VFX 설정이 있으면 덮어쓰지 않는다. 내용을 확인한 후 의도적으로 바꿀 때만 `-Force`를 추가한다.

기존 로그인 설정이 없다면 두 ID도 직접 지정한다.

```powershell
.\LunaFlow-VFXAgent\Configure.ps1 `
  -EnvironmentId '<환경 GUID>' -SchemaName '<스키마 이름>' `
  -TenantId '<테넌트 GUID>' -AppClientId '<클라이언트 GUID>'
```

스크립트 대신 `settings.example.json`을 `settings.local.json`으로 복사하고 네 값을 직접 채워도 된다.
LunaFlow의 설정은 최상위 네 필드를 사용한다. 공식 샘플의 `CopilotStudioAgent` 중첩 객체를 추가하지 않는다.

## 6. Entra 로그인 준비

기존 애니메이션 연결이 같은 조직에서 정상 동작한다면 앱 등록을 새로 만들 필요는 없다.
새 에이전트 사용 권한은 별도로 필요하다.

처음 구성한다면 [Microsoft 공식 SDK 안내](https://learn.microsoft.com/en-us/microsoft-copilot-studio/publication-integrate-web-or-native-app-m365-agents-sdk)에 따라 준비한다.

1. 같은 테넌트의 Entra 앱 등록을 생성/선택한다.
2. 플랫폼은 **모바일 및 데스크톱 / 공용 네이티브 클라이언트**, 리디렉션 URI는 `http://localhost`로 설정한다.
3. **API permissions → Add a permission → APIs my organization uses → Power Platform API**를 연다.
4. 위임된 권한 **CopilotStudio.Copilots.Invoke**를 추가하고 조직에서 요구하는 동의를 받는다.

**클라이언트 비밀, 계정 비밀번호, 로그인 토큰은 필요하지 않으며 설정 파일에 넣지 않는다.**
SDK 로그인 때문에 에이전트의 인증 설정을 `인증 없음`으로 바꿀 필요도 없다.

## 7. 연결과 실제 편집

현재 컴퓨터에서는 빌드된 프로그램을 사용한다. 다른 컴퓨터에서 옮긴 소스를 처음 빌드하려면 루트에서 `.\Build-LunaFlow.ps1`을 실행한다.

```powershell
# 설정 형식만 검사: 로그인/원격 요청 없음
.\LunaFlow-VFXAgent\Test-Connection.ps1 -CheckOnly

# Microsoft 로그인 후 새 VFX 에이전트의 대화 응답 검사
.\LunaFlow-VFXAgent\Test-Connection.ps1
```

두 번째 명령은 연결 시험 문장을 한 번 보내며 실제 프로젝트를 변경하지 않는다.
브라우저에서는 **새 VFX 에이전트에 접근할 수 있는 조직 계정**으로 로그인한다.
대화 연결 성공은 편집 품질 검증과 다르다. 이어서 LunaFlow에서 확인한다.

1. `Start-LunaFlow.cmd`로 편집기를 시작하고 **VFX 탭**을 연다.
2. 계층에서 오른손 등 부착할 뼈를 선택한다. VFX 대화창 위쪽의 부착 뼈 표시를 확인한다.
3. 오른쪽 **VFX AI** 머리글에서 `Copilot`을 고르고 `선택한 뼈에 파란 마법 파티클을 만들어 줘. 초당 80개, 수명 1.5초.`를 보낸다.
4. 효과 목록과 화면에 생성된 것을 확인한 뒤 `선택한 효과의 입자 크기를 절반으로 줄여 줘`라고 보낸다. 효과 개수가 늘지 않고 기존 효과가 바뀌어야 한다.
5. **작업 중지**는 아직 적용하지 않은 단계를 멈춘다. **Ctrl+Z**는 적용된 이번 AI 작업을 한 묶음으로 되돌린다.

**VFX AI**의 `로컬` 모드는 기존 키워드 규칙 기능이며 로그인하지 않는다. 대화의 각 메시지에 출처(로컬/Copilot Studio)를 표시해 구분한다.
VFX와 애니메이션 대화/입력은 분리하지만, 같은 장면을 편집하므로 AI 작업은 동시에 하나만 실행한다.
VFX 대화 내역은 메모리에만 유지되며 새 장면을 열면 초기화된다.

## 연결이 안 될 때

| 증상 | 확인할 내용 |
|---|---|
| VFX settings are missing | 이 폴더의 settings.local.json 생성 여부. 애니메이션 설정만 있어서는 VFX가 연결되지 않음 |
| 설정 검사는 성공하지만 연결 실패 | 새 에이전트 게시, 접근 권한, 환경별 SDK 지원, Microsoft 로그인 상태 |
| 403 | 에이전트 사용 권한, API 위임 권한, 조직 동의, 게시 상태 |
| AADSTS7000218 | 공용 네이티브 앱 플랫폼과 localhost 리디렉션. 비밀을 추가하여 우회하지 않음 |
| 일반 설명만 오고 편집 안 됨 | VFX 지침 저장/재게시, SchemaName, JSON 계획 응답 확인 |
| Operation is not allowed | VFX가 애니메이션 명령을 계획함. 지침/게시 버전 점검. 이 경우 전체 계획을 거부함 |
| The scene changed | AI 대기 중 수동 편집이 있었음. 새 상태로 다시 요청 |

공식 문서 확인일: 2026-10-28.
SDK 일반 안내는 표준 하네스를 대상으로 하므로 새 경험의 지원을 무조건 보장하지 않는다.
관련 배포 논의는 [Microsoft SDK 이슈 #1015](https://github.com/microsoft/Agents-for-net/issues/1015)를 참고하고 실제 연결 결과로 판단한다.
