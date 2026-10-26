# LunaFlow

DirectX 11 기반 애니메이션 편집기.

- **실행:** `Start-LunaFlow.cmd` 더블클릭
- **Visual Studio:** `build/LunaFlow.slnx`
- **사용법:** [한국어 안내](LunaFlow/README.ko.md)
- **화면:** [편집기](LunaFlow/Docs/UI.png) · [VFX 작업 화면](LunaFlow/Docs/UI-VFX.png)

| 위치 | 내용 |
|---|---|
| `LunaFlow/App` | 메뉴, 편집 화면, 설정, 타임라인, AI 대화 UI |
| `LunaFlow/Core` | 모델, 뼈대, 애니메이션, 프로젝트, 도형 |
| `LunaFlow/Rendering` | DX11 렌더러, 셰이더, 카메라, 피킹 |
| `LunaFlow/Tests` | 데이터·렌더링 검증 |
| `LunaFlow/AgentBridge` | 향후 AI 연결용 요청·결과 큐와 충돌 검증 |
| `unity_tools` | Unity 가져오기 도구 |
| `third_party/imgui` | 편집기 UI에 사용하는 ImGui 라이브러리 |
| `build` | 생성된 솔루션과 실행 파일, 빌드 캐시 |

```powershell
.\Build-LunaFlow.ps1 -Test
```

최초 구성에는 인터넷과 Visual Studio C++ 개발 도구가 필요하다.
