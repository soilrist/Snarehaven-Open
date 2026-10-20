#include "SnarehavenGameMode.h"
#include "SnareCheckSwitch.h"
#include "Combat/MeleeAttackComponent.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Animals/AnimalCharacter.h"
#include "Backend/SnareNickname.h"
#include "Gathering/GatherableActor.h"
#include "World/DangerZoneActor.h"
#include "World/SnareHarpoonCampActor.h"
#include "World/SnareLighthouseActor.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Online/SnareOnlineSubsystem.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/LobbyWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

const FName ASnarehavenGameMode::HuntLevelName(TEXT("L_Hunt"));

ASnarehavenGameMode::ASnarehavenGameMode()
{
	// ROADMAP M0-03: 플레이어 폰은 가상 조이스틱 입력을 받는 탑다운 캐릭터.
	DefaultPawnClass = ASnarehavenCharacter::StaticClass();

	// 디자인이 얹힌 WBP 가 있으면 그걸 쓴다. 없으면 CreateLobbyWidget 이 C++ 클래스로 떨어뜨린다
	// (engines/unreal.md 코드 규칙 4 — 생성자 하드 참조).
	static ConstructorHelpers::FClassFinder<ULobbyWidget> LobbyAsset(TEXT("/Game/Game/UI/WBP_Lobby"));
	if (LobbyAsset.Succeeded())
	{
		LobbyWidgetClass = LobbyAsset.Class;
	}
}

bool ASnarehavenGameMode::IsLobbyMap(const FString& CurrentMapName)
{
	// PIE 는 맵 이름 앞에 "UEDPIE_0_" 가 붙으므로 Contains 로 비교 (Equals 는 PIE 에서 항상 실패).
	return CurrentMapName.Contains(USnareOnlineSubsystem::LobbyLevelName.ToString());
}

FName ASnarehavenGameMode::GetNextLevelName(const FString& CurrentMapName)
{
	if (CurrentMapName.Contains(TEXT("L_Boot")))
	{
		// M3-03: 방이 해산되면 엔진이 게스트를 기본 맵(`L_Boot?closed`)에 먼저 내려놓는다 —
		// 우리가 로비를 열어 봐야 뒤에 오는 엔진 처리가 덮어쓴다(루프#41 3층 `join41.log:1714`).
		// 그래서 싸우지 않고 여기서 받아 로비까지 데려간다. 평소 부팅 흐름과 가는 곳이 같다.
		return USnareOnlineSubsystem::LobbyLevelName;
	}

	// 로비가 종착이다 (M3-04). 사냥 맵은 로비 버튼이 연다 — 여기서 자동으로 넘기면 로비가 안 보인다.
	// 그 밖의 맵(사냥·목장·테스트 월드·에디터 임시 맵)도 흐름 밖이라 아무 데도 보내지 않는다.
	return NAME_None;
}

bool ASnarehavenGameMode::ShouldShowHuntHud() const
{
	// 로비에서는 사냥 HUD 를 띄우지 않는다 — 로비 버튼 위에 꺼진 "포획" 버튼이 겹쳐 앉는다.
	// 전에는 로비를 한 프레임 만에 지나쳐서 겹치는 걸 볼 일이 없었다.
	const UWorld* World = GetWorld();
	return bShowHuntHud && !IsLobbyMap(World != nullptr ? World->GetMapName() : FString());
}

bool ASnarehavenGameMode::HideEngineDebugText()
{
	if (GIsEditor || GEngine == nullptr)
	{
		return false;
	}
	GEngine->bEnableOnScreenDebugMessages = false;
	return true;
}

void ASnarehavenGameMode::BeginPlay()
{
	Super::BeginPlay();
	HideEngineDebugText();

	const FString CurrentMap = GetWorld()->GetMapName();
	const FName NextLevelName = GetNextLevelName(CurrentMap);
	if (!NextLevelName.IsNone())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s -> %s 자동 진입"), *CurrentMap, *NextLevelName.ToString());
		UGameplayStatics::OpenLevel(this, NextLevelName);
		return;
	}

	// M3-04: 로비가 흐름의 종착이다 — 방 만들기·찾기·참가·혼자 사냥 버튼을 여기서 화면에 올린다.
	if (IsLobbyMap(CurrentMap))
	{
		CreateLobbyWidget();
		// 3층 검사용: 심사 점검표 ① 「설정에서 계정 삭제에 닿나」 를 캡처로 보려고 설정 화면을 바로 연다.
		if (IsValid(LobbyWidget) && SnareCheckSwitch::Param(TEXT("SnareLobbySettings")))
		{
			LobbyWidget->SetSettingsOpen(true);
		}
	}

	// 맵에 손으로 놓인 자리표시 동물(돼지·소·양)은 기본 플레이에 서지 않는다 (사용자 지시 2026-09-23 16:55 ②).
	RemovePlaceholderAnimals(GetWorld());

	// DESIGN 8.10: 맵의 장식 나무를 손도끼로 벨 수 있게 호스트가 그 자리에 복제 채집물을 세운다.
	AGatherableActor::SpawnAtDecorMeshes(GetWorld());

	// M12-07: 랜덤 월드 이벤트(한정시간 위험구역) 등장 주기를 건다. 이 맵에 속한 이벤트 행이 없으면 아무 일 없다.
	ADangerZoneActor::ScheduleWorldEvents(GetWorld());

	// 메인 스토리 ⑤: 등대 탑은 늘 서 있고, 25단계를 끝낸 계정에게만 밤에 불이 켜진다 (명세 story-lighthouse-light.md).
	if (!IsLobbyMap(CurrentMap))
	{
		ASnareLighthouseActor::SpawnAtSpot(GetWorld());
		ASnareHarpoonCampActor::SpawnAtSpot(GetWorld());  // 4장 작살단 캠프
	}

	// 3층 눈 검사용 (`-SnareCapture=`). 게스트 프로세스에는 게임모드가 없어서 클라이언트 쪽에서도
	// 같은 함수를 부른다 — `ASnarehavenCharacter::BeginPlay` 참고 (M9-06).
	ArmCaptureIfRequested(GetWorld());
}

int32 ASnarehavenGameMode::RemovePlaceholderAnimals(UWorld* World)
{
	if (!IsValid(World) || AAnimalCharacter::ShouldStandPlaceholderSpecies())
	{
		return 0;
	}

	// 표는 동물 기본값(CDO)에서 빌린다 — 표 경로는 `AAnimalCharacter` 생성자 한 곳에만 둔다.
	const UDataTable* AnimalTable = GetDefault<AAnimalCharacter>()->GetAnimalTable();

	TArray<AAnimalCharacter*> Removed;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		if (AAnimalCharacter::IsPlaceholderSpeciesRow(AnimalTable, It->GetAnimalRowName()))
		{
			Removed.Add(*It);
		}
	}

	// 훑는 중에 지우지 않는다 — 반복자가 보고 있는 배열을 건드리는 꼴이다.
	for (AAnimalCharacter* Animal : Removed)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("자리표시 동물을 뺀다 - %s (행 %s, -SnareStageAnimals 를 주면 선다)"),
			*Animal->GetName(), *Animal->GetAnimalRowName().ToString());
		Animal->Destroy();
	}

	if (Removed.Num() > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("자리표시 동물 %d마리를 기본 플레이에서 뺐다"), Removed.Num());
	}

	return Removed.Num();
}

FString ASnarehavenGameMode::MakeRoomPlayerName(const TArray<FString>& TakenNames)
{
	for (int32 Number = 1;; ++Number)
	{
		const FString Candidate = FString::Printf(TEXT("플레이어 %d"), Number);
		if (!TakenNames.Contains(Candidate))
		{
			return Candidate;
		}
	}
}

FString ASnarehavenGameMode::PickRoomPlayerName(const FString& Nickname, const TArray<FString>& TakenNames)
{
	const FString Trimmed = SnareNickname::Trim(Nickname);
	if (!Trimmed.IsEmpty() && SnareNickname::ProblemCode(Trimmed).IsEmpty() && !TakenNames.Contains(Trimmed))
	{
		return Trimmed;
	}
	return MakeRoomPlayerName(TakenNames);
}

TArray<FString> ASnarehavenGameMode::CollectTakenNames(const APlayerState* Except) const
{
	TArray<FString> Taken;
	if (const AGameStateBase* const Game = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* const Member : Game->PlayerArray)
		{
			if (Member != nullptr && Member != Except)
			{
				Taken.Add(Member->GetPlayerName());
			}
		}
	}
	return Taken;
}

bool ASnarehavenGameMode::ApplyPlayerNickname(AController* Other, const FString& Nickname)
{
	APlayerState* const State = Other != nullptr ? Other->PlayerState.Get() : nullptr;
	if (State == nullptr)
	{
		return false;
	}
	const TArray<FString> Taken = CollectTakenNames(State);
	const FString Trimmed = SnareNickname::Trim(Nickname);
	const FString Picked = PickRoomPlayerName(Trimmed, Taken);
	if (Picked != Trimmed)
	{
		// 지금 이름(「플레이어 N」)을 그대로 둔다 — 새 번호를 다시 뽑으면 이름표 색이 바뀐다.
		UE_LOG(LogSnarehaven, Log, TEXT("[Name] 닉네임을 받지 않음 — 이름표는 %s 그대로"), *State->GetPlayerName());
		return false;
	}
	if (State->GetPlayerName() != Picked)
	{
		// 우리 `ChangeName` 은 이미 이름이 있으면 바꾸기를 막으므로 엔진 쪽을 바로 부른다.
		AGameModeBase::ChangeName(Other, Picked, true);
		UE_LOG(LogSnarehaven, Log, TEXT("[Name] 이름표 = 닉네임 %s"), *Picked);
	}
	return true;
}

void ASnarehavenGameMode::ChangeName(AController* Other, const FString& NewName, bool bNameChange)
{
	APlayerState* const State = Other != nullptr ? Other->PlayerState.Get() : nullptr;
	if (State == nullptr || (bNameChange && !State->GetPlayerName().IsEmpty()))
	{
		return;
	}
	Super::ChangeName(Other, MakeRoomPlayerName(CollectTakenNames(State)), bNameChange);
}

FString ASnarehavenGameMode::ResolveCapturePath(const FString& RawPath)
{
	if (RawPath.IsEmpty())
	{
		return RawPath;
	}

	// 상대 경로는 Saved 밑으로 (M15-06). 엔진은 상대 경로를 **작업 폴더** 기준으로 푸는데, 그 자리가
	// 데스크톱에서는 엔진 바이너리 폴더(루프#166 이 여기서 캡처를 잃었다)고 iOS 에서는 쓰기 금지인
	// 앱 번들이다. `Saved` 는 두 곳 다 쓸 수 있고, iOS 에서는 앱 컨테이너의 Documents 로 옮겨 앉아
	// `tools/ios/pull_captures.py` 가 꺼낼 수 있는 유일한 자리가 된다.
	// 접는 것이 **붙이기보다 먼저다.** `Saved` 경로 자체가 `../../../../…` 로 시작하는데,
	// `CollapseRelativeDirectories` 는 루트 위로 올라가는 `..` 를 만나면 **통째로 포기하고 거짓을 돌려준다**
	// — 붙인 뒤에 접으면 사람이 준 `M15/../cam.png` 의 `..` 까지 그대로 남는다(루프#197 이 새 시험으로 잡았다).
	// 그 앞머리 `../` 는 iOS 플랫폼 파일 층이 Documents 로 옮겨 적을 때 쓰는 표식이라 지워서도 안 된다.
	FString Path = RawPath;
	FPaths::CollapseRelativeDirectories(Path);
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::Combine(FPaths::ProjectSavedDir(), Path);
	}
	return Path;
}

void ASnarehavenGameMode::ArmCaptureIfRequested(UWorld* World)
{
	if (!IsValid(World))
	{
		return;
	}

	// 부르는 쪽이 이미 **종착 맵**인 것을 확인하고 온다(게임모드는 자동 진입 뒤에, 게스트는
	// 호스트가 맞아 준 맵에서). L_Hunt 뿐 아니라 L_Ranch 처럼 명령줄로 바로 띄운 맵도 같은 방식으로
	// 찍는다 (루프#30, M2-04b).
	// Verification.md "캡처는 검사다": 씬이 자리 잡을 시간(3초)을 준 뒤 찍고, 저장 완료 델리게이트에서 프로세스를 종료해
	// 헤드리스 -game 실행이 캡처 확인 없이 계속 도는 걸 막는다.
	FString RawCapturePath;
	if (SnareCheckSwitch::Value(TEXT("SnareCapture="), RawCapturePath))
	{
		const FString CapturePath = ResolveCapturePath(RawCapturePath);

		// 맵 이름을 람다에 담지 않는다 — 이 타이머는 월드가 아니라 **게임 인스턴스**의 것이라
		// 맵을 갈아타도 살아남는다(M3-05 로 호스트가 로비에서 사냥 맵으로 떠나면서 드러났다:
		// `logs/game_host_loop44b.log:1671` 이 L_Hunt 화면을 "L_Title 캡처" 라고 적었다).
		// 어느 맵을 찍었는지는 바로 앞의 `LoadMap Load map complete` 줄이 말해 준다.
		// 기다리는 시간은 검사마다 다르다 — 로비는 3초면 자리를 잡지만, 죽음 자세(M1-09)를 찍으려면
		// `-SnareAutoAttack` 이 동물을 다 때릴 때까지 기다려야 한다. `-SnareRoom=`·`-SnareCapture=` 와
		// 같은 **검사 전용 스위치**고, 기본값은 지금까지 쓰던 3초 그대로다(설계 숫자가 아니다).
		float CaptureDelaySeconds = 3.0f;
		SnareCheckSwitch::Value(TEXT("SnareCaptureDelay="), CaptureDelaySeconds);

		// `-SnareCaptureBurst=8 -SnareCaptureInterval=0.0833` 이면 첫 장 뒤 그 간격으로 이어 찍어 `<이름>_00.png`…
		// 를 남기고 마지막 장 뒤에 닫는다 — 클립 한 주기의 칸 시트를 한 번 띄워 얻는다(루프#401, INBOX 12:40
		// 「대기·걷기·달리기 각 8칸」). 장 사이는 간격 + 저장 한 프레임이라 실제 시각을 줄마다 찍는다.
		// 스위치가 없으면 한 장·원래 파일 이름 그대로다(검사 전용, 설계 숫자 아님).
		struct FCaptureBurst
		{
			FString BasePath;
			int32 Count = 1;
			int32 Taken = 0;
			float IntervalSeconds = 0.f;
			TWeakObjectPtr<UWorld> World;
		};
		const TSharedRef<FCaptureBurst> Burst = MakeShared<FCaptureBurst>();
		Burst->BasePath = CapturePath;
		Burst->World = World;
		SnareCheckSwitch::Value(TEXT("SnareCaptureBurst="), Burst->Count);
		SnareCheckSwitch::Value(TEXT("SnareCaptureInterval="), Burst->IntervalSeconds);
		Burst->Count = FMath::Clamp(Burst->Count, 1, 64);
		Burst->IntervalSeconds = FMath::Max(Burst->IntervalSeconds, 0.f);

		const TFunction<void()> RequestShot = [Burst]()
		{
			FString ShotPath = Burst->BasePath;
			if (Burst->Count > 1)
			{
				ShotPath = FPaths::Combine(FPaths::GetPath(Burst->BasePath),
					FString::Printf(TEXT("%s_%02d.%s"), *FPaths::GetBaseFilename(Burst->BasePath), Burst->Taken, *FPaths::GetExtension(Burst->BasePath)));
			}
			const double WorldSeconds = Burst->World.IsValid() ? Burst->World->GetTimeSeconds() : -1.0;
			UE_LOG(LogSnarehaven, Log, TEXT("검사 캡처 요청: %s (%d/%d · 월드 %.3f초)"), *ShotPath, Burst->Taken + 1, Burst->Count, WorldSeconds);
			FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
		};

		// `-SnareCaptureOnShot=<n> -SnareCaptureShotLag=<초>` — n 번째 쏘는 그림이 뜬 뒤 lag 초에 찍는다(타이머 대신).
		// 총구 불꽃은 0.1초뿐이라 고정 지연으로는 맞추기 어렵다(루프#1134). 스위치가 없으면 지금까지처럼 지연 타이머.
		int32 CaptureOnShot = 0;
		SnareCheckSwitch::Value(TEXT("SnareCaptureOnShot="), CaptureOnShot);
		float CaptureShotLag = 0.03f;
		SnareCheckSwitch::Value(TEXT("SnareCaptureShotLag="), CaptureShotLag);

		FTimerHandle CaptureTimerHandle;
		const TWeakObjectPtr<UWorld> ShotWorld = World;
		const TFunction<void(const TFunction<void()>&)> Arm = [CaptureOnShot, CaptureShotLag, ShotWorld, CaptureDelaySeconds, CaptureTimerHandle](const TFunction<void()>& Fire) mutable
		{
			if (CaptureOnShot > 0)
			{
				const TSharedRef<bool> bFired = MakeShared<bool>(false);
				UMeleeAttackComponent::OnShotVisual().AddLambda([CaptureOnShot, CaptureShotLag, ShotWorld, bFired, Fire](int32 ShotCount)
				{
					if (*bFired || ShotCount < CaptureOnShot || !ShotWorld.IsValid())
					{
						return;
					}
					*bFired = true;
					FTimerHandle LagHandle;
					ShotWorld->GetTimerManager().SetTimer(LagHandle, FTimerDelegate::CreateLambda(Fire), FMath::Max(CaptureShotLag, KINDA_SMALL_NUMBER), false);
				});
			}
			else if (ShotWorld.IsValid())
			{
				ShotWorld->GetTimerManager().SetTimer(CaptureTimerHandle, FTimerDelegate::CreateLambda(Fire), CaptureDelaySeconds, false);
			}
		};
		Arm([Burst, RequestShot]()
		{
			FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Burst, RequestShot]()
			{
				++Burst->Taken;
				if (Burst->Taken < Burst->Count && Burst->World.IsValid())
				{
					FTimerHandle NextHandle;
					Burst->World->GetTimerManager().SetTimer(NextHandle, FTimerDelegate::CreateLambda(RequestShot),
						FMath::Max(Burst->IntervalSeconds, KINDA_SMALL_NUMBER), false);
					return;
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사 캡처 저장 완료, 종료"));
				FPlatformMisc::RequestExit(false);
			});
			// 두 번째 인자 bShowUI = true — M1-08 사냥 HUD 가 찍혀야 3층 눈 검사가 된다.
			// false 면 UMG 가 빠진 3D 화면만 저장된다 (M0-06 때는 UI 가 없어 차이가 없었다).
			RequestShot();
		});
	}
}

void ASnarehavenGameMode::CreateLobbyWidget()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Lobby] 로컬 플레이어가 없어 로비 UI 를 못 올린다"));
		return;
	}

	// WBP 가 아직 없어도 로비는 떠야 3층 캡처를 찍는다 (자리표시 트리는 위젯이 스스로 만든다).
	const TSubclassOf<ULobbyWidget> WidgetClass =
		LobbyWidgetClass != nullptr ? LobbyWidgetClass : TSubclassOf<ULobbyWidget>(ULobbyWidget::StaticClass());

	LobbyWidget = CreateWidget<ULobbyWidget>(PlayerController, WidgetClass);
	if (!IsValid(LobbyWidget))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Lobby] 로비 위젯을 만들지 못했다"));
		return;
	}

	LobbyWidget->AddToViewport();
	// 버튼을 손가락·마우스로 누를 수 있어야 한다 — 로비에는 폰을 세우지 않으니(`PlayerCanRestart`) 입력을 UI 에 준다.
	PlayerController->SetShowMouseCursor(true);
	// 가상 조이스틱을 내린다(`DefaultInput.ini` 의 `bAlwaysShowTouchInterface=True` 가 올려 둔 것).
	// 로비에는 움직일 폰이 없어 조이스틱 두 개가 버튼 위에 얹힌 채 아무 일도 하지 않는다 —
	// 사냥 맵으로 가면 그 맵의 플레이어 컨트롤러가 다시 기본 인터페이스를 올린다.
	PlayerController->ActivateTouchInterface(nullptr);
	UE_LOG(LogSnarehaven, Log, TEXT("[Lobby] 로비 UI 표시: %s"), *WidgetClass->GetName());
}

bool ASnarehavenGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const UWorld* const World = GetWorld();
	return !IsLobbyMap(World != nullptr ? World->GetMapName() : FString()) && Super::PlayerCanRestart_Implementation(Player);
}

void ASnarehavenGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// M3-01c1: 호스트(리슨 서버)에서만 돈다. 로컬 플레이어와 원격 게스트를 갈라 적는다 —
	// 갈라 두지 않으면 호스트 자신의 로그인 한 줄과 게스트 접속 한 줄이 똑같이 보여 증거가 못 된다.
	UE_LOG(LogSnarehaven, Log, TEXT("[Net] %s 접속 — PlayerController %s, 총 %d명"),
		NewPlayer != nullptr && NewPlayer->IsLocalController() ? TEXT("호스트") : TEXT("게스트"),
		*GetNameSafe(NewPlayer), GetNumPlayers());
}
