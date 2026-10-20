#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SnareGunTracerActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FHitResult;

/** 총알이 닿은 자리의 튀김 종류 (명세 `hunting-gun.md` 2절). 회선에 실리므로 UENUM 이다. */
UENUM()
enum class ESnareGunSplash : uint8
{
	Dust,    // 땅·빗나감 — 흙먼지
	Animal,  // 사냥 동물 — 하얀 별
	Wood,    // 나무 — 나무 부스러기
	Stone    // 바위·광맥 — 돌 부스러기
};

/**
 * 보이는 총알 한 발 (ROADMAP M39-05 · 명세 `hunting-gun.md` 2절). 각 기기가 `UMeleeAttackComponent::Multicast_PlayShot`
 * 에서 제 손으로 띄우는 **복제 안 하는 연출 액터**다 — 판정·피해는 호스트가 이미 끝냈고 이건 그림뿐이다.
 * 총구 불꽃(1~2장) → 빛나는 점 + 꼬리가 출발에서 도착까지 **실제로 이동**(순간 선 금지) → 도착 순간 총알이 사라지고
 * 맞은 것에 따라 튀김. 날아가는 시간은 호스트가 보낸 값(= 거리 ÷ 행 속도)이라 체력 막대가 깎이는 순간과 맞는다.
 * 포획 반짝(`ASnareCaptureBurstActor`)처럼 파티클 에셋 없이 엔진 기본 도형으로 그린다 — 총알·꼬리·불꽃은
 * 언릿 발광 `M_HitFlash` 동적 인스턴스라 모바일 렌더러에서 그림자·빛 없이도 밝게 보인다.
 */
UCLASS(NotBlueprintable)
class SNAREHAVEN_API ASnareGunTracerActor : public AActor
{
	GENERATED_BODY()

public:
	ASnareGunTracerActor();

	/**
	 * 꼬리 길이 = 총알 속도 × 이 초. 권총 4000cm/s → 160cm · 소총 6000 → 240cm (DESIGN 8.6-c 「총알 꼬리」 —
	 * 게임 카메라 1280×720 에서 ≥60px, 30fps 한 장 이동(133cm)보다 길어 장 사이가 이어진다). 옛 0.015(60cm)는
	 * 25px 실선이라 풀밭 결과 안 갈렸다(감독 #583, M39-07b). 속도가 행마다 달라도 숫자 하나로 맞는다.
	 */
	static constexpr float TailSeconds = 0.04f;
	/** 총구 불꽃·펠릿 줄이 떠 있는 시간(초) — 30fps 3장 (감독 보정 10-15 「0.1초 내외」, 0.05초는 캡처·눈에 안 잡혔다). */
	static constexpr float MuzzleFlashSeconds = 0.1f;
	/**
	 * 튀김 조각 수(털 조각 포함)와 수명(초). 수명은 동물 피격 파편(0.35초)과 같게 — 두 그림이 같이 사라진다.
	 * 12 → 20 (M39-07c 감독 「퍽 대신 조각을 굵고 많게」). 동물이면 그중 FurPieceCount 개가 털빛.
	 */
	static constexpr int32 SplashCount = 20;
	static constexpr int32 FurPieceCount = 6;
	/** 닿는 순간 퍽(반투명 번쩍)이 떠 있는 시간(초) — 동물 흰 깜빡임(0.08초)과 같은 박자, 끝에 불투명도 0. */
	static constexpr float ImpactPuffSeconds = 0.08f;
	static constexpr float SplashLifeSeconds = 0.35f;
	/** 닿는 순간 소리 (M39-06c) — 튀김 종류와 상관없이 하나, 종류는 튀김 그림이 말한다. */
	static constexpr const TCHAR* ImpactCue = TEXT("BulletImpact");
	static constexpr float ImpactVolume = 0.7f;
	/** 폭발(바주카) 소리와 그림 — 퍽이 반경 전체까지 부풀며 0.35초 동안 사라진다. 반경은 총 행 `SplashRadiusCm` (DESIGN 8.6-c). */
	static constexpr const TCHAR* BlastCue = TEXT("Explosion");
	static constexpr float BlastVolume = 1.0f;
	static constexpr float BlastPuffSeconds = 0.35f;
	static constexpr float BlastPuffStartRatio = 0.4f;
	static constexpr float BlastPieceSpeedScale = 3.f;
	/** 로켓 연기 꼬리 — 총알 꼬리(0.04초)보다 5배 길고 3배 굵은 회색 줄. 속도 2500cm/s → 500cm. */
	static constexpr float SmokeTailSeconds = 0.3f;
	static constexpr float SmokeTailThicknessMul = 4.f;
	/** 로켓 연기 꼬리 = 둥근 연기 덩이 줄(감독 10-16 2차 「흰 판 말고 둥근 덩이 8~12개」). 로켓이 지나간 자리에 하나씩 켜지고,
	 *  포구에서 멀수록 커지고 옅다. 닿은 뒤 이 시간 동안 1.5배로 퍼지며 사라진다. */
	static constexpr float SmokeTrailFadeSeconds = 0.8f;
	static constexpr float SmokeTrailFadeWidenMul = 1.5f;
	static constexpr int32 SmokePuffCount = 10;
	/** 쏘는 장에 바로 켜는 포구 곁 덩이 수(감독 10-16 3차 「포구와 첫 연기 사이 빈틈」). */
	static constexpr int32 SmokePuffInstantCount = 3;
	/** 덩이 사이 간격 상한 — 먼 과녁이어도 포구 곁 600cm 안에 촘촘히 놓여 0.15초(≈350cm)만에 덩이 5~6개가 보인다. */
	static constexpr float SmokePuffSpacingCm = 60.f;
	static constexpr float SmokePuffNearCm = 60.f;
	static constexpr float SmokePuffFarCm = 120.f;
	static constexpr float SmokePuffNearOpacity = 0.95f;
	static constexpr float SmokePuffFarOpacity = 0.45f;
	/** 폭발 뒤 검은 연기 덩어리(#1211 보정 「노란 안개 말고 또렷한 불덩이 + 연기 + 파편」) — 불덩이 지름의 0.3 → 0.9 배로 0.6초 동안 커지며 사라진다. */
	static constexpr float BlastSmokeSeconds = 0.6f;
	static constexpr float BlastSmokeStartRatio = 0.3f;
	static constexpr float BlastSmokeEndRatio = 0.9f;
	static constexpr float BlastPieceSizeMul = 2.f;
	/** 로켓(폭발 총)의 약한 포물선 — 꼭대기 높이 = 날아가는 거리 × 이 비율(3000cm → 120cm). 닿는 자리·시각은 곧은 선과 같다(DESIGN 8.6-c). */
	static constexpr float RocketArcApexRatio = 0.04f;

	/**
	 * 한 발을 띄운다.
	 * @param Muzzle 총구 자리 — 총알이 여기서 출발하고 불꽃이 여기 핀다
	 * @param End 닿는 자리 — 튀김이 여기 인다
	 * @param InFlightSeconds 날아가는 시간. 0 이하면 바로 튀김
	 * @param InBlastRadiusCm 0 보다 크면 폭발 — 닿는 자리에 이 반경의 주황 불덩이와 폭발 소리
	 */
	void Start(const FVector& Muzzle, const FVector& End, float InFlightSeconds, ESnareGunSplash InSplash,
		int32 InPelletCount = 1, float InSpreadHalfDegrees = 0.f, float InBlastRadiusCm = 0.f);

	float GetBlastRadiusCm() const { return BlastRadiusCm; }
	/** 폭발 불덩이 색 — 퍽에 쓴다. */
	static FLinearColor GetBlastColor();
	static FLinearColor GetBlastSmokeColor();
	static FLinearColor GetBlastDebrisColor();
	bool IsBlastSmokeVisible() const { return IsValid(BlastSmoke) && BlastSmoke->IsVisible(); }
	bool IsTailVisible() const;
	int32 GetVisibleSmokePuffCount() const;
	/** 연기 덩이 Index 의 가운데 — 0 번은 포구 끝. 덩이가 없으면 의미 없음. */
	FVector GetSmokePuffLocation(int32 Index) const;
	/** 출발에서 Traveled cm 간 자리 — 일반 총알은 곧은 선, 로켓은 약한 포물선(양 끝 높이 0). */
	FVector GetFlightPoint(float Traveled) const;

	/** 산탄(펠릿 2 이상)이 총구에서 갈래로 퍼지는 줄 수 — 시험용. */
	int32 GetPelletLineCount() const { return PelletLines.Num(); }

	virtual void Tick(float DeltaSeconds) override;

	/** 막힌 자리 하나를 튀김 종류로 — 채집물은 필요한 도구(곡괭이 = 돌, 그 밖 = 나무), 그 밖은 위를 보면 땅 · 옆면이면 돌. */
	static ESnareGunSplash ClassifyWorldHit(const FHitResult& Hit);

	/** 튀김 조각 색 (동물 = 하양 · 땅 = 흙 · 나무 = 밝은 갈색 · 돌 = 회색). */
	static FLinearColor GetSplashColor(ESnareGunSplash InSplash);

	/** 2층 검사용. */
	float GetFlightSeconds() const { return FlightSeconds; }
	FVector GetStartLocation() const { return StartLocation; }
	ESnareGunSplash GetSplash() const { return Splash; }
	bool IsBulletVisible() const;
	FVector GetBulletLocation() const;
	float GetTailLengthCm() const { return TailLengthCm; }
	float GetTailWidthCm() const { return TailThicknessScaleNow * 100.f; }
	bool IsMuzzleFlashVisible() const;
	int32 GetSplashInstanceCount() const;
	bool IsImpactPuffVisible() const;
	float GetImpactPuffDiameterCm() const;
	/** 퍽 가운데 불투명도(가장자리는 재질이 0 으로 줄인다). 꺼졌으면 0. */
	float GetImpactPuffOpacity() const;
	static float GetSplashPieceSizeCm();
	/** 털빛 조각 수 (동물이 아니면 0). */
	int32 GetFurPieceCount() const;
	static float GetTailThicknessCm();
	bool HasLanded() const { return bLanded; }
	int32 GetImpactSoundCount() const { return ImpactSoundCount; }

private:
	UStaticMeshComponent* MakeGlowPart(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color,
		UMaterialInterface* Material = nullptr);
	UInstancedStaticMeshComponent* MakeSplashPart(const TCHAR* Name, const FLinearColor& Color, bool bGlow);
	void UpdateSplashPart(UInstancedStaticMeshComponent* Part, int32 FirstVelocity, float DeltaSeconds, float Gravity, float Shrink);
	void Land();
	/** 연기 덩이 — 포구에서 ReachedCm 까지 지나간 덩이만 켜고, FadeAlpha(0~1) 만큼 퍼지며 옅게. */
	void UpdateSmokePuffs(float ReachedCm, float FadeAlpha);
	float GetPuffStartScale() const;
	float GetPuffEndScale() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Bullet;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Tail;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MuzzleFlash;

	/** 로켓 연기 덩이 — 로켓일 때만 만든다(그때 Tail 은 안 쓴다). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SmokePuffs;

	/** 산탄 펠릿 갈래 줄 — 불꽃과 같이 켜졌다 꺼진다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PelletLines;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ImpactPuff;

	/** 폭발 검은 연기 덩어리 — 폭발일 때만 만든다. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BlastSmoke;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SplashPieces;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FurPieces;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PuffMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SplashMaterial;

	TArray<FVector> SplashVelocities;
	FVector SplashPieceScale = FVector::OneVector;
	FVector StartLocation = FVector::ZeroVector;
	FVector EndLocation = FVector::ZeroVector;
	FVector SplashLocation = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float FlightSeconds = 0.f;
	float TailLengthCm = 0.f;
	float ElapsedSeconds = 0.f;
	float LandedSeconds = 0.f;
	float ImpactPuffOpacity = 0.f;
	float BlastRadiusCm = 0.f;
	float TailThicknessScaleNow = 0.f;
	float SmokePuffSpacingNowCm = 0.f;
	ESnareGunSplash Splash = ESnareGunSplash::Dust;
	bool bStarted = false;
	bool bLanded = false;
	int32 ImpactSoundCount = 0;
};
