// ROADMAP M0-03/M0-04 2층 검사 — 입력을 넣으면 캐릭터 위치가 바뀌는지, 입력 에셋 배선이 살아 있는지,
// 탑다운 카메라가 캐릭터에 매달려 고정 각을 유지하는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Data/ItemRow.h"
#include "Equipment/EquipmentComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다).
	constexpr float TestDeltaSeconds = 1.0f / 60.0f;
	constexpr int32 TestTickCount = 10;
	// 카메라 셋은 DESIGN 3.3 표 값 그대로다(M15-01 레퍼런스 구도) — 설계서 숫자가 바뀌면 여기도 같이 바꾼다.
	constexpr float TestCameraDistance = 4500.0f;
	constexpr float TestCameraPitchDegrees = 55.0f;
	constexpr float TestCameraFieldOfViewDegrees = 30.0f;
	constexpr float TestCameraZoomMin = 0.6f;
	constexpr float TestCameraZoomMax = 1.3f;
	constexpr float TestInteriorCameraMul = 0.45f;
	constexpr float TestAngleTolerance = 0.01f;
	constexpr float TestDistanceTolerance = 1.0f;
	// 합격 폭도 DESIGN 3.3 표 값이다 — 캐릭터 키가 화면 높이에서 차지하는 몫(레퍼런스 구도).
	constexpr float TestFillFractionMin = 0.06f;
	constexpr float TestFillFractionMax = 0.09f;
	// 화면 크기는 테스트 전용 값이다 (Verification.md 6) — 4:3 은 3층 캡처, 19.5:9 는 아이폰 화면.
	constexpr int32 TestCaptureSizeX = 1440;
	constexpr int32 TestCaptureSizeY = 1080;
	constexpr int32 TestPhoneSizeX = 2340;
	constexpr int32 TestPhoneSizeY = 1080;
	// 엔진 기본 실린더(/Engine/BasicShapes/Cylinder)의 반치수 — 엔진 에셋의 사실이지 설계 숫자가 아니다.
	constexpr float TestEngineCylinderHalfExtent = 50.0f;

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCharacterMoveTest, "Snarehaven.Character.MoveAxis.ChangesLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCharacterMoveTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("캐릭터 스폰"), Character))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 컨트롤러가 없으면 APawn::IsMoveInputIgnored() 가 true 라 입력이 통째로 버려진다 — 기본 AI 컨트롤러를 붙인다.
	Character->SpawnDefaultController();
	if (!TestNotNull(TEXT("컨트롤러 부착"), Character->GetController()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 테스트 월드엔 바닥이 없다 — 낙하 대신 수평 이동만 보려고 비행 모드로.
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	const FVector StartLocation = Character->GetActorLocation();
	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		Character->MoveByAxis(FVector2D(1.0f, 0.0f));   // 조이스틱을 오른쪽으로
		// 손으로 만든 테스트 월드에선 컴포넌트 틱 함수가 등록되지 않아 World->Tick 이 이동을 안 돌린다 —
		// 이동 컴포넌트 틱을 직접 부른다 (틱 함수 인자는 엔진에서 상위 호출로만 넘겨져 nullptr 로 안전).
		Character->GetCharacterMovement()->TickComponent(TestDeltaSeconds, LEVELTICK_All, nullptr);
	}
	const FVector RightLocation = Character->GetActorLocation();
	TestTrue(TEXT("오른쪽 입력이면 월드 +Y 로 움직인다"), RightLocation.Y > StartLocation.Y);

	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		Character->MoveByAxis(FVector2D(0.0f, 1.0f));   // 조이스틱을 위로
		// 손으로 만든 테스트 월드에선 컴포넌트 틱 함수가 등록되지 않아 World->Tick 이 이동을 안 돌린다 —
		// 이동 컴포넌트 틱을 직접 부른다 (틱 함수 인자는 엔진에서 상위 호출로만 넘겨져 nullptr 로 안전).
		Character->GetCharacterMovement()->TickComponent(TestDeltaSeconds, LEVELTICK_All, nullptr);
	}
	TestTrue(TEXT("위쪽 입력이면 월드 +X 로 움직인다"), Character->GetActorLocation().X > RightLocation.X);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDefaultInputContextTest, "Snarehaven.Input.DefaultContext.MapsMoveAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDefaultInputContextTest::RunTest(const FString& Parameters)
{
	const UInputAction* MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Game/Input/IA_Move.IA_Move"));
	const UInputMappingContext* Context =
		LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IA_Move 로드"), MoveAction) || !TestNotNull(TEXT("IMC_Default 로드"), Context))
	{
		return false;
	}

	TestEqual(TEXT("IA_Move 는 Axis2D"), MoveAction->ValueType, EInputActionValueType::Axis2D);

	bool bFoundJoystickMapping = false;
	Context->ForEachKeyMapping([&bFoundJoystickMapping, MoveAction](const FEnhancedActionKeyMapping& Mapping)
	{
		// 엔진 기본 터치 인터페이스의 왼쪽 스틱(Gamepad_LeftX/LeftY)이 짝을 이루는 2D 키.
		if (Mapping.Action == MoveAction && Mapping.Key == EKeys::Gamepad_Left2D)
		{
			bFoundJoystickMapping = true;
		}
	});
	TestTrue(TEXT("IMC_Default 에 IA_Move <- Gamepad_Left2D 매핑이 있다"), bFoundJoystickMapping);

	return true;
}

// ROADMAP M1-10a 2층 검사 — 데스크톱 키보드 이동. 맥 `-game` 창에서는 가상 조이스틱을 마우스로 못 끌기 때문에
// W/A/S/D 와 방향키 여덟 개가 IA_Move 로 들어와야 한다. 키보드 키는 1D 라 Axis2D 의 X 로만 들어오므로
// 위/아래는 Swizzle(YXZ) 로 Y 에 보내고, 아래·왼쪽은 Negate 로 부호를 뒤집는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenKeyboardMoveMappingTest, "Snarehaven.Input.DefaultContext.MapsKeyboardMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenKeyboardMoveMappingTest::RunTest(const FString& Parameters)
{
	const UInputAction* MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Game/Input/IA_Move.IA_Move"));
	const UInputMappingContext* Context =
		LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IA_Move 로드"), MoveAction) || !TestNotNull(TEXT("IMC_Default 로드"), Context))
	{
		return false;
	}

	// 키 / 위아래 축인가(Swizzle 필요) / 반대 방향인가(Negate 필요)
	struct FExpectedMoveKey
	{
		FKey Key;
		bool bVertical;
		bool bNegative;
	};
	const TArray<FExpectedMoveKey> Expected = {
		{ EKeys::W, true, false },      { EKeys::S, true, true },
		{ EKeys::A, false, true },      { EKeys::D, false, false },
		{ EKeys::Up, true, false },     { EKeys::Down, true, true },
		{ EKeys::Left, false, true },   { EKeys::Right, false, false },
	};

	for (const FExpectedMoveKey& Entry : Expected)
	{
		bool bFound = false;
		bool bHasSwizzleToY = false;
		bool bHasNegate = false;
		Context->ForEachKeyMapping([&](const FEnhancedActionKeyMapping& Mapping)
		{
			if (Mapping.Action != MoveAction || Mapping.Key != Entry.Key)
			{
				return;
			}
			bFound = true;
			for (const UInputModifier* Modifier : Mapping.Modifiers)
			{
				if (const UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
				{
					bHasSwizzleToY |= (Swizzle->Order == EInputAxisSwizzle::YXZ);
				}
				else if (Cast<UInputModifierNegate>(Modifier) != nullptr)
				{
					bHasNegate = true;
				}
			}
		});

		TestTrue(FString::Printf(TEXT("IMC_Default 에 IA_Move <- %s 매핑이 있다"), *Entry.Key.ToString()), bFound);
		TestEqual(FString::Printf(TEXT("%s 의 Swizzle(YXZ) 유무"), *Entry.Key.ToString()), bHasSwizzleToY, Entry.bVertical);
		TestEqual(FString::Printf(TEXT("%s 의 Negate 유무"), *Entry.Key.ToString()), bHasNegate, Entry.bNegative);
	}

	// 마우스로는 움직이지 않는다 (감독 결정 2026-09-20 — 마우스 이동 없음).
	bool bFoundMouseMove = false;
	Context->ForEachKeyMapping([&bFoundMouseMove, MoveAction](const FEnhancedActionKeyMapping& Mapping)
	{
		if (Mapping.Action == MoveAction && Mapping.Key.IsMouseButton())
		{
			bFoundMouseMove = true;
		}
	});
	TestFalse(TEXT("IA_Move 에 마우스 버튼 매핑은 없다"), bFoundMouseMove);

	return true;
}

// INBOX 18:20 ③ 2층 검사 — PC 스페이스바 = 화면 상황 버튼. 스페이스바가 `IA_Attack` 에 걸려 있어야
// 화면 버튼과 **같은** `HandleAttackInputStarted -> BeginAttack` 으로 들어가 공격·채집·베기·포획·낚시가 상황대로
// 된다. 다른 액션에도 걸리면 한 번 누름이 두 일을 한다 — 그래서 스페이스바는 `IA_Attack` 한 곳만.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSpaceBarSituationActionTest,
	"Snarehaven.Input.DefaultContext.SpaceBarIsSituationAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSpaceBarSituationActionTest::RunTest(const FString& Parameters)
{
	const UInputAction* AttackAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Game/Input/IA_Attack.IA_Attack"));
	const UInputMappingContext* Context =
		LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IA_Attack 로드"), AttackAction) || !TestNotNull(TEXT("IMC_Default 로드"), Context))
	{
		return false;
	}

	int32 AttackMappings = 0;
	int32 OtherMappings = 0;
	Context->ForEachKeyMapping([&](const FEnhancedActionKeyMapping& Mapping)
	{
		if (Mapping.Key != EKeys::SpaceBar)
		{
			return;
		}
		if (Mapping.Action == AttackAction)
		{
			++AttackMappings;
		}
		else
		{
			++OtherMappings;
		}
	});
	AddInfo(FString::Printf(TEXT("[SpaceBar] IA_Attack 매핑 %d · 다른 액션 매핑 %d"), AttackMappings, OtherMappings));

	TestEqual(TEXT("IMC_Default 에 IA_Attack <- SpaceBar 매핑이 하나 있다"), AttackMappings, 1);
	TestEqual(TEXT("스페이스바는 다른 액션에 걸리지 않는다"), OtherMappings, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTopDownCameraTest, "Snarehaven.Camera.TopDown.FollowsCharacterAtFixedAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenTopDownCameraTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("캐릭터 스폰"), Character))
	{
		DestroyTestWorld(World);
		return false;
	}

	USpringArmComponent* CameraBoom = Character->GetCameraBoom();
	UCameraComponent* Camera = Character->GetTopDownCamera();
	if (!TestNotNull(TEXT("CameraBoom 부착"), CameraBoom) || !TestNotNull(TEXT("TopDownCamera 부착"), Camera))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 캐릭터가 화면 중앙에 남으려면 카메라가 팔에, 팔이 캐릭터 루트에 매달려 있어야 한다.
	TestTrue(TEXT("카메라는 CameraBoom 에 붙어 있다"), Camera->GetAttachParent() == CameraBoom);
	TestTrue(TEXT("CameraBoom 은 캐릭터 루트에 붙어 있다"),
		CameraBoom->GetAttachParent() == Character->GetRootComponent());

	// 고정 각: 컨트롤러·캐릭터가 돌아도 팔이 따라 돌지 않는다.
	TestFalse(TEXT("CameraBoom 은 컨트롤러 회전을 따르지 않는다"), CameraBoom->bUsePawnControlRotation);
	TestFalse(TEXT("CameraBoom 은 Pitch 를 상속하지 않는다"), CameraBoom->bInheritPitch);
	TestFalse(TEXT("CameraBoom 은 Yaw 를 상속하지 않는다"), CameraBoom->bInheritYaw);
	TestFalse(TEXT("CameraBoom 은 Roll 을 상속하지 않는다"), CameraBoom->bInheritRoll);
	TestTrue(TEXT("CameraBoom 회전은 월드 기준 — 몸 회전이 붙는 틈이 없다"), CameraBoom->IsUsingAbsoluteRotation());
	// 프로젝트 기본 모션 블러 끔(DefaultEngine.ini) — 켜져 있으면 몸을 홱 돌린 장에 화면이 번진다(M39-07b).
	const IConsoleVariable* const MotionBlur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.MotionBlur"));
	TestTrue(TEXT("모션 블러 기본값 끔"), MotionBlur != nullptr && MotionBlur->GetInt() == 0);

	Character->ApplyCameraSettings(TestCameraDistance, TestCameraPitchDegrees);
	TestEqual(TEXT("거리가 SpringArm 에 반영된다"), CameraBoom->TargetArmLength, TestCameraDistance);
	TestEqual(TEXT("화각이 카메라에 반영된다 (DESIGN 3.3)"), Camera->FieldOfView, TestCameraFieldOfViewDegrees,
		TestAngleTolerance);
	TestTrue(TEXT("화각은 세로 고정 — 폰 비율이 달라도 캐릭터 몫이 같다 (DESIGN 3.3)"),
		Camera->bOverrideAspectRatioAxisConstraint
		&& Camera->AspectRatioAxisConstraint == EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
	TestEqual(TEXT("내려다보는 각이 SpringArm 에 반영된다"),
		static_cast<double>(CameraBoom->GetRelativeRotation().Pitch),
		static_cast<double>(-TestCameraPitchDegrees), static_cast<double>(TestAngleTolerance));

	// 미정(-1)이면 추측값으로 덮지 않고 방금 넣은 값을 그대로 둔다 (DataDriven.md 3).
	Character->ApplyCameraSettings(-1.f, -1.f);
	TestEqual(TEXT("미정 값은 SpringArm 을 건드리지 않는다"), CameraBoom->TargetArmLength, TestCameraDistance);

	// 캐릭터가 돌아도 카메라 월드 각이 그대로여야 탑다운이 유지된다.
	// 손으로 만든 테스트 월드에선 틱 함수가 등록되지 않아 SpringArm 의 소켓 계산을 직접 돌린다
	// (M0-03 에서 이동 컴포넌트에 같은 함정이 있었다).
	CameraBoom->TickComponent(TestDeltaSeconds, LEVELTICK_All, nullptr);

	// 소켓 계산이 실제로 돌았다는 증거 — 안 돌면 아래 "각 고정" 단언이 그냥 통과해 버린다.
	TestEqual(TEXT("카메라가 팔 길이만큼 떨어져 있다"),
		FVector::Dist(Camera->GetComponentLocation(), Character->GetActorLocation()),
		static_cast<double>(TestCameraDistance), static_cast<double>(TestDistanceTolerance));

	const FRotator CameraRotationBefore = Camera->GetComponentRotation();
	Character->SetActorRotation(FRotator(0.f, 90.f, 0.f));
	// 같은 틱(팔 계산 전)에도 그대로여야 한다 — 쏘기·공격이 몸을 과녁 쪽으로 홱 돌리는 장에 카메라가 한 장
	// 몸을 따라 돌았다가 돌아와 화면 전체가 돌며 번졌다(M39-07b, `captures/583_gun_flash.png`).
	TestTrue(TEXT("몸을 돌린 같은 틱에도 카메라 각은 고정"),
		Camera->GetComponentRotation().Equals(CameraRotationBefore, TestAngleTolerance));
	CameraBoom->TickComponent(TestDeltaSeconds, LEVELTICK_All, nullptr);
	TestTrue(TEXT("캐릭터가 돌아도 카메라 각은 고정"),
		Camera->GetComponentRotation().Equals(CameraRotationBefore, TestAngleTolerance));

	// 캐릭터가 움직이면 카메라도 같이 따라간다 (화면 중앙 유지).
	const FVector BoomLocationBefore = CameraBoom->GetComponentLocation();
	Character->SetActorLocation(FVector(500.f, 0.f, 0.f));
	TestTrue(TEXT("캐릭터가 움직이면 카메라도 따라간다"),
		!CameraBoom->GetComponentLocation().Equals(BoomLocationBefore));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenToolCameraTest, "Snarehaven.Camera.TopDown.BinocularsWidenView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 13.5 망원경 — 도구 칸에 든 동안만 카메라 거리가 표의 배율만큼 멀어지고, 내리면 돌아온다.
bool FSnarehavenToolCameraTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UEquipmentComponent* const Equipment = Character != nullptr ? Character->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("캐릭터·장비"), Equipment) || !TestNotNull(TEXT("CameraBoom"), Character->GetCameraBoom()))
	{
		DestroyTestWorld(World);
		return false;
	}
	USpringArmComponent* const CameraBoom = Character->GetCameraBoom();
	// 도구 행은 메시 대기라 `EquipItem` 이 못 건다 — 호스트 목록을 옮기는 길로 칸만 채운다.
	auto Hold = [&](const TCHAR* ItemID)
	{
		TArray<FEquippedSlot> Slots;
		if (ItemID != nullptr)
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Equipment->FindEquipmentRowByItemID(ItemID);
		}
		Equipment->ApplyEquippedSlots(Slots);
		Character->UpdateToolCamera();
	};

	// 기본 거리는 캐릭터의 `CameraDistance` 다 — 망원경을 한 번 들었다 내려서 그 값을 팔에 건 뒤 잰다.
	Hold(TEXT("item.binoculars"));
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
	const float Held = CameraBoom->TargetArmLength;
	Hold(nullptr);
	const float Base = CameraBoom->TargetArmLength;
	if (!TestNotNull(TEXT("망원경 행을 들었다"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 캐릭터 기본값이 곧 설계서 숫자다 — 손으로 넣은 값이 아니라 `CameraDistance`·`CameraPitchDegrees`·화각 기본값을 잰다.
	TestEqual(TEXT("기본 거리 = DESIGN 3.3"), Base, TestCameraDistance, TestDistanceTolerance);
	TestEqual(TEXT("기본 각 = DESIGN 3.3"), static_cast<double>(CameraBoom->GetRelativeRotation().Pitch),
		static_cast<double>(-TestCameraPitchDegrees), static_cast<double>(TestAngleTolerance));
	TestEqual(TEXT("기본 화각 = DESIGN 3.3"), Character->GetTopDownCamera()->FieldOfView, TestCameraFieldOfViewDegrees,
		TestAngleTolerance);
	TestTrue(TEXT("망원경 배율은 1보다 크다"), Row->CameraDistanceMul > 1.f);
	TestEqual(TEXT("망원경을 들면 거리 × 배율"), Held, Base * Row->CameraDistanceMul, TestDistanceTolerance);
	Hold(TEXT("item.binoculars"));
	Hold(TEXT("item.satellite_communicator"));
	TestEqual(TEXT("배율 없는 도구로 바꾸면 원래 거리"), CameraBoom->TargetArmLength, Base, TestDistanceTolerance);
	AddInfo(FString::Printf(TEXT("[Camera] 기본 %.0f · 망원경 %.0f"), Base, Base * Row->CameraDistanceMul));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCameraZoomTest, "Snarehaven.Camera.TopDown.ZoomClampsAndEases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 3.3 줌 — 휠·핀치 목표는 [최소, 최대] 로 잘리고, 팔 길이는 목표를 향해 한 번에 뛰지 않고 다가가며, 망원경 배율과 곱해진다.
bool FSnarehavenCameraZoomTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("캐릭터"), Character) || !TestNotNull(TEXT("CameraBoom"), Character->GetCameraBoom()))
	{
		DestroyTestWorld(World);
		return false;
	}
	USpringArmComponent* const CameraBoom = Character->GetCameraBoom();
	Character->SetCameraZoomTarget(0.f);
	TestEqual(TEXT("가까이는 최소에서 멈춘다"), Character->GetCameraZoomTarget(), TestCameraZoomMin);
	Character->UpdateToolCamera(TestDeltaSeconds);
	const float Eased = CameraBoom->TargetArmLength;
	TestTrue(TEXT("한 프레임엔 목표까지 뛰지 않는다"),
		Eased < TestCameraDistance && Eased > TestCameraDistance * TestCameraZoomMin + TestDistanceTolerance);
	for (int32 Index = 0; Index < 300; ++Index)
	{
		Character->UpdateToolCamera(TestDeltaSeconds);
	}
	TestEqual(TEXT("충분히 지나면 기본 거리 × 최소"), CameraBoom->TargetArmLength,
		TestCameraDistance * TestCameraZoomMin, TestDistanceTolerance);
	Character->SetCameraZoomTarget(100.f);
	TestEqual(TEXT("멀리는 최대에서 멈춘다"), Character->GetCameraZoomTarget(), TestCameraZoomMax);
	AddInfo(FString::Printf(TEXT("[Zoom] %.0f ~ %.0fcm"), TestCameraDistance * TestCameraZoomMin,
		TestCameraDistance * TestCameraZoomMax));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCameraInteriorTest, "Snarehaven.Camera.TopDown.InteriorPullsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M18-02 집 실내 — 안에 들면 팔이 `CameraDistance × 실내 배율` 로 한 번에 뛰지 않고 다가가고, 나오면 원래 거리로 돌아간다.
bool FSnarehavenCameraInteriorTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("캐릭터"), Character) || !TestNotNull(TEXT("CameraBoom"), Character->GetCameraBoom()))
	{
		DestroyTestWorld(World);
		return false;
	}
	USpringArmComponent* const CameraBoom = Character->GetCameraBoom();
	Character->SetInteriorCamera(true);
	Character->UpdateToolCamera(TestDeltaSeconds);
	const float Eased = CameraBoom->TargetArmLength;
	TestTrue(TEXT("한 프레임엔 실내 거리까지 뛰지 않는다"),
		Eased < TestCameraDistance && Eased > TestCameraDistance * TestInteriorCameraMul + TestDistanceTolerance);
	for (int32 Index = 0; Index < 300; ++Index)
	{
		Character->UpdateToolCamera(TestDeltaSeconds);
	}
	const float Inside = CameraBoom->TargetArmLength;
	TestEqual(TEXT("충분히 지나면 기본 거리 × 실내 배율"), Inside, TestCameraDistance * TestInteriorCameraMul, TestDistanceTolerance);
	Character->SetInteriorCamera(false);
	Character->UpdateToolCamera(TestDeltaSeconds);
	TestTrue(TEXT("나올 때도 한 번에 뛰지 않는다"),
		CameraBoom->TargetArmLength > Inside + TestDistanceTolerance && CameraBoom->TargetArmLength < TestCameraDistance);
	for (int32 Index = 0; Index < 300; ++Index)
	{
		Character->UpdateToolCamera(TestDeltaSeconds);
	}
	TestEqual(TEXT("나오면 원래 거리"), CameraBoom->TargetArmLength, TestCameraDistance, TestDistanceTolerance);
	AddInfo(FString::Printf(TEXT("[Interior] 바깥 %.0f · 실내 %.0fcm"), TestCameraDistance, Inside));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCameraFillTest, "Snarehaven.Camera.TopDown.CharacterFitsReferenceFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-01 레퍼런스 구도 — 거리·각·화각 **셋이 함께** 만들어 내는 결과, 곧 "캐릭터가 화면 높이의 몇 몫인가"를 잰다.
// 값 셋을 따로 견주는 검사(위 BinocularsWidenView)는 숫자가 바뀐 것만 잡고 **구도가 틀어진 것은 못 잡는다** —
// 화각을 세로로 고정하면서 거리를 그대로 두면 캐릭터가 점만 해지는데 세 숫자는 다 "설계서와 같다".
// 재는 법은 픽셀 자가 아니라 **엔진이 실제로 쓰는 투영**(`CalculateProjectionMatrixGivenViewRectangle`)이다.
bool FSnarehavenCameraFillTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Character =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	USpringArmComponent* const CameraBoom = Character != nullptr ? Character->GetCameraBoom() : nullptr;
	UCameraComponent* const Camera = Character != nullptr ? Character->GetTopDownCamera() : nullptr;
	UCapsuleComponent* const Capsule = Character != nullptr ? Character->GetCapsuleComponent() : nullptr;
	if (!TestNotNull(TEXT("캐릭터·카메라·캡슐"), Camera) || !TestNotNull(TEXT("CameraBoom"), CameraBoom)
		|| !TestNotNull(TEXT("캡슐"), Capsule))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 손으로 만든 테스트 월드에선 `BeginPlay` 가 안 돌아 팔이 엔진 기본(300cm)에 머문다 — 사람이 쓰는 길과
	// 같은 함수에 **게임 값 그대로**를 넣어 건다 (손으로 넣은 숫자로 재면 설계서와 어긋나도 통과한다).
	Character->ApplyCameraSettings(Character->GetCameraDistance(), Character->GetCameraPitchDegrees());
	CameraBoom->TickComponent(TestDeltaSeconds, LEVELTICK_All, nullptr);
	TestEqual(TEXT("게임 값 = DESIGN 3.3 거리"), CameraBoom->TargetArmLength, TestCameraDistance, TestDistanceTolerance);
	TestEqual(TEXT("게임 값 = DESIGN 3.3 각"), static_cast<double>(-CameraBoom->GetRelativeRotation().Pitch),
		static_cast<double>(TestCameraPitchDegrees), static_cast<double>(TestAngleTolerance));

	// 몸 높이는 캡슐(DESIGN 3.3 "기준 몸" 176cm)이다. 기본 겉모습은 맨머리라(M15-10) 모자가 더 얹히지 않는다.
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Center = Character->GetActorLocation();
	const FVector Foot = Center - FVector(0.f, 0.f, HalfHeight);
	const FVector Head = Center + FVector(0.f, 0.f, HalfHeight);

	// 화면 높이에서 캐릭터가 차지하는 몫. 엔진이 뷰포트에 쓰는 것과 같은 함수로 투영 행렬을 만든다.
	auto FillFraction = [&](int32 SizeX, int32 SizeY) -> float
	{
		FMinimalViewInfo ViewInfo;
		Camera->GetCameraView(0.f, ViewInfo);

		FSceneViewProjectionData Projection;
		Projection.ViewOrigin = ViewInfo.Location;
		// 월드 → 뷰 공간: 카메라 회전을 풀고 UE 의 축 순서(X 앞·Y 오른쪽·Z 위 → 오른쪽·위·앞)로 바꾼다.
		Projection.ViewRotationMatrix = FInverseRotationMatrix(ViewInfo.Rotation) * FMatrix(
			FPlane(0.f, 0.f, 1.f, 0.f),
			FPlane(1.f, 0.f, 0.f, 0.f),
			FPlane(0.f, 1.f, 0.f, 0.f),
			FPlane(0.f, 0.f, 0.f, 1.f));
		Projection.SetViewRectangle(FIntRect(0, 0, SizeX, SizeY));
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(ViewInfo,
			Camera->AspectRatioAxisConstraint, FIntRect(0, 0, SizeX, SizeY), Projection);

		const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();
		const FVector4 FootClip = ViewProjection.TransformFVector4(FVector4(Foot, 1.f));
		const FVector4 HeadClip = ViewProjection.TransformFVector4(FVector4(Head, 1.f));
		if (FootClip.W <= 0.f || HeadClip.W <= 0.f)
		{
			return 0.f;   // 카메라 뒤 — 몫을 잴 수 없다
		}
		// NDC 는 -1~1 이라 화면 높이가 2 다.
		return FMath::Abs(HeadClip.Y / HeadClip.W - FootClip.Y / FootClip.W) * 0.5f;
	};

	const float CaptureFill = FillFraction(TestCaptureSizeX, TestCaptureSizeY);
	const float PhoneFill = FillFraction(TestPhoneSizeX, TestPhoneSizeY);
	AddInfo(FString::Printf(TEXT("[Camera] 거리 %.0fcm · 각 %.0f도 · 화각 %.0f — 캐릭터가 화면 높이의 %.2f%% (4:3) · %.2f%% (19.5:9)"),
		CameraBoom->TargetArmLength, -CameraBoom->GetRelativeRotation().Pitch, Camera->FieldOfView,
		CaptureFill * 100.f, PhoneFill * 100.f));

	TestTrue(FString::Printf(TEXT("4:3 캡처에서 캐릭터 몫 %.2f%% 가 합격 폭 %.0f~%.0f%% 안 (DESIGN 3.3)"),
		CaptureFill * 100.f, TestFillFractionMin * 100.f, TestFillFractionMax * 100.f),
		CaptureFill >= TestFillFractionMin && CaptureFill <= TestFillFractionMax);
	// 화각 세로 고정(MaintainYFOV)의 뜻이 바로 이것 — 화면이 넓어져도 캐릭터 몫이 안 변한다.
	TestEqual(TEXT("폰(19.5:9)에서도 같은 몫 — 화각이 세로 고정이다"), PhoneFill, CaptureFill, 0.001f);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlaceholderBodyTest, "Snarehaven.Character.PlaceholderBody.IsVisibleAndCapsuleSized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M0-06 3층 캡처가 바닥만 찍힌 원인(보이는 몸이 없음)을 다시 못 만들게 막는 검사.
// 캡처는 매 바퀴 눈으로 보지 않으므로, "화면에 찍힐 메시가 붙어 있다"를 2층에서 단언한다.
bool FSnarehavenPlaceholderBodyTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Character = World->SpawnActor<ASnarehavenCharacter>(
		ASnarehavenCharacter::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("캐릭터 스폰"), Character))
	{
		DestroyTestWorld(World);
		return false;
	}

	UStaticMeshComponent* Body = Character->GetPlaceholderBody();
	if (!TestNotNull(TEXT("자리표시 몸통 컴포넌트가 있다"), Body))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 컴포넌트만 있고 메시가 비어 있으면 화면에는 아무것도 안 찍힌다 — 캡처가 비었던 상태와 같다.
	TestNotNull(TEXT("자리표시 몸통에 메시가 들어 있다"), ToRawPtr(Body->GetStaticMesh()));
	TestTrue(TEXT("자리표시 몸통이 보이는 상태다"), Body->IsVisible());
	TestTrue(TEXT("자리표시 몸통이 캐릭터에 붙어 있다"),
		Body->GetAttachParent() == Character->GetCapsuleComponent());
	// 충돌은 캡슐이 전담한다 — 자리표시가 물리에 끼면 진짜 메시로 바꿀 때 동작이 달라진다.
	TestTrue(TEXT("자리표시 몸통은 충돌하지 않는다"),
		Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	// 크기가 캡슐에서 계산됐는지 — 엔진 기본 실린더(반지름·반높이 50cm)를 캡슐 치수로 늘인 배율이어야 한다.
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Scale = Body->GetRelativeScale3D();
	TestEqual(TEXT("자리표시 몸통 반지름이 캡슐 반지름과 같다"),
		static_cast<double>(Scale.X * TestEngineCylinderHalfExtent),
		static_cast<double>(Capsule->GetUnscaledCapsuleRadius()), static_cast<double>(TestDistanceTolerance));
	TestEqual(TEXT("자리표시 몸통 높이가 캡슐 높이와 같다"),
		static_cast<double>(Scale.Z * TestEngineCylinderHalfExtent),
		static_cast<double>(Capsule->GetUnscaledCapsuleHalfHeight()), static_cast<double>(TestDistanceTolerance));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
