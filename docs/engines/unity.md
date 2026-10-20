# 엔진 모듈: Unity

> 공통 원칙은 `docs/conventions/AssetNaming.md` · `CodingStyle.md` · `FolderStructure.md` · `DataDriven.md`.
> 여기엔 **Unity 에서 실제로 어떻게** 만 적는다. 검사(테스트·캡처) 방법은 `docs/checks/unity.md`.

## 에셋 이름 접두어

### 그림
| 종류 | 접두어 | 예시 |
|---|---|---|
| 스프라이트(2D 캐릭터·오브젝트) | `SPR_` | `SPR_Player_Hero_Run_01` |
| 스프라이트 시트 | `SPS_` | `SPS_Player_Hero_Run` |
| 스프라이트 아틀라스 | `SA_` | `SA_Characters` |
| 타일 / 타일팔레트 | `TL_` / `TP_` | `TL_Grass_01` / `TP_Forest` |
| 텍스처(3D·셰이더용) | `T_` | `T_Rock_D` |
| UI 이미지 | `UI_` | `UI_Btn_Confirm` |
| 아이콘 | `ICO_` | `ICO_Item_Potion` |
| 폰트 | `FNT_` | `FNT_NotoSansKR` |

### 3D / 재질 / 효과
| 종류 | 접두어 | 예시 |
|---|---|---|
| 정적 메시(FBX) | `SM_` | `SM_Barrel` |
| 스키닝 메시 | `SK_` | `SK_Player_Knight` |
| 머티리얼 | `M_` | `M_Water` |
| 머티리얼 인스턴스(변형) | `MI_` | `MI_Water_Dark` |
| 셰이더 / 셰이더그래프 | `SH_` | `SH_Dissolve` |
| 파티클·VFX | `VFX_` | `VFX_Slash_Fire` |
| 포스트프로세싱 볼륨 프로필 | `PP_` | `PP_Dungeon` |

### 움직임
| 종류 | 접두어 | 예시 |
|---|---|---|
| 애니메이션 클립 | `A_` | `A_Player_Hero_Attack_01` |
| 애니메이터 컨트롤러 | `AC_` | `AC_Player_Hero` |
| 오버라이드 컨트롤러 | `AOC_` | `AOC_Player_Hero_Mage` |
| 타임라인 | `TLN_` | `TLN_Intro` |
| 아바타 마스크 | `AM_` | `AM_UpperBody` |

### 소리
| 종류 | 접두어 | 예시 |
|---|---|---|
| 배경음 | `BGM_` | `BGM_Town_Day` |
| 효과음 | `SFX_` | `SFX_Hit_Metal_02` |
| 음성 | `VO_` | `VO_Npc_Elder_Greet` |
| 환경음 | `AMB_` | `AMB_Forest_Night` |
| 오디오 믹서 | `MIX_` | `MIX_Master` |

### 조립품 / 데이터
| 종류 | 접두어 | 예시 |
|---|---|---|
| 프리팹 | `PF_` | `PF_Enemy_Goblin` |
| ScriptableObject 데이터 | `SO_` | `SO_Item_Potion_Small` |
| 입력 액션 | `IA_` | `IA_Player` |
| 물리 재질 | `PHM_` | `PHM_Ice` |
| 렌더 텍스처 | `RT_` | `RT_Minimap` |

### 접두어 없음
- **씬**: `대분류_이름` — `Town_Hanyang`, `Dungeon_01`, `Boot`, `Title`
- **C# 스크립트**: 클래스 이름과 똑같이 — `PlayerController.cs`
- **폴더**: PascalCase, 접두어 없음

### 접미어 (텍스처)
| 접미어 | 뜻 |
|---|---|
| `_D` | 기본색(Albedo/Diffuse) |
| `_N` | 노멀 |
| `_M` | 마스크(메탈릭·AO 등 묶음) |
| `_E` | 발광(Emission) |
| `_R` | 거칠기 |
| `_A` | 알파 |

## AI 생성 이미지 들이기
가공 끝난 것만 이름을 규칙대로 바꿔 `Assets/_Project/Art/` 로 들인다. 원본·라이선스 기록 절차는 `docs/conventions/AssetNaming.md` "AI 생성 이미지" 참고.

## 코드 — 필드 접두어 (Unity 공식)
| 대상 | 규칙 | 예 |
|---|---|---|
| private/protected 멤버 필드(직렬화 포함) | `m_` + PascalCase | `[SerializeField] private float m_MoveSpeed;` |
| 상수(`const`) | `k_` + PascalCase | `private const int k_MaxStack = 99;` |
| static 필드 | `s_` + PascalCase | `private static int s_InstanceCount;` |
| public 필드 | 쓰지 않는다 → 프로퍼티로 | `public float Speed => m_Speed;` |

출처: Unity 공식 [Naming and code style tips for C# scripting](https://unity.com/how-to/naming-and-code-style-tips-c-scripting-unity) · [C# Code Style Guide (Unity 6)](https://unity.com/resources/c-sharp-style-guide-unity-6)

## 코드 — Unity 규칙 (실제 버그를 막는 부분)
1. **인스펙터 노출은 `[SerializeField] private`**, `public` 필드 금지. 왜: 아무 코드나 값을 바꿔 추적이 안 된다.
2. **`Update` 계열에서 `GetComponent`, `Find*`, `Camera.main` 반복 호출 금지** → `Awake` 에서 한 번 찾아 `m_` 필드에. 왜: 매 프레임 느려진다.
3. **밸런스 숫자를 코드에 박지 않는다** → 데이터(SO)로 (DataDriven.md). `k_` 상수는 "절대 안 바뀌는 기술 값"만.
4. **`Resources.Load` / 문자열로 찾기 지양** → 인스펙터 참조나 Addressables.
5. **이벤트 구독은 `OnEnable` 에서 `+=`, `OnDisable` 에서 `-=`.** 왜: 사라진 객체를 불러 에러.
6. **Unity 객체(GameObject·Component)에 `?.` / `??` 쓰지 않는다** → `if (m_Target != null)`. 왜: 파괴된 Unity 객체는 C# `?.` 로 null 이 아니다.
7. 코루틴·async 는 객체 파괴 시 멈추게 (`StopAllCoroutines`, `destroyCancellationToken`).
8. `Debug.Log` 는 태그로 시작: `Debug.Log("[Combat] ...")`. 커밋 전 임시 로그 삭제.
9. 에디터 전용 코드는 `Editor` asmdef 안에만. `#if UNITY_EDITOR` 는 최후 수단.

## 코드 — 예시
```csharp
using System;
using UnityEngine;

namespace MyGame.Gameplay.Combat
{
    public sealed class Health : MonoBehaviour, IDamageable
    {
        private const float k_MinHealth = 0f;

        private static int s_AliveCount;

        [SerializeField] private HealthConfig m_Config;   // 최대 체력·무적 시간은 데이터(SO_Player_Hero_Health)에서

        private float m_Current;
        private float m_InvincibleUntil;

        public bool IsDead => m_Current <= k_MinHealth;

        public event Action<float> HealthChanged;
        public event Action Died;

        private void Awake()
        {
            m_Current = m_Config.MaxHealth;
            s_AliveCount++;
        }

        public void TakeDamage(float amount)
        {
            // 무적 중이거나 이미 죽었으면 무시 (사망 이벤트 중복 방지)
            if (IsDead || Time.time < m_InvincibleUntil)
            {
                return;
            }

            m_Current = Mathf.Max(k_MinHealth, m_Current - amount);
            m_InvincibleUntil = Time.time + m_Config.InvincibleSeconds;
            OnHealthChanged(m_Current);

            if (IsDead)
            {
                OnDied();
            }
        }

        private void OnHealthChanged(float current)
        {
            HealthChanged?.Invoke(current);
        }

        private void OnDied()
        {
            s_AliveCount--;
            Died?.Invoke();
        }
    }
}
```
> ScriptableObject **클래스**는 접두어 없음(`HealthConfig`), 그걸로 만든 **에셋 파일**만 `SO_` (`SO_Player_Hero_Health`).

## 폴더 구조
```
Assets/                     (Unity 프로젝트가 하위 폴더면 unity/Assets/)
├─ _Project/                ← ★ 우리 것 전부
│  ├─ Art/
│  │  ├─ Sprites/           (대상별 하위 폴더: Characters/Hero, Enemies/Goblin …)
│  │  ├─ Tiles/  Textures/  Materials/  Models/  Animations/  VFX/  UI/  Fonts/
│  ├─ Audio/
│  │  ├─ BGM/  SFX/  VO/  AMB/  Mixers/
│  ├─ Data/                 ← ScriptableObject 데이터
│  │  ├─ Items/  Enemies/  Skills/  Quests/  Dialogue/  Balance/
│  ├─ Prefabs/
│  │  ├─ Characters/  Enemies/  Environment/  UI/  VFX/  Systems/
│  ├─ Scenes/
│  ├─ Scripts/
│  │  ├─ Runtime/           (Project.Runtime.asmdef)
│  │  │  ├─ Core/  Gameplay/  UI/  Data/  Audio/  Utils/
│  │  ├─ Editor/            (Project.Editor.asmdef — 에디터 전용)
│  │  └─ Tests/
│  │     ├─ EditMode/       (Project.Tests.EditMode.asmdef)
│  │     └─ PlayMode/       (Project.Tests.PlayMode.asmdef)
│  ├─ Input/
│  └─ Settings/             ← URP 에셋, 볼륨 프로필, 품질 설정
├─ _Sandbox/                ← 실험용. 빌드에 안 들어감. 합격하면 _Project 로 옮김
├─ ThirdParty/               ← 에셋스토어·외부 에셋 (수정 금지)
├─ Plugins/                 ← 네이티브 플러그인만
├─ Resources/                ← 가능하면 쓰지 않는다 (Addressables 권장)
└─ StreamingAssets/
```

## 유니티 파일 다루기 (연결이 끊기는 사고 방지)
`.meta` 는 "이 프리팹이 어떤 스프라이트를 쓰는지" 같은 **연결 정보**다. 에디터 밖에서 건드리면 연결이 끊긴다.
1. **파일 이동·이름 변경은 에디터 Project 창에서** (또는 Unity MCP 의 에셋 이동 명령 / `AssetDatabase.MoveAsset`). 탐색기·Finder·`mv`·`git mv` 금지.
2. **`.meta` 는 원본과 같이 커밋.** 원본만, 또는 `.meta` 만 커밋 금지.
3. **씬(`.unity`)·프리팹(`.prefab`)을 텍스트로 직접 고치지 않는다.** 에디터나 Unity MCP 명령으로 작업. 왜: YAML 속 ID 하나만 틀려도 참조가 조용히 사라진다.
4. 새 파일은 에디터가 `.meta` 를 만든 뒤(에셋 새로고침 후)에 커밋.

## 어셈블리 정의(asmdef)
- `Runtime` / `Editor` / `Tests.EditMode` / `Tests.PlayMode` 4개로 시작.
- 왜: 컴파일이 빨라지고, 에디터 전용 코드가 빌드에 섞여 들어가는 사고를 막는다.
- 네임스페이스 = `프로젝트명.폴더` (예: `MyGame.Gameplay.Combat`).

## 데이터 틀 예시 (DataDriven.md 참고)
```csharp
using UnityEngine;

namespace MyGame.Data
{
    [CreateAssetMenu(menuName = "MyGame/Enemy", fileName = "SO_Enemy_New")]
    public sealed class EnemyData : ScriptableObject
    {
        [SerializeField] private string m_Id = "enemy.new";
        [SerializeField] private string m_DisplayNameKey;      // 표시 이름은 로컬라이즈 키로
        [SerializeField] private float m_MaxHealth = -1f;      // -1 = 사람이 아직 안 정함
        [SerializeField] private float m_MoveSpeed = -1f;
        [SerializeField] private GameObject m_Prefab;          // PF_Enemy_* 연결
        [SerializeField] private ItemData[] m_Drops;           // SO_Item_* 직접 연결 (이름 문자열 X)

        public string Id => m_Id;
        public float MaxHealth => m_MaxHealth;
        public float MoveSpeed => m_MoveSpeed;
    }
}
```

## 브랜치 합치기 충돌
씬·프리팹 충돌: `UnityYAMLMerge` 로 시도 (`.gitattributes` + 설치 때 `git config merge.unityyamlmerge.driver` 설정) → 안 되면 INBOX `막힘:` 로 사람에게. AI가 씬 YAML 을 손으로 고치지 않는다.

## 버전
`Player Settings > Version` 을 `CHANGELOG.md`/태그와 같은 값으로. 안드로이드 `Bundle Version Code` 는 태그할 때마다 +1.

## Unity MCP (에디터 연결)
- **조회는 자유:** 콘솔 로그, 씬 계층, 컴포넌트 값, 테스트 실행 결과 읽기.
- **변경은 조건부:** 에셋 생성·이동, 컴포넌트 부착, 씬 저장은 — 감독/수동 작업이면 승인 후, 루프면 이번 조각 범위 안에서만.
- **금지:** 사람이 Play 중이거나 INBOX 에 `보류:` 가 있을 때 에디터 변경. 프로젝트 설정·패키지 변경은 INBOX 지시가 있을 때만.
