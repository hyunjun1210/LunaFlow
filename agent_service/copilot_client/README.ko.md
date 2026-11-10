# LunaFlow Copilot Studio 연결 확인

기본 실행은 **게시된 에이전트와 대화 한 번**을 검증하는 콘솔 프로그램이다.
Microsoft 로그인 후 테스트 문장을 보내고 응답을 출력하며, 이 기본 모드는 프로젝트를 변경하지 않는다.
LunaFlow는 `--worker` 모드로 이 프로그램을 숨겨서 실행한다. UTF-8 파이프로 장면과 요청을 전달하고 편집 계획을 받아 C++에서 검증·적용한다.

## 로그인 없는 모드 (`settings.public.json`)

설정 파일에 `"Authentication": "None"`(또는 `TokenEndpoint`)이 있으면 MSAL 로그인 대신 `DirectLineChannel.cs`를 쓴다.
흐름: 토큰 엔드포인트(인증 헤더 없음) → 지역 설정에서 Direct Line 주소(`*.botframework.com`만 허용) → 대화 생성 → 메시지 전송 → watermark로 응답 수신.
설정 파일 옆에 `directline.secret`(웹 채널 보안 암호, git 제외)이 있으면 토큰 엔드포인트 대신 승인된 Direct Line 주소의 `v3/directline/tokens/generate`에서 `dl_` 사용자에 묶인 토큰을 받는다. 암호는 이 요청에만 쓴다.
응답이 온 뒤 2.5초 동안 새 메시지가 없거나 `endOfConversation`이면 끝낸다. 로그인 카드가 오면 "인증 안 함으로 게시" 안내 오류를 낸다.
에이전트는 Copilot Studio에서 **인증 안 함**으로 게시해야 한다. `--self-test`는 가짜 Direct Line 서버로 두 토큰 방식을 오프라인 검사한다.

## 새 환경의 SDK 연결

2026-10-19 SDK 팀은 GitHub Copilot harness 에이전트에 대한 사용자 로그인 방식 지원을 지역별로 순차 배포한다고 안내했다.
SDK 관리자는 최신 SDK에 **EnvironmentId + SchemaName**을 지정하여 엔드포인트 검색을 사용하라고 설명했다.
10월 20일 같은 이슈에 .NET 1.9.205 / Python 1.9.0의 연결 성공 보고가 있다.
반면 Learn 개요에는 아직 표준 환경만 지원한다는 문구가 있다. 이 테스트의 실제 로그인·응답 성공으로 해당 환경을 확인한다.

- [Microsoft SDK 팀의 배포 및 연결 방식 안내](https://github.com/microsoft/Agents-for-net/issues/1015)
- [Microsoft의 앱 등록·위임 권한 설정 안내](https://learn.microsoft.com/en-us/microsoft-copilot-studio/publication-integrate-web-or-native-app-m365-agents-sdk#configure-your-app-registration-for-user-interactive-sign-in)
- [Microsoft의 로그인 콘솔 샘플](https://github.com/microsoft/Agents/tree/main/samples/dotnet/copilotstudio-client)

`SuppliedEndpoint`는 사용자가 선택한 에이전트 정보를 대조하는 용도다.
`DirectConnectUrl`로 고정하지 않으며, 실제 연결 경로는 SDK 1.9.205가 검색한다.
환경 ID는 연결 주소의 호스트에서 추출했다. Copilot Studio 메타데이터와 다른 경우 메타데이터를 확인한다.

## 1. 에이전트 게시

Copilot Studio의 **LunaFlow Director → 게시**를 완료한다. 새 환경과 GPT-6 Astra 모델은 유지한다.

## 2. Microsoft Entra 앱 등록

1. [Microsoft Entra 관리 센터](https://entra.microsoft.com/)에 에이전트를 만든 학교/조직 계정으로 로그인한다.
2. 에이전트와 같은 디렉터리인지 확인하고 **Entra ID → 앱 등록 → 새 등록**을 연다.
3. 이름에 `LunaFlowClient`를 입력한다.
4. 지원되는 계정 유형은 **이 조직 디렉터리의 계정만**을 선택한다.
5. 리디렉션 URI의 플랫폼은 **공용 클라이언트/네이티브(모바일 및 데스크톱)**를 선택한다.
6. 리디렉션 URI에 `http://localhost`를 입력하고 **등록**을 누른다.
7. **개요**에서 **애플리케이션(클라이언트) ID**와 **디렉터리(테넌트) ID**를 확인한다.
8. **API 권한 → 권한 추가 → 내 조직에서 사용하는 API**를 연다.
9. **Power Platform API**를 검색하고 선택한다.
10. **위임된 권한 → CopilotStudio → CopilotStudio.Copilots.Invoke**를 선택하고 권한을 추가한다.
11. 관리자 동의가 필요한 조직이라면 해당 동의 절차를 완료한다. API가 검색되지 않는 경우 공식 안내의 Power Platform API 테넌트 준비 절차를 확인한다.

이 구성은 사용자 로그인용 공용 클라이언트이므로 **클라이언트 비밀을 만들지 않는다**.
계정 비밀번호나 로그인 토큰을 설정 파일에 넣지 않는다.

## 3. 로컬 설정

이 폴더의 `settings.local.json`에 전달받은 에이전트 주소와 환경 ID/스키마 이름을 입력해 두었다.
다른 계정으로 구성할 때는 두 항목을 해당 Entra 앱 등록에서 복사한다.

```json
"TenantId": "디렉터리(테넌트) ID",
"AppClientId": "애플리케이션(클라이언트) ID"
```

대회용 CNE-AI 에이전트 연결 주소와 사용자가 등록한 LunaFlowClient의 두 ID를 로컬 설정에 입력했다.
파일이 없는 다른 체크아웃에서는 `settings.example.json`을 `settings.local.json`으로 복사하여 입력한다.
이 로컬 파일은 Git 제외 대상이다. 연결 문자열만으로는 사용자 인증이 완료되지 않는다.

## 4. 실행

프로젝트 루트의 `Test-CopilotConnection.cmd`를 더블클릭한다.
또는 PowerShell에서 다음을 실행한다.

```powershell
.\Test-CopilotConnection.ps1
```

첫 실행에는 .NET 10 SDK와 NuGet 패키지 다운로드가 필요하다. 이 PC에는 .NET 10 SDK가 설치되어 있다.
브라우저가 열리면 에이전트를 사용할 학교/조직 계정으로 로그인한다.
`연결 테스트 성공`과 에이전트 응답이 출력되면 대화 연결이 검증된 것이다.
기본 연결 테스트의 토큰은 메모리에만 보관한다. 편집기 worker는 MSAL Extensions의 Windows 보호 캐시를 `agent_service/output/auth`에 사용하여 요청 사이에 로그인을 재사용한다.

로그인 없이 설정 형식만 확인하려면 `-CheckOnly`, 네트워크 없는 SDK 경로 검사는 `-SelfTest`를 지정한다.
두 검사도 실행 전 패키지 복원/빌드를 수행하므로 첫 패키지 설치에는 인터넷이 필요하다.

## 현재 검증 범위

- 로컬 빌드와 오프라인 검사: 이 프로그램의 실행 준비 및 SDK 검색 경로 확인.
- 실제 API 연결: 제공된 환경/스키마 및 Entra 앱 등록으로 Microsoft 로그인과 실제 응답 확인.
- 실제 편집 계획: SDK worker → C++ 파이프 → 전체 계획 검사 → 동작 복제·속도 변경·오른손 파티클 적용 및 결과 프로젝트 저장 확인.
- 편집기: 오른쪽 AI 대화에서 요청, 단계 표시·취소·한 번에 Undo 지원. 왼쪽 VFX 프롬프트는 별도의 로컬 생성 기능.
- 자동 반복 재계획, 자연스러운 임의 리그 전신 모션 생성은 별도 확장 범위.

로그인 성공 후 403이면 에이전트 접근·위임 권한·동의·게시 상태를 확인한다.
`AADSTS7000218`이면 클라이언트 비밀을 추가하는 대신 앱 플랫폼이 공용 네이티브이고 `http://localhost`가 등록되어 있는지 확인한다.
