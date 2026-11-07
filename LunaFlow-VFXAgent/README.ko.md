# LunaFlow VFX Agent

**새 Copilot Studio VFX 에이전트**를 LunaFlow에 연결하는 기능 폴더다. 현재 LunaFlow에 병합되어 함께 빌드된다.
기존 애니메이션 에이전트의 Microsoft 로그인/SDK 실행기를 재사용하되, 원격 에이전트 설정과 대화는 분리한다.

- **먼저 할 일:** [SETUP.ko.md](SETUP.ko.md)대로 새 에이전트 생성 → 지침 저장 → 게시 → 연결 설정.
- **붙여 넣을 지침:** [AgentInstructions.ko.txt](AgentInstructions.ko.txt).
- **병합 상태:** 루트 `CMakeLists.txt`가 `VfxAgent.cmake`를 포함한다. 편집기 대화 라우팅, 요청별 허용 명령(`allowedOperations`) 검증,
  VFX 전용 설정 경로/로그인 캐시(`.auth`)는 기존 소스(`App`, `AgentBridge`, `agent_service`)에 직접 반영되어 별도 패치가 없다.

## 동작

VFX 탭 오른쪽에 전용 에이전트 대화창을 표시한다. 새 파티클 생성, 선택한 효과의 부분 수정, 뼈 부착, 기존 동작 미리 보기를 지원한다.
현재 장면과 최근 대화 최대 12개/32 KiB를 요청에 포함한다. 애니메이션 키·길이를 변경하는 명령은 VFX 요청에서 실행하지 않는다.
전체 계획을 먼저 검증하고 단계별로 적용한다. 취소, 장면 변경 충돌 감지, 작업 단위 Undo는 기존 실행기를 이용한다.
VFX 설정이 없거나 잘못되면 오류를 표시하며 애니메이션 에이전트나 로컬 생성으로 몰래 대체하지 않는다.

같은 **VFX AI** 창의 머리글에서 `Copilot`과 `로컬`(기존 키워드 규칙 생성) 모드를 전환한다. 로컬 결과와 오류 안내도 같은 대화에 표시한다.
VFX 설정이 없으면 로컬 모드로 시작한다. 왼쪽 갤러리의 수동 제작은 유지된다.
애니메이션 탭은 기존 에이전트 설정을 그대로 사용한다. 두 AI가 한 장면을 동시에 수정하지 않도록 한 번에 한 작업만 허용한다.

## 파일

| 파일 | 역할 |
|---|---|
| `VfxAgent.h`, `VfxAgent.cpp` | 전용 요청, 제한된 명령 목록, 대화 맥락, 설정 경로 |
| `EditorVfxAgent.cpp` | VFX 대화 UI와 기존 실행 결과의 대화별 라우팅 |
| `VfxAgent.cmake` | 기존 C++ 빌드에 소스와 검사를 등록 |
| `Tests\VfxAgentTests.cpp` | 범위 제한, 생성/수정, 충돌/취소, UI/Undo 검사 |
| `AgentInstructions.ko.txt`, `Examples\StudioRequest.json` | Studio에 넣을 지침과 가상 장면 시험 요청 |
| `Configure.ps1`, `settings.example.json` | 새 에이전트 메타데이터와 기존 로그인 ID 구성 |
| `Test-Connection.ps1` | VFX 전용 설정 검사 또는 실제 로그인/대화 시험 |
| `settings.local.json` | 이 PC의 VFX 에이전트 연결 정보. 버전 관리에서 제외 |

이 폴더는 **단독 실행 앱이나 Unity 패키지가 아니다.** 기존 LunaFlow C++ 앱과 SDK 클라이언트가 필요하다.
Copilot Studio의 에이전트 생성·게시와 조직 계정 로그인은 사용자가 수행해야 한다.
지침을 작성해 둔 것만으로 원격 에이전트가 생성되거나 게시되지는 않는다.

## 개발 확인

루트에서 `.\Build-LunaFlow.ps1 -ConfigureOnly` 후:

```powershell
cmake --build .\build --config Release --target LunaFlowVfxAgentTests LunaFlow --parallel 6
ctest --test-dir .\build -C Release -R LunaFlow.VfxAgent --output-on-failure
.\LunaFlow-VFXAgent\Test-Connection.ps1 -SelfTest
```

VFX 검사는 로컬 가상 장면/소프트웨어 DX11 장치로 수행하며 로그인하지 않는다.
SDK 자체 검사도 원격 요청이 없는 검사다. 신규 원격 에이전트의 실제 연결 성공을 대신하지 않는다.
