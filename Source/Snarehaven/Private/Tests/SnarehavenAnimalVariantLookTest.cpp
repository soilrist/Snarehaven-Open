// ROADMAP M21-07a2 2층 검사 — 색 변종 겉모습(DESIGN 10.6 「변종 겉모습」)이
// ① 변종이면 몸에 `M_HitFlash` 동적 인스턴스 한 겹이 걸리고 이름 카드가 `변종 ○○` 인지,
// ② 흰 깜빡임이 그 겹을 잠시 덮었다가 끝나면 맨몸이 아니라 변종 겹으로 돌아가는지,
// ③ 일반 개체는 겹도 딱지도 없는지(깜빡임 뒤에도).
// 숫자는 전부 **검사 전용 값**이다 (Verification.md 6).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Animal.Variant; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "CoreGlobals.h"

namespace
{
	const TCHAR* FoxMeshPath = TEXT("/Game/Game/Characters/Animals/Fox/SK_Animal_Fox.SK_Animal_Fox");
	const TCHAR* HitFlashPath = TEXT("/Game/Game/VFX/Hit/M_HitFlash.M_HitFlash");

	// 검사 전용 값.
	constexpr float TestMaxHP = 100.f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr float TestFlashSeconds = 0.25f;
	constexpr float TimerStepSeconds = 0.01f;
	constexpr float FrameSeconds = 1.f / 60.f;
	const FText TestDisplayName = FText::FromString(TEXT("검사여우"));

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// `GFrameCounter` 를 같이 올려야 한 프레임 두 번째 Tick 이 그냥 돌아가지 않는다 (SnarehavenHitFeelTest 와 같다).
	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TimerStepSeconds);
		}
	}

	AAnimalCharacter* SpawnFox(UWorld& World, float OffsetCm)
	{
		AAnimalCharacter* const Animal = World.SpawnActor<AAnimalCharacter>(FVector(OffsetCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_variant_look");
		Row.DisplayName = TestDisplayName;
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.PetItemID = TEXT("item.pet_test_look");
		Row.VariantPetItemID = TEXT("item.pet_test_look_variant");
		Row.VariantChance = 0.f;
		Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FoxMeshPath));
		Animal->ApplyBodyFromRow(Row);
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Animal->GetCharacterMovement()->MaxFlySpeed = 0.f;
		return Animal;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalVariantLookTest, "Snarehaven.Animal.Variant.LookAndCardName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalVariantLookTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	AAnimalCharacter* const Variant = SpawnFox(*World, 0.f);
	AAnimalCharacter* const Plain = SpawnFox(*World, 300.f);
	if (!TestNotNull(TEXT("변종 여우"), Variant) || !TestNotNull(TEXT("일반 여우"), Plain)
		|| !TestNotNull(TEXT("여우 몸 메시"), Variant->GetMesh()->GetSkeletalMeshAsset()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 변종 — 복제 값만 바뀌어도 다음 틱에 겹이 걸린다(게스트 길과 같다).
	TestTrue(TEXT("변종으로 정한다"), Variant->GetAnimalState()->SetVariant(true));
	Variant->Tick(FrameSeconds);
	Plain->Tick(FrameSeconds);

	TestTrue(TEXT("변종 몸에 색 겹이 걸렸다"), Variant->IsVariantLookActive());
	const UMaterialInstanceDynamic* const Overlay = Cast<UMaterialInstanceDynamic>(Variant->GetMesh()->GetOverlayMaterial());
	const UMaterialInterface* const FlashMaterial = LoadObject<UMaterialInterface>(nullptr, HitFlashPath);
	TestTrue(TEXT("겹 = M_HitFlash 동적 인스턴스(새 에셋 0)"), Overlay != nullptr && Overlay->Parent == FlashMaterial);
	// 플래그가 없으면 게임에서 기본 머티리얼로 바뀌어 금빛·흰 깜빡임이 아예 안 그려진다(루프#478).
	const UMaterial* const FlashBase = FlashMaterial != nullptr ? FlashMaterial->GetMaterial() : nullptr;
	TestTrue(TEXT("M_HitFlash 스켈레탈 메시 사용 플래그"), FlashBase != nullptr && FlashBase->bUsedWithSkeletalMesh);
	FLinearColor OverlayColor = FLinearColor::White;
	float OverlayOpacity = 1.f;
	if (Overlay != nullptr)
	{
		Overlay->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FlashColor")), OverlayColor);
		Overlay->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FlashOpacity")), OverlayOpacity);
	}
	AddInfo(FString::Printf(TEXT("[Variant] 겹 색 %s · 불투명도 %.2f"), *OverlayColor.ToString(), OverlayOpacity));
	TestFalse(TEXT("겹 색이 흰 깜빡임(흰색)과 다르다"), OverlayColor.Equals(FLinearColor::White));
	TestTrue(TEXT("겹은 반투명(몸 무늬가 비친다)"), OverlayOpacity > 0.f && OverlayOpacity < 1.f);

	const FString VariantName = Variant->GetAnimalState()->GetCardName().ToString();
	AddInfo(FString::Printf(TEXT("[Variant] 이름 카드 '%s' · 일반 '%s'"), *VariantName,
		*Plain->GetAnimalState()->GetCardName().ToString()));
	TestEqual(TEXT("변종 이름 카드"), VariantName, FString(TEXT("변종 검사여우")));

	// ③ 일반 — 겹도 딱지도 없다.
	TestFalse(TEXT("일반 몸엔 겹이 없다"), Plain->IsVariantLookActive());
	TestNull(TEXT("일반 몸 오버레이 비었음"), Plain->GetMesh()->GetOverlayMaterial());
	TestEqual(TEXT("일반 이름 카드"), Plain->GetAnimalState()->GetCardName().ToString(), TestDisplayName.ToString());

	// ② 흰 깜빡임이 덮고, 끝나면 변종은 변종 겹으로 · 일반은 맨몸으로.
	Variant->Multicast_PlayHitReaction(TestFlashSeconds, 0);
	Plain->Multicast_PlayHitReaction(TestFlashSeconds, 0);
	Variant->Tick(FrameSeconds);
	TestTrue(TEXT("변종도 맞으면 흰 깜빡임"), Variant->IsHitFlashActive());
	TestFalse(TEXT("깜빡임 중엔 색 겹이 아니다(틱이 덮지 않음)"), Variant->IsVariantLookActive());

	AdvanceTimers(World, TestFlashSeconds + 0.05f);
	TestFalse(TEXT("깜빡임이 끝났다"), Variant->IsHitFlashActive());
	TestTrue(TEXT("깜빡임 뒤 변종 겹으로 돌아왔다"), Variant->IsVariantLookActive());
	TestNull(TEXT("깜빡임 뒤 일반은 맨몸"), Plain->GetMesh()->GetOverlayMaterial());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalStoryLookTest, "Snarehaven.Animal.Variant.StoryLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalStoryLookTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	AAnimalCharacter* const Story = SpawnFox(*World, 0.f);
	AAnimalCharacter* const Plain = SpawnFox(*World, 300.f);
	if (!TestNotNull(TEXT("이야기 여우"), Story) || !TestNotNull(TEXT("일반 여우"), Plain))
	{
		DestroyTestWorld(World);
		return false;
	}

	Story->SetStoryLookForTest(true);
	Story->Tick(FrameSeconds);
	Plain->Tick(FrameSeconds);
	TestTrue(TEXT("이야기 종 몸에 안개 겹이 걸렸다"), Story->IsStoryLookActive());
	TestFalse(TEXT("안개 겹은 변종 금빛 겹이 아니다"), Story->IsVariantLookActive());
	TestFalse(TEXT("일반 몸엔 안개 겹이 없다"), Plain->IsStoryLookActive());
	TestNull(TEXT("일반 몸 오버레이 비었음"), Plain->GetMesh()->GetOverlayMaterial());

	const UMaterialInstanceDynamic* const Overlay = Cast<UMaterialInstanceDynamic>(Story->GetMesh()->GetOverlayMaterial());
	FLinearColor OverlayColor = FLinearColor::Black;
	if (Overlay != nullptr)
	{
		Overlay->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FlashColor")), OverlayColor);
	}
	// 순백은 설원 바닥에 묻혔다(#1190) — 푸른 쪽이 크고(B > R) 금빛 변종(R 큼)과도 갈리는 찬 색이어야 한다.
	TestTrue(TEXT("겹 색이 눈 바닥과 갈리는 찬 청록이다"), OverlayColor.B > 0.8f && OverlayColor.B > OverlayColor.R + 0.3f);

	Story->Multicast_PlayHitReaction(TestFlashSeconds, 0);
	Story->Tick(FrameSeconds);
	TestTrue(TEXT("맞으면 흰 깜빡임이 덮는다"), Story->IsHitFlashActive());
	AdvanceTimers(World, TestFlashSeconds + 0.05f);
	TestTrue(TEXT("깜빡임 뒤 안개 겹으로 돌아왔다"), Story->IsStoryLookActive());

	DestroyTestWorld(World);
	return true;
}

#endif
