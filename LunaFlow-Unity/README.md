# LunaFlow → Unity

LunaFlow가 저장한 **`.lunaflow` 프로젝트를 Unity 프리팹으로 직접 가져오는 독립 패키지**입니다. 원본 C++ 프로젝트를 빌드하거나 FBX를 다시 내보낼 필요 없이 파일에 포함된 메시, 뼈대, 동작, 재질, VFX를 읽습니다.

## 설치 및 사용

1. `LunaFlow-Unity` 폴더 전체를 원하는 위치에 복사합니다. 집에서는 기존 소스 옆에 같은 폴더를 두면 됩니다.
2. Unity 6 프로젝트에서 **Window → Package Management → Package Manager**를 엽니다.
3. `+` → **Install package from disk / Add package from disk**를 눌러 이 폴더의 `package.json`을 선택합니다.
4. LunaFlow에서 프로젝트를 저장한 뒤 `.lunaflow` 파일을 Unity의 `Assets` 폴더에 복사하거나 드래그합니다.
5. 가져온 에셋을 **Hierarchy/Scene에 드래그**합니다. Inspector의 **현재 씬에 배치** 버튼도 사용할 수 있습니다.
6. **Play**를 누르면 저장 시 선택했던 동작이 재생됩니다. `LunaFlowPlayer` Inspector에서 다른 동작 재생, 일시 정지, 계속 재생, 시간 이동을 할 수 있습니다.

샘플은 Package Manager에서 **LunaFlow Project Importer → Samples → Animated sample → Import**로 추가합니다. `Wave.lunaflow`에는 팔 움직임 2개와 손에 붙는 파티클이 들어 있습니다. 씬에 놓은 뒤 Scene 뷰에서 선택하고 `F`를 누르면 찾기 쉽습니다.

원본 `.lunaflow`를 수정하면 자동으로 다시 가져옵니다. 생성된 메시·재질·AnimationClip·AnimatorController는 해당 파일의 하위 에셋입니다. 가져온 결과를 직접 수정하면 다음 가져오기에서 재생성되므로, 수정용 프리팹은 **Prefab Variant**로 만드세요.

## 포함 기능

- 뼈대 계층, 여러 메시, 메시별 스킨 팔레트, 최대 4개 가중치, inverse-bind 행렬 변환.
- 내장 이미지/원시 RGBA 텍스처, 기본 색상, UV 방향 변환.
- 모든 동작을 Generic `AnimationClip`으로 생성. 위치·회전·크기, 동작 속도와 반복 설정 반영.
- 쿼터니언 회전을 보간하여 기본 30 FPS로 샘플링. 원래 키 시점도 보존하며 Inspector에서 1~120 FPS로 조절.
- 키가 없는 채널은 기준 자세를 사용하여 동작 전환 후 이전 자세가 남는 문제 방지.
- Unity `AnimatorController`, Generic Avatar, `LunaFlowPlayer` 자동 구성.
- 파티클 설정을 Unity `ParticleSystem`으로 변환하고 원래 뼈에 부착. 재생 시간/동작에 맞춰 활성화.
- 메시/키/스킨/뼈대 순환 등 잘못된 데이터 검사와 가져오기 예산 제한.
- 동작 ID에 따른 하위 에셋 식별자를 유지하여 같은 파일의 재가져오기 시 AnimationClip 참조 보존.

## 게임 코드에서 재생

```csharp
using LunaFlow.UnityImporter;
using UnityEngine;

public class CharacterDemo : MonoBehaviour
{
    [SerializeField] private LunaFlowPlayer player;

    public void Wave() => player.Play("wave"); // 동작 ID 또는 표시 이름
    public void Pause() => player.Pause();
    public void Resume() => player.Resume();
    public void Scrub(float seconds) => player.Seek(seconds); // 이동한 위치에서 일시 정지
}
```

실제 동작 목록은 `player.Project.motions`에 있습니다. Unity 시간 단위는 초이며 LunaFlow의 속도가 반영된 재생 시간을 사용합니다. 직접 Animator를 제어해도 현재 상태를 읽어 VFX를 맞춥니다. 기본 컨트롤러는 동작을 즉시 전환하며 전환 블렌딩/루트 모션을 자동 구성하지 않습니다.

## 범위와 호환성

- 입력 계약: `format: lunaflow.project`, `schema_version: 1`, 동작 `lunaflow.clip` 버전 1. 왼손 좌표, +Y 위쪽, 미터, xyzw 쿼터니언.
- 지원 기준: Unity 6. 이전 Unity 버전은 미검증입니다. Built-in 재질을 기본 사용하며 URP에서는 URP/Lit를 찾습니다. 다른 렌더 파이프라인은 **모델 셰이더**를 직접 지정하세요. HDRP는 미검증입니다.
- **Generic 애니메이션**을 가져옵니다. Unity Humanoid Avatar 매핑/다른 체형 자동 리타기팅은 이 패키지의 구현 범위에 포함되지 않습니다.
- 파티클은 두 엔진의 난수/방출/셰이더 차이 때문에 외형이 완전히 같지는 않습니다. 기존 Trail/Sparks/Aura는 Unity 컴포넌트로 근사 변환하며 경고를 표시합니다.
- 파티클 재생은 동작 시간에 동기화됩니다. 세계 좌표 파티클을 시간 이동하거나 되감으면 현재 뼈 위치를 기준으로 다시 시뮬레이션하므로 과거 이동 궤적까지 복원하지는 않습니다. Trail 역시 실제 재생 중의 이동으로 생성됩니다.
- `.lunaflow` 파일은 Unity **Editor에서 가져옵니다**. 빌드된 게임에서는 변환된 프리팹을 사용하며, 임의 JSON 파일을 실행 중 로드하는 기능은 없습니다.
- `.lunaflow`는 포함된 메시/텍스처를 사용하므로 원본 FBX의 로컬 경로가 없어도 됩니다. 포함되지 않은 외부 텍스처는 Unity `Assets`/`Packages` 내부에 있는 경우에만 찾습니다. 카메라/선택 상태 등 편집 전용 정보는 가져오지 않습니다.
- 기존 `unity_tools`의 FBX + `.lunaflow.json` 방식은 별도로 계속 사용할 수 있습니다. 이 패키지는 `.lunaflow` 확장자만 등록합니다.

## 검증 도구

`Tools~`는 Unity가 컴파일하지 않는 개발 도구 폴더입니다. .NET SDK 9 이상이 있으면 Unity 라이선스 없이 파일 계약 검사를 실행할 수 있습니다.

```powershell
# LunaFlow-Unity 폴더에서 실행
dotnet run --project './Tools~/ContractTests.csproj' -- './Samples~/AnimatedSample/Wave.lunaflow' './Tools~/Fixtures/EditorSnapshot.lunaflow'
& './Tools~/Compile.ps1' -EditorPath 'C:/Program Files/Unity/Hub/Editor/6000.3.5f2/Editor'

# 유효한 라이선스가 있는 Unity Editor에서 통합 검사
& './Tools~/Test-Unity.ps1' -EditorPath 'C:/Program Files/Unity/Hub/Editor/6000.3.5f2/Editor'
```

통합 검사는 기본 `.test-project`에 독립 Unity 프로젝트를 만들고 샘플 및 C++ 에디터의 저장 파일을 실제로 가져옵니다. 기존 Unity 프로젝트는 덮어쓰지 않습니다. 결과는 `LunaFlowVerification-result.json`, 자세한 로그는 `verification.log`에 기록됩니다. 그래픽 장치가 있으면 동작 전후 PNG도 저장합니다. `-NoGraphics`는 이미지 검증을 생략합니다. `-SkipAssemblyUpdater`는 이미 현재 API로 작성된 코드를 검사할 때 Unity의 자동 API 변환을 생략하는 선택 옵션입니다.

이 패키지는 LunaFlow 프로젝트 루트에 병합되어 있습니다. 기존 `unity_tools`(FBX + `.lunaflow.json`)와 충돌하지 않는 구조와 저장 형식 변경 시 대응 방법은 [MERGING.ko.md](MERGING.ko.md)에 정리되어 있습니다.
병합 후 현재 C++ 에디터가 저장한 `.lunaflow` 파일(테스트 출력 및 기본 캐릭터 모션캡처 테이크 포함)로 .NET 계약 검사를 다시 실행해 모두 통과했습니다.

2026-10-28 검증 결과: Unity **6000.3.5f2 / Built-in / Direct3D 11**에서 가져오기와 Play 모드 검사 **37개**, .NET에서 계약 검사 **18개** 통과. 자세한 범위는 [VALIDATION.md](VALIDATION.md)에 있습니다.

## 배포 묶음 만들기

Python이 있으면 `python './Tools~/Pack.py'`로 테스트 캐시를 제외한 소스·샘플·안내서 묶음 `Dist~/LunaFlow-Unity-0.1.0.zip`을 만들 수 있습니다(버전 관리 제외). ZIP 내부의 `TRANSFER-SHA256.json`에는 각 파일의 해시가 들어 있습니다.
