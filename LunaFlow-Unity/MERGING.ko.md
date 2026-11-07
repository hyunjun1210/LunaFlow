# 집 버전과 합치기

## 복사할 것

**이 `LunaFlow-Unity` 폴더 전체만** 집 프로젝트 옆에 추가합니다. C++ 소스, CMake, Copilot 설정, 기존 `unity_tools`에 대한 변경은 필요하지 않습니다. `Library`, `Temp`, `.test-project`, `Tools~/bin`, `Tools~/obj`, `Tools~/.build`는 검증용 산출물이므로 제외하세요.

Unity Package Manager에서 복사한 폴더의 `package.json`을 선택합니다. Git을 사용한다면 이 폴더만 별도 커밋으로 추가하고 `.meta` 파일도 함께 보관합니다. `LunaFlow-Unity`는 독립 저장소로 옮겨도 부모 프로젝트 경로가 필요하지 않습니다.

## 충돌을 피한 구조

| 항목 | 새 패키지 | 기존 연동 |
|---|---|---|
| C# namespace | `LunaFlow.UnityImporter` | `LunaFlow.Runtime` |
| Assembly | `LunaFlow.UnityImporter.Runtime`, `.Editor` | 기존 `unity_tools` 구성 유지 |
| 입력 확장자 | `.lunaflow` | FBX + `.lunaflow.json` |
| 원본 에디터 빌드 필요 | 없음 | 기존 내보내기 방식 사용 |
| 생성 데이터 위치 | Unity의 가져온 파일 하위 에셋 | 기존 파이프라인 유지 |

같은 Unity 프로젝트에서 `.lunaflow`용 `ScriptedImporter`가 이미 생겼다면 **둘 중 하나만 활성화**해야 합니다. 같은 확장자에 두 가져오기 구현을 동시에 등록하지 마세요. 이 패키지를 `Assets`와 Package Manager에 중복 설치하는 것도 피하세요.

## 집에서 저장 형식이 바뀌었다면

1. 집 버전으로 작은 테스트 프로젝트를 `.lunaflow`로 저장합니다. 모델/뼈/동작/VFX를 각각 하나 이상 포함하세요.
2. 새 패키지를 설치한 Unity의 Assets에 복사해 확인합니다. 현재 계약은 `lunaflow.project`의 `schema_version: 1`입니다. 알 수 없는 추가 필드는 무시하고, 알 수 없는 버전/좌표계는 오류로 표시합니다.
3. JSON 필드가 달라졌다면 `Runtime/Format/ProjectData.cs`의 DTO와 `ProjectValidator.cs`를 먼저 맞춥니다. 버전이 올라가면 이전 파일을 읽는 명시적인 변환 계층을 추가한 뒤 검증하세요.
4. 메시·텍스처 변환은 `Editor/LunaFlowProjectImporter.cs`, VFX 변환은 `Editor/EffectBuilder.cs`, 실행 중 재생은 `Runtime/LunaFlowPlayer.cs`에 모여 있습니다. C++ 기능과의 연결이 필요해도 UnityEngine/UnityEditor 코드를 C++ 프로젝트에 섞을 필요가 없습니다.
5. `Tools~/Fixtures/EditorSnapshot.lunaflow`는 현재 C++ 에디터가 저장한 계약 확인용 파일입니다. 집 버전의 새 파일도 별도 fixture로 추가해 계약 검사와 Unity 가져오기를 함께 확인하세요. 기존 fixture를 지워서 이전 버전 호환성 검사를 없애지 마세요.

## 합친 뒤 확인 순서

1. 컴파일 오류 없이 패키지가 로드되는지 확인합니다.
2. 내장 `Wave.lunaflow` 샘플로 모델/회전/파티클 재생을 확인합니다.
3. 집 버전의 실제 파일을 가져와 재질, 뼈대, 각 동작과 VFX를 확인합니다.
4. 같은 원본 파일을 다시 저장해 자동 갱신과 하위 AnimationClip 참조 유지를 확인합니다.

이 패키지는 Unity 소비 측 기능입니다. Copilot API, 원본 에디터의 저장/FBX/체형 보정 기능을 수정하거나 대체하지 않습니다.
