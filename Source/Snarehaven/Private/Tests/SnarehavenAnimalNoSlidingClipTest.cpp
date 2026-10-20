// ROADMAP M41-01 2층 조사 — **움직이는데 대기 클립이 도는(다리가 선 채 미끄러지는) 종을 센다.**
// `DT_AnimalTable` 행마다 진짜 행으로 동물을 세우고(게임과 같은 `InitializeFromTable` 길), 「걷기」·「도주」 두 움직임
// 상태에서 `ChooseLocomotionAnim` 이 실제로 고른 클립을 대기 클립과 맞댄다. 목장 펫(`ARanchPetActor`)은 걷기 하나.
// 이 조각은 실패를 내지 않는다 — 걸린 칸을 `AddInfo` 로 적기만 한다(M41-06 이 단정으로 굳힌다).
// 판단은 칸 이름이 아니라 고른 클립 객체로 한다 (PROMPT ③-12) — 칸이 차 있어도 대기와 같은 클립이면 걸린다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Animal.Clips.SlideSurvey; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Animation/AnimSequence.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ranch/RanchPetActor.h"

namespace
{
	const TCHAR* SlideSurveyTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	// **검사 전용 값** (Verification.md 6) — 도주 상태에 넣을 1초, 펫이 한 번은 걷기 시작할 만큼의 틱(30fps × 20초,
	// `SnarehavenRanchPetTest` 와 같은 길이).
	constexpr float SurveyEscapeSeconds = 1.f;
	constexpr float SurveyPetFrameSeconds = 1.f / 30.f;
	constexpr int32 SurveyPetMaxFrames = 30 * 20;

	FString ClipName(const UAnimSequence* Clip)
	{
		return IsValid(Clip) ? Clip->GetName() : TEXT("없음");
	}

	/** 움직이는 중으로 만들고 고른 클립을 돌려준다 — 문턱 판정은 동물 쪽 `UpdateLocomotionAnimation` 이 한다. */
	UAnimSequence* ChooseWhileMoving(AAnimalCharacter& Animal)
	{
		Animal.GetCharacterMovement()->Velocity = FVector(Animal.GetMoveAnimEnterSpeed() * 2.f, 0.f, 0.f);
		Animal.UpdateLocomotionAnimation();
		return Animal.ChooseLocomotionAnim();
	}

	/**
	 * 「도주」 상태로 넣는다. 쫓는 종은 맞아도(Fight) 걷기로 다가오므로, 게임에서 그 종이 도주 클립을 쓰는 유일한 길인
	 * 링 실패 도주(Escaping)로 넣는다 — 체력을 포획 임계까지 깎고 `BeginEscape`. 못 넣으면(우두머리 등) false.
	 */
	bool EnterFleeState(UAnimalStateComponent& State)
	{
		const float ToThreshold = State.GetCurrentHP() - State.GetCaptureThresholdHP();
		if (State.GetCaptureThresholdHP() <= 0.f || ToThreshold <= 0.f)
		{
			return false;
		}
		State.ApplyDamage(ToThreshold);
		return State.BeginEscape(SurveyEscapeSeconds);
	}

	/**
	 * 표의 행마다 움직임 칸(걷기·도주·펫 걷기)이 실제로 고른 클립을 대기 클립과 맞대어, 미끄러지는 칸을 `OutSlides` 에 담는다.
	 * 칸마다 정보 줄을 남긴다. `OutRows` = 돈 행 수(0 이면 빈 통과라 부르는 쪽이 실패로 낸다).
	 */
	void SurveySlides(FAutomationTestBase& Test, UDataTable* Table, UWorld* World, int32& OutRows, TArray<FString>& OutSlides)
	{
		OutRows = 0;
		OutSlides.Reset();
		for (const FName& RowName : Table->GetRowNames())
		{
			const FAnimalRow* const Row = Table->FindRow<FAnimalRow>(RowName, TEXT("AnimalSlideSurvey"));
			if (Row == nullptr)
			{
				continue;
			}
			++OutRows;
			const FString Label = FString::Printf(TEXT("%s(%s)"), *RowName.ToString(), *Row->DisplayName.ToString());
			UAnimSequence* const Idle = Row->IdleAnimRef.LoadSynchronous();
			if (Row->MeshRef.IsNull() || Idle == nullptr)
			{
				// 몸이나 대기 클립이 없으면 클립을 고를 일 자체가 없다 — 미끄러짐이 아니라 다른 결함이라 따로 적는다.
				Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s — 메시·대기 클립 없음, 건너뜀"), *Label));
				continue;
			}

			auto Note = [&](const TCHAR* StateName, UAnimSequence* Chosen)
			{
				const bool bSlides = Chosen == nullptr || Chosen == Idle;
				if (bSlides)
				{
					OutSlides.Add(FString::Printf(TEXT("%s %s"), *Label, StateName));
				}
				Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s %s → %s%s"), *Label, StateName, *ClipName(Chosen),
					bSlides ? TEXT(" ← 대기 클립(미끄러짐)") : TEXT("")));
			};

			// ── 사냥터 동물: 걷기 · 도주 ──
			for (const bool bFlee : { false, true })
			{
				AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>();
				if (Animal == nullptr)
				{
					Test.AddError(FString::Printf(TEXT("[%s] 동물 액터를 못 스폰했다"), *Label));
					continue;
				}
				Animal->SetAnimalData(Table, RowName);
				if (!Animal->InitializeFromTable())
				{
					Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s — InitializeFromTable 실패, 건너뜀"), *Label));
					Animal->Destroy();
					continue;
				}
				if (bFlee && !EnterFleeState(*Animal->GetAnimalState()))
				{
					Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s 도주 — 도주 상태로 못 넣음(포획 임계 0), 건너뜀"), *Label));
					Animal->Destroy();
					continue;
				}
				Note(bFlee ? TEXT("도주") : TEXT("걷기"), ChooseWhileMoving(*Animal));
				Animal->Destroy();
			}

			// ── 목장 펫: 걷기 ── 걷기 시작할 때까지 틱하고 그때 건 클립을 본다(펫은 도주가 없다).
			ARanchPetActor* const Pet = World->SpawnActor<ARanchPetActor>();
			if (Pet == nullptr || !Pet->ApplyRow(*Row))
			{
				Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s 펫 — ApplyRow 실패, 건너뜀"), *Label));
			}
			else
			{
				Pet->SetTetherHome(Pet->GetActorLocation());
				for (int32 Frame = 0; Frame < SurveyPetMaxFrames && !Pet->IsWalking(); ++Frame)
				{
					Pet->Tick(SurveyPetFrameSeconds);
				}
				if (Pet->IsWalking())
				{
					Note(TEXT("펫 걷기"), Pet->GetCurrentClip());
				}
				else
				{
					Test.AddInfo(FString::Printf(TEXT("[NoSliding] %s 펫 — 20초 안에 안 걸음, 건너뜀"), *Label));
				}
			}
			if (Pet != nullptr)
			{
				Pet->Destroy();
			}
		}
	}

	UWorld* MakeSurveyWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroySurveyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalNoSlidingClipTest, "Snarehaven.Animal.Clips.NoSlidingClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalNoSlidingClipTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, SlideSurveyTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}

	UWorld* const World = MakeSurveyWorld();
	int32 RowsSeen = 0;
	TArray<FString> SlideList;
	SurveySlides(*this, Table, World, RowsSeen, SlideList);
	DestroySurveyWorld(World);

	AddInfo(FString::Printf(TEXT("[NoSliding] 합계 — 행 %d · 미끄러지는 칸 %d: %s"),
		RowsSeen, SlideList.Num(), *FString::Join(SlideList, TEXT(" · "))));
	for (const FString& Cell : SlideList)
	{
		AddError(FString::Printf(TEXT("[NoSliding] %s — 움직이는데 대기 클립이 돈다(다리가 선 채 미끄러짐)"), *Cell));
	}
	// 행을 하나도 못 돌았으면 빈 통과다.
	TestTrue(TEXT("표의 행을 돌았다"), RowsSeen > 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalNoSlidingClipCatchesEmptyRowTest,
	"Snarehaven.Animal.Clips.NoSlidingClipCatchesEmptyRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalNoSlidingClipCatchesEmptyRowTest::RunTest(const FString& Parameters)
{
	// 역검증 — 표를 메모리 사본으로 떠서 걷기·도주 칸이 다 찬 첫 행을 비우면, 같은 조사가 그 행을 잡아야 한다.
	UDataTable* const Source = LoadObject<UDataTable>(nullptr, SlideSurveyTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Source))
	{
		return false;
	}
	UDataTable* const Copy = DuplicateObject<UDataTable>(Source, GetTransientPackage());
	if (!TestNotNull(TEXT("표 사본"), Copy))
	{
		return false;
	}

	FName Emptied = NAME_None;
	for (const FName& RowName : Copy->GetRowNames())
	{
		FAnimalRow* const Row = Copy->FindRow<FAnimalRow>(RowName, TEXT("NoSlidingReverse"));
		if (Row != nullptr && !Row->WalkAnimRef.IsNull() && !Row->FleeAnimRef.IsNull())
		{
			Row->WalkAnimRef.Reset();
			Row->FleeAnimRef.Reset();
			Emptied = RowName;
			break;
		}
	}
	if (!TestTrue(TEXT("걷기·도주 칸이 다 찬 행이 있다"), Emptied != NAME_None))
	{
		return false;
	}

	UWorld* const World = MakeSurveyWorld();
	int32 RowsSeen = 0;
	TArray<FString> SlideList;
	SurveySlides(*this, Copy, World, RowsSeen, SlideList);
	DestroySurveyWorld(World);

	const FString Prefix = Emptied.ToString() + TEXT("(");
	const int32 Caught = SlideList.FilterByPredicate([&Prefix](const FString& Cell) { return Cell.StartsWith(Prefix); }).Num();
	AddInfo(FString::Printf(TEXT("[NoSliding] 역검증 — 사본에서 %s 걷기·도주 비움 → 걸린 칸 %d: %s"),
		*Emptied.ToString(), Caught, *FString::Join(SlideList, TEXT(" · "))));
	TestTrue(TEXT("비운 행의 움직임 칸이 걸린다"), Caught > 0);
	TestEqual(TEXT("비운 행 밖은 안 걸린다"), SlideList.Num(), Caught);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
