#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/SnareGunTracerActor.h"
#include "Engine/NetSerialization.h"
#include "MeleeAttackComponent.generated.h"

class AAnimalCharacter;
class FJsonObject;
struct FItemRow;
struct FPlayerCombatRow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMeleeTargetChangedSignature, AAnimalCharacter*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMeleeHitSignature, AAnimalCharacter*, Target, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAnimalKilledSignature, AAnimalCharacter*, Target, const TArray<FName>&, DroppedItemIDs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGunReloadReplySignature, bool, bAccepted, const FString&, Code);

/** 총알 한 발이 어디서 멈췄나 (M39-03). 튀김 종류(땅·나무·바위)는 보이는 총알(M39-05)이 `World` 를 더 나눈다. */
enum class ESnareGunHitKind : uint8
{
	Miss,     // 막는 것 없이 사거리 끝까지 — 땅 먼지
	Animal,   // 사냥 동물에 닿는다 — 피해는 도착 시각에
	World     // 처음 막는 나무·바위·땅(WorldStatic)에 박힌다
};

/** 호스트가 판정한 한 발. 피해는 쏜 순간이 아니라 `FlightSeconds` 뒤에 들어간다 — 보이는 총알(M39-05)이 같은 값을 쓴다. */
struct FSnareGunShot
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	ESnareGunHitKind HitKind = ESnareGunHitKind::Miss;
	TWeakObjectPtr<AAnimalCharacter> Target;
	float Damage = 0.f;
	/** 펠릿 총의 알 수와 그중 과녁에 맞은 알 수 (알 하나짜리 총은 1 / 맞으면 1). */
	int32 PelletCount = 1;
	int32 PelletsHit = 0;
	float FlightSeconds = 0.f;
	/** 도착 자리 튀김 (M39-05). 빗나감은 사거리 끝 아래 땅의 흙먼지. */
	ESnareGunSplash Splash = ESnareGunSplash::Dust;
	/** 보이는 총알이 박히는 자리·날아가는 시간 — 빗나감만 사거리 끝 아래 땅으로 내려가 `End`·`FlightSeconds` 와 다르다. */
	FVector SplashEnd = FVector::ZeroVector;
	float SplashFlightSeconds = 0.f;
};

// ROADMAP M1-03: 플레이어의 근접 공격. DESIGN 3.3 의 "자동 상호작용" 그대로 —
// 조준이 없고, 사거리 안에서 **가장 가까운 동물 한 마리**를 스스로 골라 하이라이트하고 때린다.
// 숫자(데미지·사거리·간격·타격 지연)는 DESIGN 3.1 확정 수치이고 **코드에 없다** — `DT_PlayerCombatTable`
// 의 `Player` 행이 `ApplyCombatRow` 한 곳으로 들어온다 (M11-05, 감독 지시 2026-09-23 17:25 —
// 그 전에는 코드 기본값이라 값을 넣는 길이 검사뿐이었다). 바꾸려면 설계서부터 (DataDriven.md 3).
// 권위: 동물 HP 판정은 호스트 몫이라 여기서 직접 깎아도 된다. **재화·인벤토리는 건드리지 않는다** —
// 이 컴포넌트는 죽인 순간 드롭 목록을 `OnAnimalKilled` 로 알리기만 하고(M1-04), 지급 요청을 쌓는 건
// 그걸 듣는 쪽(ASnarehavenCharacter → UInventoryComponent)이며 보유 반영은 백엔드다 (CLAUDE.md 8 · DESIGN 4.1·4.5).
// 멀티(M3-02): **게스트는 직접 깎지 않는다.** `AttackOnce` 를 게스트에서 부르면 `Server_AttackOnce`
// 요청만 나가고, 대상 찾기와 데미지는 호스트가 자기 쪽 좌표로 다시 한다 (DESIGN 4.1 권위 표).
// 대상을 인자로 보내지 않는 것이 요점이다 — 게스트가 "저 동물" 이라고 지목할 수 있으면 사거리 밖
// 동물도 때릴 수 있다. 결과는 동물의 `UAnimalStateComponent` 복제로 게스트에 돌아온다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UMeleeAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeAttackComponent();

	/**
	 * 공격 버튼을 누른 순간. **한 번 누름 = 휘두르기 한 번**이고 누르고 있어도 반복하지 않는다 —
	 * 그 뒤 `AttackIntervalSeconds` 동안 들어온 누름은 버린다(미리 눌러 두기 없음). 사용자 2026-10-04:
	 * 「레퍼런스는 한 번 클릭, 클릭하면 다시 누를 수 있을 때까지 딜레이가 있다」 (DESIGN 3.1 공격 간격).
	 * 전기톱처럼 「누르는 동안 버티던」 도구도 같은 길이다 — 한 번 누르면 정해진 길이만큼 돈다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartAttacking();

	/**
	 * 공격 버튼을 뗀 순간. 누름 상태(`IsAttacking`)만 끈다 — 이미 시작한 휘두르기 한 바퀴는 클립 끝까지 돌고
	 * 그 안에 예약된 타격도 그대로 나간다 (사용자 결함 2026-09-23 16:40 "짧게 누르면 몇 프레임만
	 * 돌고 대기로 툭 끊긴다"). 짧게 눌러도 한 대는 나가야 모바일에서 탭 공격이 성립한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopAttacking();

	/**
	 * 휘두르기 한 바퀴. 몸 클립을 처음부터 틀라고 알리고(`GetSwingCount` 가 는다),
	 * `AttackHitDelaySeconds` 뒤에 `AttackOnce()` 로 타격을 판정한다 (DESIGN 3.1 "공격 타격 지연").
	 * 입력이 타는 길은 전부 이쪽이다 — `AttackOnce` 를 직접 부르는 것은 2층 검사뿐이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SwingOnce();

	/**
	 * 지금 대상 한 대 — **지연 없이 즉시** 판정한다. 대상을 다시 찾고 `UAnimalStateComponent::ApplyDamage` 를 부른다.
	 * 사람 입력은 이걸 바로 부르지 않는다 (`SwingOnce` 가 타격 지연 뒤에 부른다).
	 * 2층 검사는 이 함수를 직접 부른다 (타이머를 기다리지 않으려고).
	 * @return 호스트에서 실제로 때렸으면 true. **게스트에서는 요청만 보내고 언제나 false** —
	 *         맞았는지 여부는 동물 상태 복제로 온다 (M3-02).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool AttackOnce();

	/**
	 * 지금 누르면 동작이 나가는가 — 지난 동작에서 공격 간격이 지났나. 간격이 미정(-1)이거나 월드가 없으면 true.
	 * 채집·도구질도 같은 간격을 쓴다 (`ASnarehavenCharacter::BeginAttack` 이 채집보다 먼저 본다).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsActionReady() const;

	/**
	 * 동작 한 번의 길이(초) = 다음 누름까지의 간격. 손에 든 행이 「정해진 길이」면 그 쓰기 클립 길이
	 * (`ASnarehavenCharacter::GetHeldFixedUseSeconds`, 전기톱·고급/전기 곡괭이), 아니면 공격 간격.
	 */
	float GetActionLengthSeconds() const;

	/** 지금부터 동작 길이(`GetActionLengthSeconds`) 동안 누름을 버린다. 휘두르기가 아닌 동작(채집 도구질)이 나갔을 때 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void MarkActionUsed();

	/** 간격 안이라 버린 누름 수. 2층 검사가 "두 번째 누름이 무시됐나" 를 센다. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetIgnoredPressCount() const { return IgnoredPressCount; }

	/**
	 * 지금 휘두르는 중인지. 몸 클립을 고르는 `ASnarehavenCharacter::UpdateBodyAnimation` 이 이걸 본다 —
	 * 버튼을 눌렀는지(`IsAttacking`)가 아니라 **동작이 도는 중인지**라야 떼는 순간 대기로 끊기지 않는다.
	 * 한 바퀴 길이는 공격 간격과 같다 (DESIGN 3.1 "휘두르기 몸 클립 유지" — 새 숫자가 아니다).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsSwinging() const { return bSwinging; }

	/**
	 * 시작한 휘두르기 수. 몸 클립을 **휘두르기마다 처음으로 되감는** 신호다 — 되풀이 재생에 기대면
	 * 반복 타이머와 클립 위치가 서서히 어긋난다 (사용자 결함 2026-09-23 16:40 ②).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetSwingCount() const { return SwingCount; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttackHitDelaySeconds() const { return AttackHitDelaySeconds; }

	/**
	 * 타격 지연(초)을 바꾼다. 2층 검사가 테스트 전용 값으로 부른다 (Verification.md 6).
	 * 음수는 미정 취급이라 바꾸지 않고 경고만 남긴다 (`SetAttackSettings` 와 같은 처리).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetAttackHitDelaySeconds(float Seconds);

	/** 이 폰의 공격을 여기서 판정해도 되는지 (호스트인지). 액터에 안 붙었으면 권위로 본다. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool HasAttackAuthority() const;

	/** 게스트 → 호스트 공격 요청. 대상은 호스트가 자기 쪽 좌표로 다시 고른다 (인자 없음이 의도다). */
	UFUNCTION(Server, Reliable)
	void Server_AttackOnce();

	/**
	 * 사거리 안 가장 가까운 동물을 다시 찾아 하이라이트를 옮긴다.
	 * @return 새 대상 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	AAnimalCharacter* RefreshTarget();

	/**
	 * 감쇠 시작 비율 `s` 와 끝거리 데미지 비율 `m` 을 바꾼다. 2층 검사가 테스트 전용 값으로 부른다.
	 * 음수는 미정 취급이라 바꾸지 않고 경고만 남긴다 (`SetAttackSettings` 와 같은 처리).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetFalloffSettings(float StartRatio, float MinRatio);

	/**
	 * **공격 네 숫자가 들어오는 유일한 자리** (ROADMAP M11-05 · 감독 지시 2026-09-23 17:25).
	 * `DT_PlayerCombatTable` 의 행(DESIGN 3.1 확정 수치 표의 사본)을 받아 데미지·사거리·간격·타격 지연을
	 * 한 번에 채운다 — 부르는 곳은 `ASnarehavenCharacter::PostInitializeComponents` 하나다.
	 * 타격 지연만 따로 넣는 길을 두지 않는 것이 요점이다: 값을 넣는 곳이 검사뿐이면 실제 플레이는
	 * 한 번도 그 길을 안 지나간다 (DataDriven.md 3).
	 * 칸 하나라도 음수(미정)면 **아무 칸도 바꾸지 않고** false 를 돌려준다 (Verification.md 7).
	 * @return 네 칸이 전부 정해져 있어 실제로 채웠으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool ApplyCombatRow(const FPlayerCombatRow& Row);

	/**
	 * 데미지·사거리·간격을 바꾼다. 2층 검사가 테스트 전용 값으로 부른다 (Verification.md 6).
	 * 음수가 들어오면 그 값은 미정 취급이라 바꾸지 않고 경고만 남긴다 (Verification.md 7).
	 * @param Damage          1타 데미지
	 * @param RangeCm         사거리 = 하이라이트 거리(cm)
	 * @param IntervalSeconds 공격 간격 = 다음 누름까지 기다리는 시간(초)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetAttackSettings(float Damage, float RangeCm, float IntervalSeconds);

	/**
	 * 때리는 손맛 중 **때린 사람 기기에서만** 도는 것 — 히트스톱·카메라 흔들림 (M21-02 · DESIGN 3.1).
	 * 호스트 판정(`AttackOnceOnAuthority`)이 명중 직후 부른다. 호스트 자기 폰이면 그 자리에서, 게스트 폰이면
	 * 그 게스트 기기에서 실행된다 — 다른 사람 화면은 안 멈추고 안 흔들린다.
	 */
	UFUNCTION(Client, Unreliable)
	void Client_PlayHitFeel(AAnimalCharacter* Target);

	/**
	 * 주간 우두머리 보상 묻기 (M21-07f, DESIGN 10.6 「우두머리 보상 주기」) — 호스트가 우두머리 처치 때 막타 사람과
	 * 같이 잡기 창(M21-06) 안 명중자 **각자의 컴포넌트**에 한 번씩 보낸다. 받은 기기가 **자기 계정으로** 서버
	 * `ClaimBoss` 를 부르고 답을 알림 한 줄로 띄운다 — 굴림·주 1회·지급은 전부 서버 몫이다 (CLAUDE.md 8).
	 */
	UFUNCTION(Client, Reliable)
	void Client_ClaimBossReward();

	/** 이 컴포넌트가 받은 우두머리 보상 묻기 횟수. 2층이 "명중자 수만큼 불렀는가" 를 센다. */
	int32 GetBossClaimRequestCount() const { return BossClaimRequestCount; }

	/**
	 * 서버 `ClaimBoss` 답 → 알림 한 줄. 당첨 모자·당첨 코인·꽝·이번 주 이미 받음·그 밖 거절.
	 * @return 띄울 문장 (빈 경우 없음)
	 */
	static FText DescribeBossClaimReply(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error);

	/** 히트스톱이 걸려 있는 중인가. 2층 검사가 본다. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHitStopActive() const { return bHitStopActive; }

	/** 지금 하이라이트된 동물. HUD 의 포획 버튼(M1-08)이 이걸 본다. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	AAnimalCharacter* GetCurrentTarget() const { return CurrentTarget; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAttacking() const { return bIsAttacking; }

	/**
	 * 이 컴포넌트가 **실제로 맞춘** 타격 수. 나간 입력 수가 아니라 데미지가 들어간 횟수다
	 * (사거리 밖·대상 없음은 안 센다). 3층 로그가 "몇 타째인지" 를 이 값으로 찍는다 — 루프#153 의
	 * "5타 뒤 스태미나" 인용이 타격 수가 안 보여 증거로 못 선 것을 푸는 값이다.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetLandedHitCount() const { return LandedHitCount; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttackDamage() const { return AttackDamage; }

	/** 아무것도 안 든 맨손 사거리(cm). 상호작용 거리(채집·목장·존)가 이걸 공유한다 — 무기를 바꿔도 안 변한다. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttackRangeCm() const { return AttackRangeCm; }

	/** 상인·의뢰인 대화 거리(cm). DESIGN 3.1 = 250 — 표에서 온다. 미정이면 음수(상인이 안 열린다). */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetNpcTalkRangeCm() const { return NpcTalkRangeCm; }

	/** 시험·표 경로 공용 세터. */
	void SetNpcTalkRangeCm(float RangeCm) { NpcTalkRangeCm = RangeCm; }

	/** 같이 잡기 드롭 창(초) — 표(`ApplyCombatRow`)와 검사가 같은 길로 넣는다. 음수면 막타만 받는다 (M21-06). */
	void SetShareDropWindowSeconds(float Seconds) { ShareDropWindowSeconds = Seconds; }

	/**
	 * 지금 든 무기 기준 실제 사거리(cm). 대상 찾기와 감쇠가 둘 다 이걸 본다.
	 * 무기 행의 값이 0 보다 크면 그 값, 아니면 맨손 기본값이다 (DESIGN 8.6-b "0 이면 컴포넌트 기본값").
	 * **채집·목장 거리는 일부러 `GetAttackRangeCm()` 쪽에 남겨 뒀다** — 총기가 들어오면(8절 항목7)
	 * 사거리만 100m 로 늘어나는데 그걸 따라 베리 따는 거리까지 늘어나면 안 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetEffectiveAttackRangeCm() const;

	/**
	 * 거리 `d` 에서 남는 데미지 비율 (DESIGN 8.6-b 곡선: `d <= R*s` 는 1.0, `R*s < d <= R` 은 1.0→m 선형).
	 * 사거리 밖은 애초에 타격이 안 나가므로 여기서는 `m` 을 돌려준다 (끝거리와 같은 취급).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetDamageRatioAtDistance(float DistanceCm) const;

	/**
	 * 펠릿 총이 과녁에 맞히는 알 수 (DESIGN 8.6-c 펠릿 표). 알 `PelletCount` 개가 조준선 좌우 ±`SpreadHalfDegrees` 에
	 * 고르게 놓이고(무작위 없음 — 시험이 같은 답을 얻게), 조준선에서 `atan(TargetRadiusCm / DistanceCm)` 도 안에 든 알만 맞는다.
	 * 알 하나짜리(`PelletCount` ≤ 1)는 조준선 한가운데라 늘 맞는다. 가까울수록 많이, 멀수록 적게 맞는다.
	 */
	static int32 CountPelletHits(int32 PelletCount, float SpreadHalfDegrees, float DistanceCm, float TargetRadiusCm);

	/**
	 * 폭발 피해 (DESIGN 8.6-c 폭발 피해 줄). 중심 `DirectDamage`, 반경 가장자리 `DirectDamage * EdgeRatio`, 그 사이 선형, 반경 밖 0.
	 * 반경이 0 이하면 폭발이 없는 총이라 0.
	 */
	static float ComputeSplashDamage(float DirectDamage, float RadiusCm, float EdgeRatio, float DistanceCm);

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetAttackIntervalSeconds() const { return AttackIntervalSeconds; }

	// --- 사냥 총 (M39-03 · DESIGN 8.6-c · 명세 `docs/specs/hunting-gun.md`). 총 행(`MagazineSize > 0`)을 들면
	// 공격 버튼이 그대로 「쏘기」다 — 대상 고르기·감쇠·HP 깎기·드롭은 근접 길을 빌리고, 다른 것은 둘뿐이다:
	// 탄창이 있어야 나간다 · 피해가 총알 도착 시각(거리 ÷ 속도)에 들어간다. 쏘기는 서버 호출 0 (규칙 8 은 재장전 `LoadAmmo`).

	/** 손에 든 무기 행이 총인가 (`MagazineSize > 0`). */
	UFUNCTION(BlueprintPure, Category = "Combat|Gun")
	bool IsHoldingGun() const;

	/** 탄창 속 남은 발 수. 호스트가 들고 복제한다. */
	UFUNCTION(BlueprintPure, Category = "Combat|Gun")
	int32 GetLoadedRounds() const { return LoadedRounds; }

	/**
	 * 탄창 수를 넣는다 — `0..탄창`(든 총 행)으로 자른다. 총을 안 들었으면 0. 호스트에서만 바뀐다.
	 * 재장전(M39-04)은 `LoadAmmo` 응답의 `loaded` 로만 부른다(CLAUDE.md 8). 2층 검사도 이 길로 넣는다.
	 * @return 자른 뒤 실제로 들어간 수
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Gun")
	int32 SetLoadedRounds(int32 Rounds);

	/**
	 * 한 발. 탄창이 비면 **아무 요청도 없이** 빈 찰칵만 센다. 게스트는 `Server_FireOnce` 요청만(인자 없음 — 근접과 같다).
	 * 사람 입력은 `StartAttacking` 이 총을 들었을 때 이걸 부른다.
	 * @return 호스트에서 실제로 총알이 나갔으면 true (게스트는 언제나 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Gun")
	bool FireOnce();

	/** 게스트 → 호스트 쏘기 요청. 대상·탄창은 호스트가 자기 값으로 다시 본다. */
	UFUNCTION(Server, Reliable)
	void Server_FireOnce();

	/**
	 * 재장전 (M39-04). 서버 `LoadAmmo({gun, have})` 에 묻고 **응답의 `loaded` 로만** 탄창을 채운다(CLAUDE.md 8) —
	 * 응답 전엔 탄창 수도 HUD 도 그대로다. 게스트는 응답을 `Server_SetLoadedRounds` 로 호스트에 알린다.
	 * 받아들여지면 보유 상태를 다시 받는다(가방 탄약 수는 그 스냅샷이 바꾼다).
	 * @return 서버에 물었으면 true — 총을 안 들었거나·탄창이 찼거나·이미 묻는 중이거나·내 기기가 아니면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Gun")
	bool RequestReload();

	/** 재장전 답을 기다리는 중 — 이 동안 쏘기와 두 번째 재장전은 무시한다. */
	bool IsReloadPending() const { return bReloadPending; }

	/**
	 * 재장전 몸 동작이 도는 중 (M39-06b2) — 서버가 받아들인 순간부터 행 `ReloadSeconds` 동안. 이 동안 쏘기·재장전 누름은 무시한다
	 * (DESIGN 8.6-c 「권총 재장전 1.2초 — 그동안 누름 무시」). 캐릭터는 이 동안 행 `ReloadAnim` 을 튼다.
	 */
	bool IsReloading() const { return bReloading; }

	/** 시작한 재장전 동작 수 — 몸 클립 되감기 신호(`GetSwingCount` 와 같은 쓰임). */
	int32 GetReloadMotionCount() const { return ReloadMotionCount; }

	/** 재장전 동작을 **모든 기기에서** — 그림뿐이라 Unreliable. 받아들인 내 기기는 이미 틀고 있어 다시 안 감는다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayReload();

	/** 근접 휘두르기 동작을 **남의 기기에서도** — 그림뿐이라 Unreliable. 누른 기기는 이미 틀어 다시 안 감는다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlaySwing();

	/** 게스트 → 호스트: 휘둘렀으니 남의 화면에도 동작을 틀어 달라. */
	UFUNCTION(Server, Unreliable)
	void Server_PlaySwing();

	/** 게스트 → 호스트: 재장전이 받아들여졌으니 남의 화면에도 동작을 틀어 달라. */
	UFUNCTION(Server, Unreliable)
	void Server_PlayReload();

	/** 게스트 → 호스트: 서버가 준 탄창 수. 호스트는 `0..탄창` 으로 자른다 (명세 5절 🟡). */
	UFUNCTION(Server, Reliable)
	void Server_SetLoadedRounds(int32 Rounds);

	/** 나간 발 수 / 빈 탄창이라 안 나간 누름 수 (2층 계수기). */
	int32 GetShotCount() const { return ShotCount; }
	int32 GetEmptyClickCount() const { return EmptyClickCount; }

	/** 마지막으로 나간 한 발 (호스트). */
	const FSnareGunShot& GetLastShot() const { return LastShot; }

	/**
	 * 보이는 총알이 나오는 자리 (M39-07) — 손에 든 총 메시의 총구 끝(메시 +X 끝). 겨눔이 오른쪽 앞으로 뻗은 한 손이라
	 * 몸 가운데 앞으로 잡으면 총알이 총구가 아닌 가슴에서 나온다. 손에 든 메시가 없으면 몸 가운데 앞·위(아래 두 값).
	 */
	FVector GetMuzzleLocation(const FVector& BodyCenter, const FVector& Forward) const;

	/** 손에 든 총 메시가 없을 때 총구가 몸 가운데(캡슐 중심)에서 앞·위로 떨어진 거리(cm). 연출 튜닝값. */
	static constexpr float MuzzleForwardCm = 50.f;
	static constexpr float MuzzleUpCm = 20.f;
	/** 빗나간 총알이 사거리 끝에서 아래로 땅을 찾는 깊이(cm) — 못 찾으면(물·낭떠러지) 허공 끝에서 먼지. */
	static constexpr float MissGroundProbeCm = 500.f;

	/**
	 * 총소리 이름 (M39-06c · 명세 4절 「소리」) — `SFX_<이름>_01..` 은 `tools/audio/make_sounds.py`.
	 * 쏘기·재장전은 모든 기기의 연출 신호에서, 빈 찰칵은 누른 기기에서만, 맞음은 총알 액터가 닿는 순간 튼다.
	 * 크기는 연출 튜닝값 — 총소리가 제일 크고 찰칵·재장전은 휘두르기(0.5)쯤.
	 */
	static constexpr const TCHAR* GunShotCue = TEXT("PistolShot");
	/** 폭발 총(바주카)이 나가는 소리 — 총 행 `SplashRadiusCm` > 0 이면 권총 소리 대신. */
	static constexpr const TCHAR* GunRocketLaunchCue = TEXT("RocketLaunch");
	static constexpr const TCHAR* GunEmptyClickCue = TEXT("GunEmptyClick");
	static constexpr const TCHAR* GunReloadCue = TEXT("PistolReload");
	static constexpr float GunShotVolume = 1.f;
	static constexpr float GunEmptyClickVolume = 0.6f;
	static constexpr float GunReloadVolume = 0.6f;

	/** 이 기기가 요청한 총소리 수와 마지막 이름 (2층 검사 — 맞음 소리는 총알 액터가 센다). */
	int32 GetGunSoundCount() const { return GunSoundCount; }
	FName GetLastGunSound() const { return LastGunSound; }

	/**
	 * 보이는 총알 한 발을 **모든 기기에서** (M39-05 · 명세 2절). 판정은 호스트가 끝냈고 그림뿐이라 Unreliable —
	 * 놓쳐도 피해·탄창은 같다. 쏜 사람 몸 가운데를 싣고 총구 자리는 각 기기가 `GetMuzzleLocation` 으로 잡는다.
	 * 2층 검사가 기기 쪽(`_Implementation`)을 직접 부르려고 공개한다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayShot(FVector_NetQuantize Start, FVector_NetQuantize End, ESnareGunSplash Splash, float FlightSeconds);

	/** 이 기기가 띄운 보이는 총알 수와 마지막 한 발 (2층 검사). 액터는 튀김이 끝나면 스스로 사라진다. */
	int32 GetShotVisualCount() const { return ShotVisualCount; }

	/** 쏘는 그림이 뜰 때마다 불린다 (검사 스위치 `-SnareCaptureOnShot=` 전용 — 총구 불꽃 0.1초를 캡처로 잡는다). 인자 = 지금까지 쏜 수. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnShotVisual, int32);
	static FOnShotVisual& OnShotVisual();
	ASnareGunTracerActor* GetLastTracer() const { return LastTracer.Get(); }

	/** 재장전 답이 왔을 때 (내 기기). 거절이면 Code 가 서버 사유(`not_enough_items` …) — HUD 가 알약을 띄운다. */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Gun")
	FOnGunReloadReplySignature OnReloadReply;

	/** 하이라이트 대상이 바뀔 때. 사냥 HUD(M1-08)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnMeleeTargetChangedSignature OnTargetChanged;

	/** 한 대 들어갈 때마다. 타격 연출·사운드(아트 단계)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnMeleeHitSignature OnMeleeHit;

	/**
	 * 이번 타격으로 동물이 죽었을 때 한 번. 드롭 목록이 같이 온다 (M1-04).
	 * M21-06: 막타 전 `ShareDropWindowSeconds` 안에 명중한 **다른 사람의 컴포넌트에서도** 같은 목록으로 한 번씩 울린다 —
	 * 듣는 몸이 각자 자기 기기 대기열에 쌓는다(DESIGN 3.4 「같이 잡기」).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnAnimalKilledSignature OnAnimalKilled;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 아래 넷은 DESIGN 3.1 확정 수치이고 **여기에 숫자를 적지 않는다** — `DT_PlayerCombatTable` 의
	// `Player` 행이 `ApplyCombatRow` 로 채운다 (M11-05 · 감독 지시 2026-09-23 17:25 "값을 넣는 곳이
	// 검사뿐이다"). 기본값 -1 은 "아직 안 채움" 이고, 그대로면 때리지도 못한다 (Verification.md 7).
	// 인스턴스마다 다를 값이 아니라 EditDefaultsOnly (engines/unreal.md 코드 규칙 1).
	/** 1타 데미지. DESIGN 3.1 = 20 — 표에서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float AttackDamage = -1.f;

	/** 맨손 사거리 = 자동 상호작용 하이라이트 거리(cm). DESIGN 3.1 = 150 — 표에서 온다. 캡슐 중심 간 거리. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float AttackRangeCm = -1.f;

	/** 상인·의뢰인 대화 거리(cm). DESIGN 3.1 = 250 — 표에서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float NpcTalkRangeCm = -1.f;

	/** 공격 간격(초) — 한 번 누른 뒤 다음 누름을 받기까지. DESIGN 3.1 = 0.6 — 표에서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float AttackIntervalSeconds = -1.f;

	/**
	 * 휘두르기 시작 → 데미지 판정까지 지연(초). DESIGN 3.1 = 0.2 ("공격 타격 지연" — `club_swing2` 의
	 * 타격 프레임 f6 ÷ 30fps, 곤봉이 몸 앞을 지나는 순간) — 표에서 온다.
	 * **클립을 갈면 이 값도 바꾼다** — 고치는 곳은 코드가 아니라 DESIGN 3.1 표와 `DT_PlayerCombatTable` 이다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float AttackHitDelaySeconds = -1.f;

	// 아래 둘은 DESIGN 8.6-b 근접 행의 `s` / `m` 이다. **1.0 / 1.0 = 감쇠 없음이 이 행의 핵심** —
	// 근접에 감쇠를 걸면 250cm/s 로 도주 중(3.1)인 동물을 끝거리에서 때릴 때 데미지가 줄어
	// "4타째 HP 20 -> Capturable" 수학이 깨진다. 무기 행이 값을 적으면 그쪽이 이긴다.
	/** 같이 잡기 드롭 창(초). DESIGN 3.1 = 10 — 표에서 온다. -1 이면 막타만 받는다 (M21-06). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float ShareDropWindowSeconds = -1.f;

	/** 받은 우두머리 보상 묻기 횟수 (M21-07f) — 2층 계수기. */
	int32 BossClaimRequestCount = 0;

	/** 감쇠가 시작되는 사거리 비율 `s`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float FalloffStartRatio = 1.f;

	/** 끝거리에서 남는 데미지 비율 `m`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float MinDamageRatio = 1.f;

	// 때리는 손맛 (M21-02). 전부 DESIGN 3.1 표 → `DT_PlayerCombatTable` → `ApplyCombatRow` 로만 들어온다.
	// -1 이면 손맛만 꺼진다 — 타격 판정은 그대로 나간다 (검사용 행은 이 칸을 안 채운다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float HitStopSeconds = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float HitStopTimeDilation = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float KnockbackCm = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float KnockbackSeconds = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float CameraShakeAmplitudeCm = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float CameraShakeSeconds = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float HitFlashSeconds = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float HitDebrisMin = -1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitFeel")
	float HitDebrisMax = -1.f;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 카메라 흔들림만 매 프레임 돈다 — 흔드는 0.12초 동안만 틱을 켠다. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 호스트에서만 도는 실제 타격. `AttackOnce` 와 `Server_AttackOnce` 가 둘 다 여기로 모인다. */
	bool AttackOnceOnAuthority();

	/**
	 * 고른 동물에게 피해 한 번 — 근접(`AttackOnceOnAuthority`)과 총알 도착(`LandShot`)이 같이 쓴다.
	 * HP 깎기·같이 잡기 기록·손맛·드롭·우두머리·분석 이벤트가 전부 여기 하나에 있다.
	 */
	bool ApplyHitOnAuthority(AAnimalCharacter* Target, float Damage);

	/** 호스트에서만 도는 실제 쏘기. `FireOnce` 와 `Server_FireOnce` 가 여기로 모인다. */
	bool FireOnceOnAuthority();

	/** `LoadAmmo` 답 하나를 반영한다 — 받아들여졌으면 `loaded` 로 탄창을 채우고 보유 상태를 다시 묻는다. */
	void FinishReload(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error);

	/** 총알이 과녁에 닿은 순간. 그 사이 죽었거나 잡혔으면 아무 일도 없다. */
	void LandShot(TWeakObjectPtr<AAnimalCharacter> Target, float Damage);

	/** 폭발이 터진 순간 — 중심 반경 안의 사냥 동물 모두에게 거리 따라 줄어든 피해. 그 사이 끝난 동물은 건너뛴다. */
	void LandSplash(FVector Center, float DirectDamage, float RadiusCm, float EdgeRatio);

	/** 총 사거리 안 앞쪽 부채꼴(±60°)의 가장 가까운 사냥 동물. 목장 동물·펫(`ARanchPetActor`)·사람은 애초에 동물 몸이 아니다. */
	AAnimalCharacter* FindGunTarget() const;

	/** 지연이 끝난 순간의 타격 한 번. 예약은 `SwingOnce` 가 건다. */
	void HandleDelayedHit();

	/** 소리를 내 기기에서 틀고 남의 기기에도 중계한다 (M16-08). */
	void PlayCue(FName Cue, const FVector& Location, float Volume);

	/** 손맛 칸 아홉 개가 전부 정해졌나. 하나라도 미정(-1)이면 손맛 전체를 끈다. */
	bool HasHitFeel() const;

	/** 히트스톱이 끝난 순간 — 낮춘 시간 배율을 1 로 되돌린다. */
	void EndHitStop();

	/** 흔들림을 끝내고 카메라를 제자리로. */
	void EndCameraShake();

	/** 휘두르기 한 바퀴가 끝난 순간. 여기서만 `bSwinging` 이 꺼진다. */
	void EndSwing();

	/** 한 발 나간 순간(모든 기기) — 반동 클립을 쏘기 간격 한 바퀴로 튼다 (M39-06b). */
	void StartShotRecoil();

	/** 지금 손에 든 무기의 표 한 줄 (없으면 nullptr = 맨손). 캐시하지 않는다 — 벗는 순간 옛 값이 남는다. */
	const FItemRow* FindEquippedWeaponRow() const;
	AAnimalCharacter* FindNearestAnimalInRange() const;
	void SetTarget(AAnimalCharacter* NewTarget);
	void SetHighlight(AAnimalCharacter* Animal, bool bHighlighted) const;
	void HandleTargetScan();

	// 액터를 원시 포인터로 들고 있으면 파괴돼도 눈치채지 못한다 — GC 가 아는 포인터로 든다.
	UPROPERTY(Transient)
	TObjectPtr<AAnimalCharacter> CurrentTarget;

	// 버튼을 누르고 있는지(반복은 안 한다). **동작이 도는 중인지는 `bSwinging` 이다** — 둘을 한 값으로 쓰던 때는
	// 버튼을 뗀 프레임에 몸 클립이 대기로 갈아 끼워져 휘두르기가 몇 프레임 만에 끊겼다 (결함 16:40 ①).
	bool bIsAttacking = false;

	// 휘두르기 한 바퀴(= 공격 간격)가 도는 중인지.
	bool bSwinging = false;

	// 시작한 휘두르기 수. 몸 클립 되감기 신호라 타격이 빗나가도 는다.
	int32 SwingCount = 0;

	// 탄창 속 발 수 (M39-03). 호스트만 바꾸고 게스트 화면(HUD M39-04)은 복제로 받는다.
	UPROPERTY(Replicated)
	int32 LoadedRounds = 0;

	int32 ShotCount = 0;
	int32 EmptyClickCount = 0;
	bool bReloadPending = false;
	bool bReloading = false;
	int32 ReloadMotionCount = 0;
	FTimerHandle ReloadEndTimerHandle;
	/** 재장전 동작 `ReloadSeconds` 한 바퀴를 시작한다 (이 기기). */
	void StartReloadMotion();
	FSnareGunShot LastShot;
	int32 ShotVisualCount = 0;
	TWeakObjectPtr<ASnareGunTracerActor> LastTracer;
	int32 GunSoundCount = 0;
	FName LastGunSound;
	/** 총소리 하나를 이 기기에서만 (남에게 중계하지 않는다 — 부르는 자리가 이미 모든 기기에서 돈다). */
	void PlayGunSound(const TCHAR* Cue, const FVector& Location, float Volume);

	// 맞춘 타격 수. 호스트 판정 자리(`AttackOnceOnAuthority`) 한 곳에서만 는다.
	int32 LandedHitCount = 0;

	// 공격 간격 타이머 — 도는 동안 들어온 누름은 미리 눌러 두지 않고 버린다. 끝나도 부를 함수가 없다.
	// 월드 시각이 아니라 타이머로 재는 이유: 2층 시험 세계는 타이머만 돌리고 월드 시각은 안 간다.
	FTimerHandle ActionCooldownHandle;

	// 간격 안이라 버린 누름 수 (2층 계수기).
	int32 IgnoredPressCount = 0;

	FTimerHandle TargetScanTimerHandle;

	// 타격 지연은 **버튼을 떼도 안 끈다**(짧게 눌러도 한 대). 휘두르기 끝 타이머도 마찬가지다.
	FTimerHandle HitDelayTimerHandle;
	FTimerHandle SwingEndTimerHandle;

	// 히트스톱으로 시간 배율을 낮춘 두 몸. 끝나면 이 둘만 1 로 되돌린다 — 그 사이 사라졌으면 약한 참조가 빈다.
	FTimerHandle HitStopTimerHandle;
	TWeakObjectPtr<AActor> HitStopAttacker;
	TWeakObjectPtr<AActor> HitStopTarget;
	bool bHitStopActive = false;

	// 카메라 흔들림. 시각은 **실제 시간**으로 잰다 — 때린 몸이 히트스톱으로 느려지면 틱 시간도 느려진다.
	double ShakeStartRealSeconds = 0.0;
	FVector ShakeBaseLocation = FVector::ZeroVector;
	bool bShaking = false;
};
