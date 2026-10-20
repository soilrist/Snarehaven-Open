#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SubclassOf.h"
#include "SnarehavenGameMode.generated.h"

class ULobbyWidget;

// ROADMAP M0-02: L_Boot -> L_Title -> L_Hunt 자동 진입. Level Blueprint 대신
// GameMode 에 둔 이유 — 바이너리 씬 에셋(Level Blueprint 그래프) 편집 없이
// C++/커밋으로 리뷰 가능하게 하려고 (docs/engines/unreal.md "언리얼 파일 다루기").
UCLASS()
class SNAREHAVEN_API ASnarehavenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASnarehavenGameMode();

	/**
	 * 현재 맵 이름에서 다음에 열 맵을 고른다 (L_Boot -> L_Title, **로비가 종착**).
	 * BeginPlay 에서 떼어 낸 이유 — 2층 검사(`Snarehaven.Boot.ReachesLobby`)가 맵을 실제로 띄우지 않고
	 * 흐름 규칙만 검사할 수 있게 (docs/checks/unreal.md 2층).
	 * M3-04 전에는 로비에서 사냥 맵까지 자동으로 갔지만, 로비 화면(`WBP_Lobby`)이 생긴 뒤로는
	 * **여기서 멈춘다** — 4~8인 PVE 는 방을 고른 뒤에 사냥에 들어가고(DESIGN 3.4), 자동으로 지나쳐 버리면
	 * 로비가 한 프레임도 보이지 않는다. 사냥 맵은 이제 로비의 버튼만 연다.
	 * 이 덕분에 M3-03 의 `bReturnedFromRoom` 가림막도 필요 없어졌다 — 아무도 되밀지 않으니까.
	 * @param CurrentMapName `UWorld::GetMapName()` 값. PIE 는 "UEDPIE_0_" 접두어가 붙으므로 Contains 로 비교한다.
	 * @return 다음에 열 맵 이름. 더 넘어갈 곳이 없으면 `NAME_None`.
	 */
	static FName GetNextLevelName(const FString& CurrentMapName);

	/**
	 * 이 맵이 로비인가 (`USnareOnlineSubsystem::LobbyLevelName`). 로비에서만 로비 UI 가 뜨고
	 * 사냥 HUD 는 안 뜬다. PIE 접두어 때문에 Contains 로 본다 — `GetNextLevelName` 과 같은 규칙.
	 */
	static bool IsLobbyMap(const FString& CurrentMapName);

	/** 로비의 "혼자 사냥" 버튼이 여는 맵. 맵 이름을 두 군데 적지 않으려고 여기 하나만 둔다. */
	static const FName HuntLevelName;

	/**
	 * 이 레벨에서 사냥 HUD(`WBP_HUD_Hunt`)를 띄울지. 목장엔 사냥할 동물이 없어 포획 버튼이 뜨면 안 된다
	 * (감독 지시 2026-09-19 · DESIGN 3.2 "슬롯 상호작용"). **맵 이름 문자열로 가르지 않는다** —
	 * 목장 맵이 여러 개가 돼도 조건문이 늘지 않게, "이 레벨은 어떤 규칙으로 노는가" 를 아는
	 * 게임모드가 답한다. 목장 레벨은 `ARanchGameMode` 로 오버라이드돼 있다.
	 * @return 사냥 HUD 를 띄울지
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool ShouldShowHuntHud() const;

	/**
	 * 이 레벨에서 목장 HUD(`WBP_HUD_Ranch` — 칸별 Ready 아이콘·미수확 개수)를 띄울지 (ROADMAP M2-06a).
	 * 사냥 HUD 와 **기본값이 반대다**: 목장 칸이 없는 맵에 목장 HUD 가 뜨면 빈 표만 남으므로,
	 * 게임모드가 "여긴 목장이다" 라고 말할 때만 띄운다. 맵 이름으로 가르지 않는 이유는 위와 같다.
	 * @return 목장 HUD 를 띄울지
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool ShouldShowRanchHud() const { return bShowRanchHud; }

	/**
	 * 3층 눈 검사용 — 명령줄에 `-SnareCapture=<파일>` 이 있으면 몇 초 뒤 화면을 찍고 프로세스를 닫는다.
	 * 스위치가 없으면 아무 일도 안 한다 (일반 플레이에는 영향 없음).
	 *
	 * **게임모드 밖에도 있는 이유**(M9-06): 게임모드는 호스트에만 산다 — 게스트 프로세스에서는
	 * `BeginPlay` 가 아예 안 돌아 `-SnareCapture=` 가 조용히 무시됐다. 두 프로세스 3층에서
	 * **게스트 화면**을 찍으려면 클라이언트 쪽에서도 같은 길을 걸 수 있어야 한다
	 * (`ASnarehavenCharacter::BeginPlay` 가 `NM_Client` 일 때만 부른다 — 한 프로세스에서 둘 다 돌지 않는다).
	 * @param World 타이머를 걸 월드
	 */
	static void ArmCaptureIfRequested(UWorld* World);

	/**
	 * 엔진이 화면 왼쪽 위에 찍는 영문 디버그 글자(`On mobile the SkyAtmosphere …` 같은 개발 빌드 경고)를 끈다.
	 * 폰 화면에 엔진 글자가 나오면 안 된다 (INBOX 2026-09-29). 에디터(PIE·자동화)에서는 그대로 둔다 —
	 * 개발자가 화면 경고를 봐야 하는 곳이다. 게임모드(호스트)·내 폰(손님 포함)이 뜰 때 부른다.
	 * @return 끈 채로 두면 true, 에디터라 그대로 두었으면 false
	 */
	static bool HideEngineDebugText();

	/**
	 * `-SnareCapture=` 가 받은 글자를 **파일이 실제로 떨어질 자리**로 바꾼다 (M15-06).
	 * 절대 경로는 그대로 두고, 상대 경로만 프로젝트 `Saved/` 밑으로 옮긴다 — 엔진은 상대 경로를
	 * 작업 폴더 기준으로 푸는데 그 자리가 데스크톱에서는 엔진 바이너리 폴더고 iOS 에서는
	 * **쓰기 금지인 앱 번들**이라, 기기에서 찍은 3층 캡처가 조용히 사라진다.
	 * `Saved` 는 iOS 에서 앱 컨테이너의 Documents 로 옮겨 앉아 `tools/ios/pull_captures.py` 가 꺼내 온다.
	 * @param RawPath 명령줄이 준 글자 (빈 글자면 빈 글자 그대로 — 스위치가 없는 것과 같다)
	 * @return 파일이 떨어질 자리
	 */
	static FString ResolveCapturePath(const FString& RawPath);

	/**
	 * **레벨에 미리 놓인 자리표시 동물(돼지·소·양)을 기본 플레이에서 치운다** (사용자 지시 2026-09-23 16:55 ②).
	 * 어느 개체가 자리표시인지는 이름이 아니라 그 종의 행 칸(`FAnimalRow::bPlaceholderSpecies`)이 말한다.
	 * 검사 스위치 `-SnareStageAnimals` 가 붙어 있으면 한 마리도 안 치운다 — 그 셋을 쓰는 검사가 그대로 돈다.
	 *
	 * 동물을 세우는 쪽(`AAnimalSpreadActor`)은 애초에 자리표시를 안 고르므로, 여기서 치우는 것은
	 * **맵에 손으로 놓인 것들**뿐이다 (`setup_hunt_stage.py` 의 `HuntStagePig`·`Cow`·`Sheep`).
	 * @param World 훑을 월드
	 * @return 치운 마리 수
	 */
	static int32 RemovePlaceholderAnimals(UWorld* World);

	/**
	 * 방 안 이름 — "플레이어 N", N 은 다른 플레이어가 안 쓰는 가장 작은 번호 (M21-01c · DESIGN 2.2 「이름표」).
	 * 접속 주소의 `?Name=` 은 엔진 기본 온라인 모듈이 **기기 호스트명**으로 채워(`gdmui-MacBo…`, `captures/238`)
	 * 개인정보가 머리 위에 뜨고, 폰은 "홍길동의 iPhone" 같은 기기 이름이 남에게 보인다 — 그래서 받지 않는다.
	 * @param TakenNames 같은 방 다른 플레이어들의 지금 이름
	 * @return 새 이름
	 */
	static FString MakeRoomPlayerName(const TArray<FString>& TakenNames);

	/**
	 * 닉네임을 방 안 이름으로 쓸 수 있으면 그대로, 아니면 「플레이어 N」 (DESIGN 2.2 「이름표」 닉네임 줄 · 닉네임 c).
	 * 앞뒤 공백을 자른 뒤 `SnareNickname::ProblemCode` 가 빈 문자열이고 같은 방에 같은 이름이 없을 때만 받는다.
	 * 닉네임은 띄어쓰기를 못 넣어 「플레이어 N」 과 겹칠 일이 없다.
	 * @param Nickname 플레이어가 알려 온 PlayFab 표시 이름 (빈 문자열이면 아직 못 받은 것)
	 * @param TakenNames 같은 방 다른 플레이어들의 지금 이름
	 * @return 새 이름
	 */
	static FString PickRoomPlayerName(const FString& Nickname, const TArray<FString>& TakenNames);

	/**
	 * 호스트에서만 — 플레이어가 PlayFab 이 받아 준 닉네임을 알려 왔을 때 이름표 이름을 바꾼다
	 * (`ASnarehavenCharacter::Server_ReportNickname`). 규칙에 안 맞거나 겹치면 「플레이어 N」 을 그대로 둔다.
	 * @param Other 알려 온 플레이어의 컨트롤러
	 * @param Nickname 알려 온 이름
	 * @return 이름표 이름을 닉네임으로 바꿨으면 true
	 */
	bool ApplyPlayerNickname(AController* Other, const FString& Nickname);

	/** 이름은 서버가 `MakeRoomPlayerName` 으로만 준다. 이미 이름이 있으면 바꾸기 요청(`ServerChangeName`)도 받지 않는다. */
	virtual void ChangeName(AController* Other, const FString& NewName, bool bNameChange) override;

protected:
	/** 사냥 맵의 기본값은 참. 목장용 서브클래스가 생성자에서 거짓으로 바꾼다 (인스턴스마다 다를 값이 아니다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	bool bShowHuntHud = true;

	/** 목장 HUD 는 기본이 거짓. `ARanchGameMode` 가 생성자에서 참으로 바꾼다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	bool bShowRanchHud = false;

	virtual void BeginPlay() override;

	/**
	 * 플레이어가 이 서버에 붙었을 때 (ROADMAP M3-01c1). 로그 한 줄이 이 조각의 합격 증거다 —
	 * 헤드리스 두 프로세스가 붙었는지 볼 화면이 없으므로 접속 자체를 `LogSnarehaven` 에 남긴다
	 * (명세 `docs/specs/net-eos-session.md` 5장: "호스트 로그에 게스트 PlayerController 생성 줄").
	 * 엔진의 `LogNet` 줄과 달리 **몇 명이 됐는지**까지 한 줄에 담아 리플리케이션 조각(M3-02)이 이어 읽을 수 있게 한다.
	 */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/**
	 * 로비(타이틀) 맵에서는 폰을 세우지 않는다 (M15-06). 폰이 서면 그 컴포넌트들이 제작·배낭 버튼과
	 * 조작 매핑을 타이틀 위에 올려 디버그 메뉴처럼 보였다 — 레퍼런스 타이틀은 시작 버튼뿐이다.
	 */
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	/**
	 * 화면에 올릴 로비 위젯 클래스. 기본값은 `WBP_Lobby` 이고, 그 에셋이 없으면 C++ 클래스
	 * (`ULobbyWidget`)를 그대로 쓴다 — 디자인이 없어도 로비는 떠야 한다 (사냥 HUD 와 같은 규칙).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<ULobbyWidget> LobbyWidgetClass;

private:
	/** 같은 방에서 `Except` 를 뺀 플레이어들의 지금 이름. */
	TArray<FString> CollectTakenNames(const APlayerState* Except) const;

	/** 로비 맵일 때만 부른다. 방 만들기·찾기·참가 버튼을 화면에 올린다 (M3-04). */
	void CreateLobbyWidget();

	UPROPERTY()
	TObjectPtr<ULobbyWidget> LobbyWidget;
};
