#include "Player/SnareEmoteBubble.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	struct FBubbleEntry
	{
		FText Label;
		FText Glyph;
	};

	// 말풍선 소리 (DESIGN 2.2 M54-00) — 크기 0.5 · 연타 간격 0.3초.
	constexpr float EmotePopVolume = 0.5f;
	constexpr float EmotePopMinGapSeconds = 0.3f;

	const TArray<FBubbleEntry>& Entries()
	{
		static const TArray<FBubbleEntry> Table = {
			{ LOCTEXT("BubbleWaveLabel", "안녕"), LOCTEXT("BubbleWave", "안녕!") },
			{ LOCTEXT("BubbleLaughLabel", "ㅎㅎ"), LOCTEXT("BubbleLaugh", "ㅎㅎ") },
			{ LOCTEXT("BubbleHeartLabel", "♥"), LOCTEXT("BubbleHeart", "♥") },
			{ LOCTEXT("BubbleSurpriseLabel", "!"), LOCTEXT("BubbleSurprise", "!") },
			{ LOCTEXT("BubbleQuestionLabel", "?"), LOCTEXT("BubbleQuestion", "?") },
			{ LOCTEXT("BubbleLikeLabel", "좋아"), LOCTEXT("BubbleLike", "좋아!") },
		};
		return Table;
	}
}

int32 SnareEmoteBubble::Num()
{
	return Entries().Num();
}

FText SnareEmoteBubble::GetLabel(int32 Index)
{
	return Entries().IsValidIndex(Index) ? Entries()[Index].Label : FText::GetEmpty();
}

FText SnareEmoteBubble::GetGlyph(int32 Index)
{
	return Entries().IsValidIndex(Index) ? Entries()[Index].Glyph : FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

SnareSound::FChangeCue SnareEmoteBubble::CueFor(int32 Index)
{
	SnareSound::FChangeCue Out;
	if (Index < 0 || Index >= Num())
	{
		return Out;
	}
	Out.Cue = TEXT("EmotePop");
	Out.Volume = EmotePopVolume;
	Out.MinGapSeconds = EmotePopMinGapSeconds;
	Out.bWorld = true;
	return Out;
}
