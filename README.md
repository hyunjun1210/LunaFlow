# LunaFlow

DirectX 11 기반 애니메이션 편집기.

VFX 탭에서 Unity 방식의 파티클 모듈을 직접 편집하거나 한국어/영어 프롬프트로 생성할 수 있다.
VFX 탭 오른쪽 **VFX AI** 창에서 Copilot Studio(Microsoft 로그인)와 로컬 규칙 생성을 전환하고, 왼쪽 갤러리에서 한 번에 효과를 추가한다.
애니메이션 탭은 기존 에이전트, VFX 탭은 파티클 명령만 허용하는 **전용 VFX 에이전트**와 대화한다.
AI 계획은 한 단계씩 화면에 반영되며 중지와 작업 단위 되돌리기를 지원한다.
왼쪽 위 FBX 가져오기에서 캐릭터 열기·모델 추가·체형을 보정한 동작 가져오기를 선택할 수 있다.
`보기 > 웹캠 모션캡처...`로 웹캠 전신 동작을 녹화해 새 동작 클립으로 추가할 수 있다.
**Shift+F1**(또는 상태 줄 **도우미** 버튼)로 여는 **LunaFlow 도우미** 창에서 사용법을 묻고 답할 수 있다. 로그인 없이 내장 설명서로 바로 답하고, 설정하면 Copilot Studio 도우미 에이전트와 대화한다.

- **실행:** `Start-LunaFlow.cmd` 더블클릭
- **Visual Studio:** `Open-LunaFlow.cmd` 더블클릭 (현재 폴더에 솔루션을 다시 구성한 뒤 열기)
- **사용법:** [한국어 안내](LunaFlow/README.ko.md)
- **Copilot Studio 준비:** [VFX·애니메이션 에이전트 생성 가이드](LunaFlow/Docs/Copilot_Studio_Setup.ko.md)
- **전용 VFX 에이전트:** [LunaFlow-VFXAgent](LunaFlow-VFXAgent/README.ko.md) ([연결 설정](LunaFlow-VFXAgent/SETUP.ko.md))
- **웹캠 모션캡처:** [LunaFlow-Mocap](LunaFlow-Mocap/README.ko.md) (최초 1회 `LunaFlow-Mocap\Setup.ps1`, Python 3.11 x64)
- **사용 도우미 챗봇:** [LunaFlow-HelpBot](LunaFlow-HelpBot/README.ko.md) ([Copilot Studio 연결 설정](LunaFlow-HelpBot/SETUP.ko.md))
- **문장 → JSON 변환 구조:** [자연어 요청이 편집 계획 JSON으로 바뀌는 과정](LunaFlow/Docs/Sentence_to_JSON.ko.md)
- **Unity `.lunaflow` 가져오기 패키지:** [LunaFlow-Unity](LunaFlow-Unity/README.md) (Package Manager > Add package from disk > `LunaFlow-Unity/package.json`)
- **새 환경 SDK 연결 확인:** [Microsoft 로그인·연결 테스트](agent_service/copilot_client/README.ko.md) (`Test-CopilotConnection.cmd`)
- **화면:** [편집기](LunaFlow/Docs/UI.png) · [VFX 작업 화면](LunaFlow/Docs/UI-VFX.png) · [파티클 제작](LunaFlow/Docs/UI-Particles.png)

| 위치 | 내용 |
|---|---|
| `LunaFlow/App` | 메뉴, 편집 화면, 설정, 타임라인, AI 대화 UI |
| `LunaFlow/Core` | 모델, 뼈대, 애니메이션, 프로젝트, 도형 |
| `LunaFlow/Rendering` | DX11 렌더러, 셰이더, 카메라, 피킹 |
| `LunaFlow/Tests` | 데이터·렌더링 검증 |
| `LunaFlow/AgentBridge` | Copilot SDK 프로세스 연결, 계획 검증·실행, 취소·충돌 검사 |
| `agent_service/copilot_client` | Microsoft 로그인과 Copilot Studio 대화 SDK 연결 프로그램 |
| `unity_tools` | Unity 가져오기 도구 (FBX + `.lunaflow.json`) |
| `LunaFlow-VFXAgent` | VFX 전용 Copilot Studio 에이전트 요청·대화·검사 |
| `LunaFlow-Mocap` | MediaPipe 웹캠 추적 worker, 뼈 회전 변환, 녹화 패널·검사 |
| `LunaFlow-HelpBot` | 사용 도우미 챗봇 창, 내장 사용 설명서, 도우미 Copilot Studio 에이전트 요청·검사 |
| `LunaFlow-Unity` | `.lunaflow` 프로젝트를 직접 가져오는 Unity 패키지(ScriptedImporter)·계약 검사 |
| `third_party/imgui` | 편집기 UI에 사용하는 ImGui 라이브러리 |
| `build` | 생성된 솔루션과 실행 파일, 빌드 캐시 |

```powershell
.\Build-LunaFlow.ps1 -Test
```

최초 구성에는 인터넷과 Visual Studio C++ 개발 도구가 필요하다.

폴더를 옮기거나 `CMakeLists.txt` / `ZERO_CHECK.vcxproj`를 찾을 수 없다는 오류가 나오면,
Visual Studio에서 이전 솔루션을 닫고 **소스 파일이 있는 폴더의 `Open-LunaFlow.cmd`**를 실행한다.
`build` 폴더만 따로 복사해서 열면 원래 소스 경로를 계속 참조한다.
빌드 스크립트는 다른 경로의 CMake 캐시를 감지하면 기존 빌드를 `_build_backups`에 보존하고 새로 구성한다.
이미 받은 라이브러리 소스는 현재 프로젝트의 `build/_deps`에서 재사용한다.

구성만 다시 확인하려면 `.\Build-LunaFlow.ps1 -ConfigureOnly`,
현재 경로에서도 빌드 파일을 완전히 다시 생성하려면 `.\Build-LunaFlow.ps1 -Reconfigure`를 실행한다.

폴더 이동에 따른 구성 복구 검사는 `.\LunaFlow\Tests\BuildPaths.Tests.ps1`로 실행한다.
