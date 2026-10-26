# LunaFlow · A 담당 / DirectX 11

계획서의 A1–A8을 구현한 Windows용 렌더링·애니메이션 작업 프로젝트다.
기존 `examples/example_win32_directx11` 예제와 별도로, 같은 ImGui 소스와 DX11 백엔드를 사용한다.

## 바로 실행하기

저장소 최상위의 **`Start-LunaFlow.cmd`** 를 더블클릭한다.
이미 빌드한 경우 `build/Release/LunaFlow.exe`를 직접 실행해도 된다.
기본 Luna 캐릭터와 Wave·Idle·Walk 동작이 내장되어 있어 FBX 없이 시작할 수 있다.

1. `Pause`로 정지하고, 가운데 타임라인을 눌러 시간을 고른다.
2. 왼쪽에서 `RightUpperArm` 같은 뼈를 선택한다.
3. 오른쪽 `Rotation` 숫자를 드래그한다. 현재 시각에 키가 기록되고 즉시 반영된다.
4. `Save motion`으로 **현재 선택한 동작 하나**를 JSON에 저장한다.
5. `Import motion`으로 저장한 키나 C가 만든 같은 뼈대의 동작을 다시 불러온다.
6. `Bake 30 FPS`로 수정과 재생 속도를 반영한 전체 로컬 자세를 내보낸다.

`Open character`로 리깅된 FBX와 그 안의 애니메이션을 불러온다.
glTF/GLB/OBJ도 지원한다. 텍스처가 별도 파일이면 원래 상대 경로를 유지한다.
파일명과 폴더명에 한글을 사용할 수 있다.

## 카메라와 표시

| 조작 | 기능 |
|---|---|
| 미리보기 왼쪽 드래그 | 캐릭터 주위를 회전 |
| 오른쪽 / 가운데 드래그 | 화면 이동 |
| 휠 | 확대·축소 |
| 미리보기 위에서 `F` | 캐릭터에 맞춰 다시 보기 |
| `Space` | 재생·일시 정지 |
| `Bones` | 뼈대 선과 선택 관절 표시 |
| `Wireframe` | 삼각형 메시 표시 |
| `Bind pose` | 원본 기준 자세 확인 |
| `Cube` | 회전하는 DX11 큐브로 렌더링 기반 확인 |

타임라인의 다이아몬드는 선택한 뼈의 키다. 클릭·드래그로 시간을 이동한다.
재생 중에는 자세 입력이 잠긴다. 정지 후 편집한다.
키를 수정하면 저장할 데이터 자체가 바뀌며, 미리보기만 잠깐 덮어쓰는 방식이 아니다.
`+ Duplicate motion`으로 원본 동작을 복사한 뒤 수정할 수 있다.
라이브러리는 메모리에 있으므로 필요한 동작은 각각 저장한 뒤 모델을 교체하거나 앱을 종료한다.

## Visual Studio에서 열기와 다시 빌드하기

필요 환경: Windows 10/11, Visual Studio의 **C++를 사용한 데스크톱 개발**, Windows SDK,
**Windows용 C++ CMake 도구**. C++17과 DirectX 11을 사용한다.
현재 PC에서는 Visual Studio 2026, x64, Release로 빌드했다.

빌드가 준비되어 있으므로 **`build/LunaFlow.slnx`** 를 Visual Studio로 연다.
시작 프로젝트는 `LunaFlow`, 구성은 `Release / x64`다. `F5`로 실행한다.
Visual Studio 2022에서 새로 구성하면 CMake가 해당 버전의 솔루션을 생성한다.
최상위 폴더를 Visual Studio의 '폴더 열기'로 열어 CMake 프로젝트로 작업해도 된다.

최상위 폴더의 PowerShell에서:

```powershell
.\Build-LunaFlow.ps1 -Configuration Release -Test
.\Build-LunaFlow.ps1 -Run
```

스크립트 실행이 제한된 PC에서 한 번 실행하는 방법:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build-LunaFlow.ps1 -Test
```

초기 구성 때 Assimp 6.0.4와 nlohmann/json 3.12.0을 공식 저장소에서 내려받는다.
버전과 SHA-256을 고정했으며, 이후에는 `build/_deps`의 파일을 재사용한다.
Assimp, JSON, ImGui는 앱에 함께 링크된다. Assimp DLL을 따로 복사할 필요가 없다.
다른 PC에 실행 파일을 전달할 때는 `Shaders`와 `licenses` 폴더도 함께 전달한다.
Visual C++ 런타임이 없는 PC에는 Visual Studio의 해당 런타임도 필요하다.

## A1–A8 구현 위치

| 계획 | 구현 |
|---|---|
| A1 · 렌더링 기반, 큐브 | `App/main.cpp`, `Core/DemoScene.cpp`, `Rendering/RendererDX11.cpp` |
| A2 · 패널 미리보기, 크기 변경, 카메라 | `RendererDX11`, `PreviewCamera`, `EditorApp::Viewport` |
| A3 · FBX, 메시, 재질, 텍스처 | `Core/SceneImporter.cpp`, WIC 텍스처 로딩 |
| A4 · 뼈 경로, 부모, 기준 자세, 가중치 | `Core/Scene.h`, `SceneImporter`, 뼈대 디버그 표시 |
| A5 · 시간별 평가, 보간, 반복 | `Core/Animation.cpp`의 `EvaluatePose` |
| A6 · GPU 스키닝 | `Rendering/Shaders/Skinned.hlsl`, 메시별 스킨 팔레트 |
| A7 · 키 수정, 도↔쿼터니언, 저장 연결 | `SetTransformKey`, `DeleteTransformKey`, `EditorApp`, `ClipIO` |
| A8 · 30FPS 최종 자세 출력 | `BakeClip`, `SaveBake` |

## B·C와 연결하기

공통 데이터와 함수는 `Core/Scene.h`에 있다. UI와 애니메이션 평가기는 서로 독립적이다.

```cpp
// B: 선택한 동작과 시간을 미리보기에 반영한다.
Pose pose = EvaluatePose(model.skeleton, &model.clips[clipIndex], seconds, false);

// B: UI에서 수정한 로컬 TRS를 원본 동작의 시간에 기록한다.
Transform edited = pose.local[boneIndex];
edited.rotation = QuaternionFromDegrees({20, 0, 80});
SetTransformKey(clip, boneIndex, seconds * clip.speed, edited);

// B: 패널 크기를 전달하고, SRV를 ImGui 이미지로 사용한다.
renderer.Resize(width, height);
renderer.Render(model, pose, camera, options);
ID3D11ShaderResourceView* preview = renderer.Texture();

// C: 동일한 캐릭터에 맞춘 동작을 추가한다.
AnimationClip generated = LoadClip("generated.json", model.skeleton);
model.clips.push_back(std::move(generated));

// C: 미리보기와 같은 평가기로 최종 자세를 받는다.
BakedClip finalPoses = BakeClip(model.skeleton, clip, 30);
SaveBake("baked.json", model.skeleton, finalPoses);
```

`RendererDX11`는 전달받은 Device/Context를 참조하고 자체 자원만 관리한다.
렌더링 뒤 출력 타깃을 해제한다. B의 프레임에서는 3D 장면을 먼저 그리고,
메인 화면의 렌더 타깃을 다시 지정한 다음 ImGui를 그린다.
미리보기 SRV는 다음 `Resize` 또는 렌더러 파괴 전까지 유효하다.
Device/Context를 사용하는 함수와 앱의 모델 교체는 UI/렌더 스레드에서 호출한다.
C의 네트워크 응답은 그 스레드로 전달한 뒤 적용한다.

### 좌표와 시간 규칙

- 내부는 **왼손 좌표계, +Y 위쪽, 미터**, DirectX 행 벡터다.
- 로컬 행렬은 `S * R * T`, 계층은 `local * parentGlobal`이다.
- Assimp 행렬은 가져올 때 한 번 전치한다. FBX의 축 메타데이터와 단위를 한 번 적용한다.
- FBX 단위는 자동 변환된다. `Import settings`의 추가 배율은 잘못된 메타데이터 보정에만 쓴다.
  OBJ는 단위 정보가 없으므로 원본 숫자를 미터로 해석한다.
- 뼈 목록은 **전체 장면 노드**를 포함하며 부모가 자식보다 앞에 온다.
  중간 변환 노드를 제거하지 않는다. 전체 `path`가 외부 동작의 식별자다.
- 쿼터니언은 `[x, y, z, w]`다. Euler UI는 DirectX의 Pitch(X)·Yaw(Y)·Roll(Z) 순서다.
- `clip.duration`과 키 시간은 **원본 동작의 초**다. 전달하는 `seconds`는 재생 시간이다.
  재생 길이는 `duration / speed`이며, 평가기는 `seconds * speed`를 조회한다.
- 위치·크기는 선형 보간, 회전은 정규화한 Slerp를 쓴다.
  없는 채널은 해당 뼈의 원본 기준 자세를 쓴다.
- 반복 재생은 `[0, 재생 길이)`다. 정지 화면과 Bake는 반복을 끄므로 끝 키를 보존한다.
- 메시별 스킨은 `inverseBind * boneGlobal`, 정점당 상위 4개 가중치를 정규화한다.
  메시 하나의 스킨 뼈는 최대 255개이며, 가중치 없는 정점은 메시 노드에 연결한다.

### 동작 JSON: `lunaflow.clip`, 버전 1

`Assets/TeamC_MotionExample.json`을 내장 Luna 모델에서 `Import motion`으로 열어 볼 수 있다.
`positions`, `rotations`, `scales`는 생략할 수 있다. 제공한 채널은 시간 순서의 절대 로컬 값이다.

```json
{
  "format": "lunaflow.clip", "version": 1,
  "coordinates": {
    "handedness": "left", "up": "+Y", "unit": "meter", "quaternion": "xyzw"
  },
  "id": "wave-001", "name": "Wave", "duration": 2.0, "speed": 1.0,
  "tracks": [{
    "bonePath": "/Root/Hips/Chest/RightUpperArm",
    "rotations": [
      {"time": 0.0, "value": [0, 0, 0, 1]},
      {"time": 2.0, "value": [0, 0, 0.7071068, 0.7071068]}
    ]
  }]
}
```

다른 캐릭터에서는 `Save skeleton JSON`으로 뼈 경로와 기준 TRS를 먼저 받는다.
C가 생성 모델의 좌표·기준 자세·관절 이름을 이 뼈대로 리타기팅한 뒤 전달해야 한다.
없는 뼈, 중복 트랙/키 시간, 유효하지 않은 쿼터니언, 길이 밖의 키, 다른 좌표 규칙은 오류로 반환한다.
리타기팅 자체와 AI 서비스 호출은 C가 연결할 부분이다.

### 최종 자세 JSON: `lunaflow.bake`, 버전 1

`clipId`, `name`, 재생 속도를 반영한 `duration`, `fps`, `bones`, `frames`를 포함한다.
각 프레임은 `{ "time": ..., "local": [ TRS, TRS, ... ] }`이며 배열 순서는 `bones`와 같다.
TRS는 `translation[3]`, `rotation[4]`, `scale[3]`이다.
0초, 1/30초 간격, **정확한 끝 시각**을 포함하며, 끝이 프레임 간격과 맞지 않으면 마지막 간격만 짧아진다.
C는 이 좌표를 Blender/FBX 기준으로 변환하고 동일한 뼈 경로의 로컬 자세에 적용한다.
원본 FBX와 기준 자세를 보존하고, 스킨 바인드를 새 기준 자세로 재생성하지 않는다.

## 검증과 현재 범위

`LunaFlowTests`는 보간·부모 계층·반복·끝 시각·속도 변경·키 수정·도/쿼터니언 변환,
한글 경로 JSON 왕복·잘못된 입력 거부·Bake 일치를 검사한다.
Assimp의 실제 FBX 샘플로 단위, 뼈, 가중치, 애니메이션을 검증하고,
DirectX 11 WARP에서 셰이더를 실행해 자세 변경에 따라 이미지가 바뀌는지 검사한다.
텍스처와 크기 변경, PNG 저장, 설치되어 있으면 DirectX 디버그 레이어 오류도 검사한다.
이미지는 `build/test-output/`에 저장된다.

확인한 것은 내장 캐릭터와 Assimp 테스트 모델이다. 팀원의 실제 캐릭터가 준비되면
Blender에서 정면·기준 자세·기존 동작을 나란히 비교해 최종 확인해야 한다.

현재 재질은 기본 색상·diffuse/base-color 텍스처·기본 조명이다.
Windows WIC에서 읽는 PNG/JPEG 등의 텍스처를 지원하며, 실패하면 기본 색상을 사용한다.
PBR 전체 재질, 모프 타깃, 제약조건/IK, Unity 확인, Blender 기반 FBX 작성,
AI 호출과 자동 리타기팅, B의 전체 편집기/프로젝트 저장 기능은 이 A 모듈의 범위 밖이다.
