# Unity 연결

1. LunaFlow에서 `파일 > 내보내기 > Unity 패키지`를 선택한다.
2. Unity 프로젝트의 **Assets/LunaFlow** 아래에 `Scene.lunaflow.json`으로 저장한다.
   다른 곳에 내보냈다면 생성된 폴더 전체를 Assets/LunaFlow로 복사한다.
3. Runtime·Editor 스크립트가 컴파일되고 FBX가 가져와지면 `.controller`와 `.prefab`이 생성된다.
4. 생성된 프리팹을 장면에 놓고 Play를 누른다.
5. 다른 동작은 Animator의 해당 상태를 기본 상태로 바꾸거나 `Animator.Play(동작 이름)`으로 재생한다.

모델·텍스처·스킨·여러 Take는 FBX에, 효과 종류·부착 뼈·색상·크기·재생 구간은 옆의 JSON에 저장된다.
검의 부모 뼈와 부착 위치는 FBX 계층에 포함된다. 칼 궤적·스파크·오라는 Runtime 스크립트가 재구성한다.
Unity VFX Graph 패키지 설치는 필요하지 않다.

Unity 2022 LTS / Unity 6의 Generic 리그용 통합이다. 직접 작성한 C# 스크립트는 프로젝트에 포함된다.
여러 장면은 같은 Assets/LunaFlow 폴더 아래에 다른 이름으로 내보내어 공유 스크립트가 중복되지 않게 한다.
다시 가져온 뒤 프리팹이 필요하면 FBX를 선택하고 `LunaFlow > Rebuild selected scene prefab`을 누른다.

셰이더는 URP 파티클 Unlit, 기본 파티클 Unlit, 기본 Sprite 순으로 사용 가능한 것을 선택한다.
HDRP나 커스텀 렌더 파이프라인을 쓰면 Runtime에서 사용할 효과 머티리얼을 해당 프로젝트에 맞게 교체해야 한다.
Animator의 Root Motion, 전환·블렌딩·게임 플레이 중의 IK는 게임 프로젝트의 설정에 따른다.

LunaFlow 쪽에서는 모델·클립·부착·효과 데이터의 저장 및 FBX 재불러오기 검증을 수행한다.
Unity 프로젝트에 통합할 때는 사용하는 렌더 파이프라인과 게임의 Animator 설정도 확인한다.
