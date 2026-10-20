#include "Player/SnareWarmth.h"

namespace
{
	// 불 옆 온기 소리 (DESIGN 2.2 M58-00) — 크기 0.5 · 되풀이 간격 4.0초.
	constexpr float WarmthVolume = 0.5f;
	constexpr float WarmthMinGapSeconds = 4.f;
	// 회복 틱이 1초 간격이라 프레임 흔들림으로 3.99초가 쌓이면 다음 틱(5초)까지 밀린다 — 비교에 작은 여유.
	constexpr float WarmthGapSlackSeconds = 0.1f;
}

SnareSound::FChangeCue SnareWarmth::CueFor(float HealPerSecond, bool bHealthFull, float SecondsSinceLast)
{
	SnareSound::FChangeCue Out;
	if (HealPerSecond <= NoHeal || bHealthFull || SecondsSinceLast < WarmthMinGapSeconds - WarmthGapSlackSeconds)
	{
		return Out;
	}
	Out.Cue = TEXT("Warmth");
	Out.Volume = WarmthVolume;
	Out.MinGapSeconds = WarmthMinGapSeconds;
	Out.bWorld = true;
	return Out;
}
