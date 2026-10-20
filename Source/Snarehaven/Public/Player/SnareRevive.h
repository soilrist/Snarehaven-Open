#pragma once

#include "CoreMinimal.h"
#include "World/SnareSound.h"

// 쓰러졌다 다시 일어설 때 소리 (ROADMAP M59-02 · DESIGN 2.2 「부활 소리」).
// 규칙만 담은 순수 함수 — 재생 연결은 M59-03. 부활 자리·생존 값 계산은 읽기만 하고 바꾸지 않는다.
namespace SnareRevive
{
	/** 방금까지 쓰러져 있었으면 `Revive`, 모든 기기 월드 소리, 한 번 부활에 한 번(간격 없음).
	 *  쓰러진 적 없이 불린 경우(평소)는 무음(빈 이름). */
	SNAREHAVEN_API SnareSound::FChangeCue CueFor(bool bWasDowned);

	/**
	 * 부활 자리 = 내 집/텐트 문 앞 (DESIGN 8.12 「사망」 표 「부활 자리」). 집 가운데에서 **문 쪽(집이 바라보는 +X)**으로
	 * 발자국 긴 변의 반 + 여유만큼 나간 자리 — 집 벽 안에 끼지 않게. 높이는 집과 같게 두고 부르는 쪽이 바닥을 쏴 맞춘다.
	 * @param YawDegrees 집 액터의 회전(도). @param CellCm 한 칸 크기(cm).
	 */
	SNAREHAVEN_API FVector HomeFrontLocation(const FVector& HomeLocation, float YawDegrees, const FIntPoint& Footprint, float CellCm);

	/** 문 앞 여유(cm) — 캡슐 반지름(약 34)의 3배쯤, 문턱에 걸리지 않을 만큼. */
	constexpr float HomeFrontMarginCm = 100.f;
}
