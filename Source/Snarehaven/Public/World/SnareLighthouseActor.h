#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SnareLighthouseActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class USpotLightComponent;
class UStaticMeshComponent;

// 메인 스토리 ⑤ 등대 점등 (`docs/specs/story-lighthouse-light.md`). 호스트·게스트 각자 자기 화면에 세우는 장식이다 —
// 재화·보상이 없어 권위 2단과 무관하다. 25단계를 끝낸 계정(`SnareStory::GetKnownDone`)만 밤에 불이 켜진다.
// 켜는 규칙은 `SnareStory::LighthouseBrightness`·`LighthouseBeamYaw` 순수 함수가 정하고 여기는 그 값을 빛에 옮길 뿐이다.
UCLASS()
class SNAREHAVEN_API ASnareLighthouseActor : public AActor
{
	GENERATED_BODY()

public:
	ASnareLighthouseActor();

	virtual void Tick(float DeltaSeconds) override;

	/** 게임모드가 사냥 맵에서 한 번 부른다 — 설원 칸 가운데 바닥에 탑을 세운다. 이미 있으면 세우지 않는다. */
	static ASnareLighthouseActor* SpawnAtSpot(UWorld* World);

	/** 지금 밝기(0~1)와 빛줄기 방향을 빛에 옮긴다. 2층 검사는 값을 직접 넣어 부른다. */
	void ApplyLight(float Brightness, float BeamYawDegrees);

	/** 5장 끝 점화 연출을 지금(`NowSeconds` = 월드 시각) 시작한다 — 연출 동안은 낮이어도 불이 붙고 빛줄기가 한 바퀴 돈다. */
	void StartIgnition(double NowSeconds);

	/** 점화 연출 중인가 (`NowSeconds` = 월드 시각). */
	bool IsIgniting(double NowSeconds) const;

	/** 밤 규칙과 점화 연출을 합쳐 빛에 옮긴다 — 밝기는 둘 중 큰 값, 점화 중엔 빛줄기에 한 바퀴를 더한다. 틱이 부르고, 2층 검사는 값을 직접 넣어 부른다. */
	void UpdateLight(bool bDone, float Night, double BeamSeconds, double NowSeconds);

	float GetBeamIntensity() const;
	float GetBeamYaw() const;
	/** 랜턴 방 창(`Window` 슬롯)이 지금 켜진 정도 0~1 — 어두운 원래 재질이면 0. */
	float GetWindowGlow() const;
	float GetLanternIntensity() const;
	/** 낮에도 보이는 빛줄기 메시(가산 원뿔) — 불이 꺼지면 숨는다. 2층 검사가 자리·길이·보임을 잰다. */
	const UStaticMeshComponent* GetBeamMesh() const;
	/** 빛줄기 메시 재질에 넣은 세기 (`BeamStrength`) — 숨었으면 0. */
	float GetBeamMeshStrength() const;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Tower;

	UPROPERTY()
	TObjectPtr<USpotLightComponent> Beam;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Lantern;

	/** 스포트 빛(`Beam`)에 붙어 같이 도는 원뿔 — 꼭짓점이 랜턴, 바닥이 빛줄기 끝. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BeamMesh;

private:
	/** 창 슬롯을 밝기에 맞춘다 — 0 이면 원래 어두운 재질, 0 보다 크면 언릿 발광 재질로 어두운 색 → 따뜻한 빛. */
	void UpdateWindow(float Brightness);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WindowGlowMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WindowDarkMaterial;

	/** 빛줄기 메시를 밝기에 맞춘다 — 0 이면 숨김(그리기 호출 0), 0 보다 크면 보이고 세기 = 밝기 × 최대. */
	void UpdateBeamMesh(float Brightness);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeamMeshMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BeamMeshGlowMaterial;

	float BeamMeshStrength = 0.f;

	int32 WindowSlot = INDEX_NONE;
	float WindowGlow = 0.f;

	/** 마지막으로 로그에 남긴 밝기 — 바뀔 때만 한 줄 (캡처 판정용). */
	float LoggedBrightness = -1.f;

	/** 점화 연출을 시작한 월드 시각 — 시작 안 했으면 음수. */
	double IgnitionStartSeconds = -1.0;
};
