#include "Backend/SnareStory.h"

#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "SnareStory"

namespace SnareStory
{
bool ParseView(const TSharedPtr<FJsonObject>& Response, FStoryView& Out)
{
	double Step = 0.0;
	if (!Response.IsValid() || Response->HasField(TEXT("error")) || !Response->TryGetNumberField(TEXT("step"), Step))
	{
		return false;
	}
	Out = FStoryView();
	Out.Step = FMath::Clamp(static_cast<int32>(Step), 0, TotalSteps);
	Response->TryGetBoolField(TEXT("done"), Out.bDone);
	double Chapter = 1.0;
	if (Response->TryGetNumberField(TEXT("chapter"), Chapter))
	{
		Out.Chapter = FMath::Clamp(static_cast<int32>(Chapter), 1, TotalSteps / StepsPerChapter);
	}
	const TArray<TSharedPtr<FJsonValue>>* Titles = nullptr;
	if (Response->TryGetArrayField(TEXT("titles"), Titles))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Titles)
		{
			FString Title;
			if (Value.IsValid() && Value->TryGetString(Title))
			{
				Out.Titles.Add(Title);
			}
		}
	}
	const TSharedPtr<FJsonObject>* Current = nullptr;
	if (!Out.bDone && Response->TryGetObjectField(TEXT("current"), Current) && Current->IsValid())
	{
		double Value = 0.0;
		if ((*Current)->TryGetNumberField(TEXT("step"), Value))
		{
			Out.CurrentStep = static_cast<int32>(Value);
		}
		(*Current)->TryGetStringField(TEXT("kind"), Out.Kind);
		if ((*Current)->TryGetNumberField(TEXT("have"), Value))
		{
			Out.Have = static_cast<int32>(Value);
		}
		if ((*Current)->TryGetNumberField(TEXT("need"), Value))
		{
			Out.Need = static_cast<int32>(Value);
		}
		const TArray<TSharedPtr<FJsonValue>>* Needs = nullptr;
		if ((*Current)->TryGetArrayField(TEXT("needs"), Needs))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Needs)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				FString ItemId;
				double Count = 0.0;
				if (Entry.IsValid() && Entry->TryGetObject(Obj) && (*Obj)->TryGetStringField(TEXT("itemId"), ItemId)
					&& (*Obj)->TryGetNumberField(TEXT("count"), Count))
				{
					Out.Needs.Emplace(FName(*ItemId), static_cast<int32>(Count));
				}
			}
		}
	}
	return true;
}

EGiverMark GiverMark(const FStoryView& View)
{
	if (View.bDone || View.Step >= TotalSteps)
	{
		return EGiverMark::None;
	}
	if (View.Kind == TEXT("report"))
	{
		return EGiverMark::Question;
	}
	if (View.Kind == TEXT("give"))
	{
		return View.Have >= View.Need ? EGiverMark::Question : EGiverMark::Bang;
	}
	return EGiverMark::None;
}

EStoryNpc TargetNpc(int32 Step)
{
	if (Step < 1 || Step > TotalSteps)
	{
		return EStoryNpc::None;
	}
	if (Step >= 11 && Step <= 14)
	{
		return EStoryNpc::Mollae;	// 3장 앞 4단계는 뱃사공 모래 (STORY.md 4절)
	}
	if (Step == 18 || Step == 20)
	{
		return EStoryNpc::Hook;	// 갈고리
	}
	return EStoryNpc::Solbangul;
}

namespace
{
	EStoryNpc GCurrentTargetNpc = EStoryNpc::None;
}

void SetCurrentTargetNpc(EStoryNpc Npc)
{
	GCurrentTargetNpc = Npc;
}

EStoryNpc GetCurrentTargetNpc()
{
	return GCurrentTargetNpc;
}

bool ShouldAutoAdvance(const FStoryView& View)
{
	if (View.bDone || View.CurrentStep <= 0 || View.Need <= 0)
	{
		return false;
	}
	return (View.Kind == TEXT("count") || View.Kind == TEXT("ranch")) && View.Have >= View.Need;
}

bool ShouldReport(const FStoryView& View)
{
	return !View.bDone && View.CurrentStep > 0 && View.Kind == TEXT("report");
}

bool ShouldGive(const FStoryView& View)
{
	return !View.bDone && View.CurrentStep > 0 && View.Kind == TEXT("give");
}

FText GiverMarkText(EGiverMark Mark)
{
	switch (Mark)
	{
	case EGiverMark::Bang: return LOCTEXT("MarkBang", "!");
	case EGiverMark::Question: return LOCTEXT("MarkQuestion", "?");
	default: return FText::GetEmpty();
	}
}

FText ChapterTitle(int32 Chapter)
{
	switch (Chapter)
	{
	case 1: return LOCTEXT("Ch1", "떠밀려 온 아침");
	case 2: return LOCTEXT("Ch2", "울타리 안의 온기");
	case 3: return LOCTEXT("Ch3", "안개 너머 물길");
	case 4: return LOCTEXT("Ch4", "회색 작살단");
	case 5: return LOCTEXT("Ch5", "등대를 밝히다");
	default: return FText::GetEmpty();
	}
}

int32 CompletedChapter(const FStoryView& View)
{
	return FMath::Clamp(View.Step / StepsPerChapter, 0, TotalSteps / StepsPerChapter);
}

int32 LighthouseRingsLit(const FStoryView& View)
{
	return View.bDone ? TotalSteps / StepsPerChapter : CompletedChapter(View);
}

int32 ChapterCoins(int32 Chapter)
{
	static const int32 Coins[] = { 20, 30, 40, 50, 60 };
	return Chapter >= 1 && Chapter <= UE_ARRAY_COUNT(Coins) ? Coins[Chapter - 1] : 0;
}

FText ChapterRewardItemName(int32 Chapter)
{
	switch (Chapter)
	{
	case 1: return LOCTEXT("Rw1", "솔방울 여행자 모자");
	case 2: return LOCTEXT("Rw2", "목장 나무 팻말");
	case 3: return LOCTEXT("Rw3", "뱃사공 목도리");
	case 4: return LOCTEXT("Rw4", "부서진 작살 장식");
	case 5: return LOCTEXT("Rw5", "등대 모양 등불");
	default: return FText::GetEmpty();
	}
}

FText StepGoal(int32 Step)
{
	switch (Step)
	{
	case 1: return LOCTEXT("S1", "해변에서 거점의 불빛까지 걷기");
	case 2: return LOCTEXT("S2", "손도끼 만들기");
	case 3: return LOCTEXT("S3", "솔방울에게 {0} 건네기");
	case 4: return LOCTEXT("S4", "동물 1마리 포획하기");
	case 5: return LOCTEXT("S5", "잡은 동물을 목장에 두기");
	case 6: return LOCTEXT("S6", "목장에 동물 3마리 두기");
	case 7: return LOCTEXT("S7", "목장 수확 3번 하기");
	case 8: return LOCTEXT("S8", "솔방울에게 {0} 건네기");
	case 9: return LOCTEXT("S9", "서로 다른 동물 3종 포획하기");
	case 10: return LOCTEXT("S10", "솔방울에게 말 걸기");
	case 11: return LOCTEXT("S11", "첫 항구의 뱃사공 모래에게 말 걸기");
	case 12: return LOCTEXT("S12", "솔방울에게 {0} 건네기");
	case 13: return LOCTEXT("S13", "배 만들기");
	case 14: return LOCTEXT("S14", "배로 이웃 섬에 닿기");
	case 15: return LOCTEXT("S15", "해람의 일지 1을 솔방울에게 건네기");
	case 16: return LOCTEXT("S16", "북쪽 물굽이의 작살단 캠프 찾기");
	case 17: return LOCTEXT("S17", "솔방울에게 {0} 건네기");
	case 18: return LOCTEXT("S18", "캠프의 덫과 우리 3개 부수기");
	case 19: return LOCTEXT("S19", "풀려난 동물 3마리 다시 포획하기");
	case 20: return LOCTEXT("S20", "갈고리에게 말 걸기");
	case 21: return LOCTEXT("S21", "솔방울에게 {0} 건네기");
	case 22: return LOCTEXT("S22", "안개 낀 등대 언덕에 오르기");
	case 23: return LOCTEXT("S23", "안개뿔 포획하기");
	case 24: return LOCTEXT("S24", "안개뿔로 목장 수확 1번 하기");
	case 25: return LOCTEXT("S25", "등대 꼭대기에서 불 켜기");
	default: return FText::GetEmpty();
	}
}

TArray<FDialogueLine> StepDialogue(int32 Step)
{
	TArray<FDialogueLine> Out;
	auto Add = [&Out](const FText& Speaker, const FText& Text) { Out.Add({Speaker, Text}); };
	switch (Step)
	{
	case 1:
		Add(LOCTEXT("D1S0", "솔방울"), LOCTEXT("D1T0", "살아 있었군. 이 안개에 배가 들어온 건 십 년 만이야."));
		Add(LOCTEXT("D1S1", "솔방울"), LOCTEXT("D1T1", "저 불빛 쪽으로 오게. 몸부터 말려야지."));
		break;
	case 2:
		Add(LOCTEXT("D2S0", "솔방울"), LOCTEXT("D2T0", "맨손으로는 겨울을 못 나. 도끼부터."));
		break;
	case 3:
		Add(LOCTEXT("D3S0", "솔방울"), LOCTEXT("D3T0", "불을 피우면 동물들도 덜 무서워하지."));
		Add(LOCTEXT("D3S1", "솔방울"), LOCTEXT("D3T1", "나무와 산딸기를 모아 오게."));
		break;
	case 4:
		Add(LOCTEXT("D4S0", "솔방울"), LOCTEXT("D4T0", "죽이지 말고 잡게. 이 섬에선 그게 법이었어."));
		Add(LOCTEXT("D4S1", "솔방울"), LOCTEXT("D4T1", "올가미를 던져 체력을 깎아 보게."));
		break;
	case 5:
		Add(LOCTEXT("D5S0", "솔방울"), LOCTEXT("D5T0", "잡은 녀석을 목장에 두게. 돌봐 주면 보답이 있을 거야."));
		break;
	case 6:
		Add(LOCTEXT("D6S0", "솔방울"), LOCTEXT("D6T0", "온기를 모으려면 동물이 편해야 해. 목장부터 제대로 꾸리게."));
		Add(LOCTEXT("D6S1", "솔방울"), LOCTEXT("D6T1", "우선 세 마리를 두어 보게."));
		break;
	case 7:
		Add(LOCTEXT("D7S0", "솔방울"), LOCTEXT("D7T0", "배부르고 안 맞으면, 동물은 뭔가를 내준다네."));
		break;
	case 8:
		Add(LOCTEXT("D8S0", "솔방울"), LOCTEXT("D8T0", "먹이도 직접 길러. 밭에서 거둔 걸 가져오게."));
		break;
	case 9:
		Add(LOCTEXT("D9S0", "솔방울"), LOCTEXT("D9T0", "한 종류만으론 목장이 허전하지. 서로 다른 동물을 잡아 오게."));
		break;
	case 10:
		Add(LOCTEXT("D10S0", "솔방울"), LOCTEXT("D10T0", "오빠는 동물마다 이름을 붙였었지. 너도 붙여 보게."));
		break;
	case 11:
		Add(LOCTEXT("D11S0", "모래"), LOCTEXT("D11T0", "등대 없인 큰 배가 못 와. 대신 작은 배로 물길을 다시 찾아야지."));
		Add(LOCTEXT("D11S1", "모래"), LOCTEXT("D11T1", "항구로 오게. 얘기 좀 하세."));
		break;
	case 12:
		Add(LOCTEXT("D12S0", "모래"), LOCTEXT("D12T0", "낚싯줄 쥘 줄은 알아야 배를 타지."));
		Add(LOCTEXT("D12S1", "모래"), LOCTEXT("D12T1", "붕어와 잉어를 낚아 오게."));
		break;
	case 13:
		Add(LOCTEXT("D13S0", "모래"), LOCTEXT("D13T0", "물길은 배가 있어야 열려. 한 척 만들어 보게."));
		break;
	case 14:
		Add(LOCTEXT("D14S0", "모래"), LOCTEXT("D14T0", "이웃 섬 해변에 뭔가 떠밀려 온다더군. 가 보게."));
		break;
	case 15:
		Add(LOCTEXT("D15S0", "솔방울"), LOCTEXT("D15T0", "(일지를 오래 바라본다) …오빠 글씨다."));
		Add(LOCTEXT("D15S1", "솔방울"), LOCTEXT("D15T1", "폭풍 오는 밤. 작살 든 배들이 북쪽 물굽이로 들어왔다. 흰 사슴이 다쳤다."));
		break;
	case 16:
		Add(LOCTEXT("D16S0", "솔방울"), LOCTEXT("D16T0", "북쪽 물굽이라… 작살단 캠프가 거기 있겠구나. 조심해서 가 보게."));
		break;
	case 17:
		Add(LOCTEXT("D17S0", "솔방울"), LOCTEXT("D17T0", "작살단과 맞서려면 장비부터. 사냥터 재료로 만들어 오게."));
		break;
	case 18:
		Add(LOCTEXT("D18S0", "갈고리"), LOCTEXT("D18T0", "…우리를 다 부숴 놨군, 꼬마. 이 섬 짐승은 다 값이 매겨져 있다고."));
		Add(LOCTEXT("D18S1", "갈고리"), LOCTEXT("D18T1", "풀려난 놈들이 사납게 날뛸 텐데, 어디 거두어 보시지."));
		break;
	case 19:
		Add(LOCTEXT("D19S0", "솔방울"), LOCTEXT("D19T0", "겁먹은 것뿐이야. 돌봐 주면 돌아와."));
		break;
	case 20:
		Add(LOCTEXT("D20S0", "갈고리"), LOCTEXT("D20T0", "…안개가 걷히면 우리도 장사 끝이다."));
		Add(LOCTEXT("D20S1", "갈고리"), LOCTEXT("D20T1", "등대가 다시 켜지면, 섬을 뜨지."));
		break;
	case 21:
		Add(LOCTEXT("D21S0", "솔방울"), LOCTEXT("D21T0", "등대 언덕은 춥다네. 단단히 채비하게."));
		Add(LOCTEXT("D21S1", "솔방울"), LOCTEXT("D21T1", "오빠는 도망친 게 아니었어. 사슴을 지키다… 그래, 이제 알겠다."));
		break;
	case 22:
		Add(LOCTEXT("D22S0", "해람의 일지"), LOCTEXT("D22T0", "사슴은 사람을 무서워하지 않는다. 다만 작살을 무서워할 뿐."));
		break;
	case 23:
		Add(LOCTEXT("D23S0", "솔방울"), LOCTEXT("D23T0", "저 흰 사슴이 안개뿔이야. 겁주지 말고 천천히 다가가게."));
		break;
	case 24:
		Add(LOCTEXT("D24S0", "솔방울"), LOCTEXT("D24T0", "안개뿔 곁에서 수확해 보게. 이게 진짜 온기야."));
		Add(LOCTEXT("D24S1", "솔방울"), LOCTEXT("D24T1", "(일지) 이 글을 읽는 사람에게. 등대는 기름으로 켜지지 않는다. 고마워."));
		break;
	case 25:
		Add(LOCTEXT("D25S0", "솔방울"), LOCTEXT("D25T0", "어서 오게, 올가미지기."));
		break;
	default:
		break;
	}
	return Out;
}

TArray<FDialogueLine> ArrivalDialogue(int32 Step)
{
	TArray<FDialogueLine> Out;
	switch (Step)
	{
	case 14:
		Out.Add({LOCTEXT("A14S0", "나"), LOCTEXT("A14T0", "(이웃 섬 해변이다. 모래 위에 낡은 일지 한 권이 떠밀려 와 있다)")});
		break;
	case 16:
		Out.Add({LOCTEXT("A16S0", "나"), LOCTEXT("A16T0", "(늪 너머로 천막 지붕과 우리가 보인다) …여기가 작살단 캠프.")});
		Out.Add({LOCTEXT("A16S1", "나"), LOCTEXT("A16T1", "(우리 안에서 짐승 울음이 새어 나온다)")});
		break;
	default:
		break;
	}
	return Out;
}

FText FormatLine(const FStoryView& View, TFunctionRef<FText(FName)> DisplayName)
{
	if (View.bDone || View.Step >= TotalSteps)
	{
		return LOCTEXT("Done", "올가미지기가 되었다 — 등대가 켜졌다");
	}
	const int32 Step = View.CurrentStep > 0 ? View.CurrentStep : View.Step + 1;
	FText Goal = StepGoal(Step);
	if (View.Kind == TEXT("give"))
	{
		TArray<FText> Parts;
		for (const TPair<FName, int32>& Need : View.Needs)
		{
			Parts.Add(FText::Format(LOCTEXT("NeedPart", "{0} {1}개"), DisplayName(Need.Key), FText::AsNumber(Need.Value)));
		}
		Goal = FText::Format(Goal, FText::Join(FText::FromString(TEXT(", ")), Parts));
	}
	else if (View.Need > 1)
	{
		Goal = FText::Format(LOCTEXT("WithCount", "{0} ({1}/{2})"), Goal, FText::AsNumber(View.Have), FText::AsNumber(View.Need));
	}
	const int32 InChapter = (Step - 1) % StepsPerChapter + 1;
	return FText::Format(LOCTEXT("Line", "지금 할 일: {0} · {1}장 {2}/{3}"), Goal, FText::AsNumber((Step - 1) / StepsPerChapter + 1),
		FText::AsNumber(InChapter), FText::AsNumber(StepsPerChapter));
}

bool ShouldStandFogbuck(int32 CurrentStep)
{
	return CurrentStep == 22 || CurrentStep == 23;
}

FArrival ArrivalFor(int32 Step)
{
	switch (Step)
	{
	case 1: return {EArrival::NearGiver, 300.f, NAME_None};	// 해변에서 거점의 불빛까지
	case 10: return {EArrival::NearGiver, 400.f, NAME_None};	// 솔방울 곁
	case 11: return {EArrival::NearMollae, 400.f, NAME_None};	// 3장 1단계 뱃사공 모래(낚시 상인) 곁
	case 15: return {EArrival::NearGiver, 400.f, NAME_None};	// 3장 5단계 일지를 솔방울(의뢰인)에게 — 10단계와 같은 곁 반경
	case 14: return {EArrival::InRegion, 0.f, FName(TEXT("zone.forest"))};	// 🟡 이웃 섬 = 숲(가을섬) 존 — 존 표 `ZoneID`(행 이름 Zone_Forest 아님)
	case 22: return {EArrival::AtLighthouse, 3000.f, NAME_None};	// 등대 언덕
	case 16: return {EArrival::AtCamp, 1000.f, NAME_None};	// 4장 1단계 작살단 캠프 찾기
	case 18: return {EArrival::CagesBroken, 0.f, NAME_None};	// 4장 3단계 캠프 우리 3개 부수기
	case 20: return {EArrival::NearHook, 400.f, NAME_None};	// 4장 5단계 갈고리(캠프 모닥불 앞) 곁 — 11단계 모래 곁과 같은 반경
	case 25: return {EArrival::LightPressed, 600.f, NAME_None};	// 5장 5단계 등대 꼭대기에서 불 켜기 — 탑 600cm 안에서 상황 버튼 「불 켜기」 를 누르면 (꼭대기는 못 올라 탑 앞에서)
	default: return {};
	}
}

bool IsArrived(const FArrival& Arrival, float GiverDistanceCm, FName RegionId, float LighthouseDistanceCm, float CampDistanceCm, bool bCagesBroken, float MollaeDistanceCm, bool bLighthouseLit, float HookDistanceCm)
{
	switch (Arrival.Kind)
	{
	case EArrival::NearGiver: return GiverDistanceCm <= Arrival.RadiusCm;
	case EArrival::InRegion: return !RegionId.IsNone() && RegionId == Arrival.RegionId;
	case EArrival::AtLighthouse: return LighthouseDistanceCm <= Arrival.RadiusCm;
	case EArrival::AtCamp: return CampDistanceCm <= Arrival.RadiusCm;
	case EArrival::CagesBroken: return bCagesBroken;
	case EArrival::NearMollae: return MollaeDistanceCm <= Arrival.RadiusCm;
	case EArrival::LightPressed: return bLighthouseLit;
	case EArrival::NearHook: return HookDistanceCm <= Arrival.RadiusCm;
	default: return true;
	}
}

bool CanPressLighthouseLight(int32 CurrentStep, float LighthouseDistanceCm, bool bAlreadyPressed)
{
	const FArrival Arrival = ArrivalFor(CurrentStep);
	return Arrival.Kind == EArrival::LightPressed && !bAlreadyPressed && LighthouseDistanceCm <= Arrival.RadiusCm;
}

namespace
{
	int32 KnownCurrentStep = 0;
}

void SetKnownCurrentStep(int32 CurrentStep)
{
	KnownCurrentStep = CurrentStep;
}

int32 GetKnownCurrentStep()
{
	return KnownCurrentStep;
}

namespace
{
	bool bKnownDone = false;
}

void SetKnownDone(bool bDone)
{
	bKnownDone = bDone;
}

bool GetKnownDone()
{
	return bKnownDone;
}

float LighthouseBrightness(bool bDone, float Night)
{
	return bDone ? FMath::Clamp(Night, 0.f, 1.f) : 0.f;
}

float LighthouseIgnitionBrightness(float SecondsSinceStart)
{
	if (SecondsSinceStart < 0.f || SecondsSinceStart >= LighthouseIgnitionSeconds())
	{
		return 0.f;
	}
	return FMath::Clamp((SecondsSinceStart - LighthouseGlanceBlendSeconds) / LighthouseIgnitionRampSeconds, 0.f, 1.f);
}

float LighthouseIgnitionExtraYaw(float SecondsSinceStart)
{
	return FMath::Clamp((SecondsSinceStart - LighthouseGlanceBlendSeconds) / LighthouseGlanceHoldSeconds, 0.f, 1.f) * 360.f;
}

float LighthouseBeamYaw(double Seconds)
{
	const double Phase = FMath::Fmod(Seconds, LighthouseTurnSeconds);
	return static_cast<float>((Phase < 0.0 ? Phase + LighthouseTurnSeconds : Phase) / LighthouseTurnSeconds * 360.0);
}

bool FPopupQueue::RequestChapterEnd(int32 Chapter, bool bPersist, bool bDialogueOpen, bool bChapterEndOpen)
{
	if (Chapter <= 0 || bChapterEndOpen)
	{
		return false;
	}
	if (bDialogueOpen)
	{
		WaitingChapter = FMath::Max(WaitingChapter, Chapter);
		bWaitingChapterPersist = bPersist;
		return false;
	}
	return true;
}

bool FPopupQueue::RequestDialogue(bool bChapterEndOpen)
{
	if (bChapterEndOpen)
	{
		bDialogueWaiting = true;
		return false;
	}
	return true;
}

int32 FPopupQueue::TakeChapterAfterDialogue(bool& bOutPersist)
{
	const int32 Chapter = WaitingChapter;
	bOutPersist = bWaitingChapterPersist;
	WaitingChapter = 0;
	bWaitingChapterPersist = true;
	return Chapter;
}

bool FPopupQueue::TakeDialogueAfterChapterEnd()
{
	const bool bWaiting = bDialogueWaiting;
	bDialogueWaiting = false;
	return bWaiting;
}

FTransform LighthouseGlancePose(const FVector& TowerBase, const FVector& PlayerLocation)
{
	FVector Away(PlayerLocation.X - TowerBase.X, PlayerLocation.Y - TowerBase.Y, 0.0);
	if (!Away.Normalize())
	{
		Away = FVector(-1.0, 0.0, 0.0);
	}
	const FVector Eye = TowerBase + Away * LighthouseGlanceDistanceCm + FVector(0.0, 0.0, LighthouseGlanceEyeHeightCm);
	const FVector Look = TowerBase + FVector(0.0, 0.0, LighthouseGlanceLookHeightCm);
	return FTransform((Look - Eye).Rotation(), Eye);
}
}

#undef LOCTEXT_NAMESPACE
