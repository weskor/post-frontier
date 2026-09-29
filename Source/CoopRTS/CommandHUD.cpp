#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Headquarters.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	constexpr float DoctrineTop = 150.f;
	constexpr float DoctrineCardTop = DoctrineTop + 66.f;
	constexpr float DoctrineCardStep = 104.f;
	constexpr float DoctrineCardHeight = 96.f;

	struct FDoctrinePanel
	{
		float X;
		float Width;
		float Height;
	};

	FDoctrinePanel DoctrinePanel(float ViewportWidth, bool bCompact)
	{
		const float X = FMath::Max(904.f, ViewportWidth - 560.f);
		return {X, FMath::Max(0.f, FMath::Min(548.f, ViewportWidth - X - 12.f)),
			bCompact ? 158.f : 435.f};
	}

	bool Inside(float X, float Y, float Width, float Height, const FVector2D& Position)
	{
		return Position.X >= X && Position.X < X + Width
			&& Position.Y >= Y && Position.Y < Y + Height;
	}

	const TCHAR* RoleName(EUnitRole Role)
	{
		switch (Role)
		{
		case EUnitRole::Frontline: return TEXT("Frontline");
		case EUnitRole::Ranged: return TEXT("Ranged");
		case EUnitRole::Siege: return TEXT("Siege");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* DoctrineName(EArmyDoctrine Doctrine)
	{
		switch (Doctrine)
		{
		case EArmyDoctrine::SiegeOptics: return TEXT("Siege Optics");
		case EArmyDoctrine::FieldRepairs: return TEXT("Field Repairs");
		case EArmyDoctrine::EntrenchedFrontline: return TEXT("Entrenched Frontline");
		default: return TEXT("Undecided");
		}
	}
}

bool ACommandHUD::IsDoctrinePanelPoint(const FVector2D& Position) const
{
	// AHUD::Canvas is reset after PostRender; hit testing happens on an input tick.
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Controller) return false;
	int32 ViewportWidth, ViewportHeight;
	Controller->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0) return false;
	const ACommandPlayerState* PlayerState = GetOwningPlayerController()
		? GetOwningPlayerController()->GetPlayerState<ACommandPlayerState>() : nullptr;
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const bool bCompact = !PlayerState || PlayerState->Doctrine != EArmyDoctrine::None
		|| (State && State->MatchResult != EMatchResult::Ongoing);
	const FDoctrinePanel Panel = DoctrinePanel(ViewportWidth, bCompact);
	return Inside(Panel.X, DoctrineTop, Panel.Width, Panel.Height, Position);
}

EArmyDoctrine ACommandHUD::GetDoctrineAtScreenPosition(const FVector2D& Position) const
{
	if (!IsDoctrinePanelPoint(Position)) return EArmyDoctrine::None;
	const ACommandPlayerState* PlayerState = GetOwningPlayerController()
		? GetOwningPlayerController()->GetPlayerState<ACommandPlayerState>() : nullptr;
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!PlayerState || !State || State->MatchResult != EMatchResult::Ongoing
		|| PlayerState->Doctrine != EArmyDoctrine::None) return EArmyDoctrine::None;

	int32 ViewportWidth, ViewportHeight;
	GetOwningPlayerController()->GetViewportSize(ViewportWidth, ViewportHeight);
	const FDoctrinePanel Panel = DoctrinePanel(ViewportWidth, false);
	const int32 Card = FMath::FloorToInt((Position.Y - DoctrineCardTop) / DoctrineCardStep);
	if (Card < 0 || Card > 2 || !Inside(Panel.X + 8.f,
		DoctrineCardTop + Card * DoctrineCardStep, Panel.Width - 16.f, DoctrineCardHeight, Position))
		return EArmyDoctrine::None;
	switch (Card)
	{
	case 0: return EArmyDoctrine::SiegeOptics;
	case 1: return EArmyDoctrine::FieldRepairs;
	case 2: return EArmyDoctrine::EntrenchedFrontline;
	default: return EArmyDoctrine::None;
	}
}

void ACommandHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine) return;
	DrawRect(FLinearColor(0.015f, 0.025f, 0.04f, 0.88f), 12, 12, 880, 465);
	UFont* Font = GEngine->GetSmallFont();
	DrawText(TEXT("CO-OP RTS | ECONOMY & ORDERS"), FLinearColor::White, 24, 22, Font, 1.3f);
	DrawText(TEXT("WASD pan | Middle-drag | Wheel zoom | 1 / 2 select army | Space focus"), FLinearColor::White, 24, 49, Font);
	DrawText(TEXT("Left-click unit: select | Right-click: move | Q: attack unit / HQ / ground"), FLinearColor::White, 24, 69, Font);
	DrawText(TEXT("H: hold | R: retreat | N: buy casualty replacements / rebuild at base"), FLinearColor(0.7f, 0.75f, 0.8f), 24, 89, Font);
	if (const auto* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController()))
	{
		const ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		const ACommandGameState* Territory = GetWorld()->GetGameState<ACommandGameState>();
		if (Wallet && Territory)
			DrawText(FString::Printf(TEXT("YOUR resources: %d | Income: +%d/s base +%d/s territory (%d/2 sites) = +%d/s"),
				Wallet->Resources, ACommandGameState::BaselineIncomePerSecond,
				ACommandGameState::ResourceIncomePerSecond * Territory->ControlledResourceSites,
				Territory->ControlledResourceSites, Wallet->GetIncomePerSecond()),
				FLinearColor(0.5f, 1.f, 0.65f), 24, 113, Font);
		else DrawText(TEXT("Wallet / territory syncing..."), FLinearColor::Yellow, 24, 113, Font);
		if (const AArmyGroup* Army = Controller->GetSelectedArmy();
			IsValid(Army) && Wallet && Army->OwningPlayerState == Wallet && Army->TeamIndex == 0)
		{
			const int32 Cost = Army->GetReinforcementCost();
			FVector Source;
			bool bBase = false;
			const bool bHasSource = Army->GetReinforcementSource(Source, bBase);
			const bool bEmpty = Army->Units.IsEmpty();
			const FString Status = Army->GetReinforcementStatus();
			DrawText(FString::Printf(TEXT("N quote: %d | %s | Source: %s | %s"),
				Cost, bEmpty ? TEXT("EMPTY: rebuild at base") : TEXT("Replace missing roles"),
				bHasSource ? (bBase ? TEXT("base") : TEXT("controlled forward site")) : TEXT("none in range"),
				*Status),
				Status == TEXT("READY") ? FLinearColor(0.5f, 1.f, 0.65f) : FLinearColor::Yellow,
				24, 135, Font);
			const bool bAttack = Army->Order == EArmyOrder::Attack;
			const AActor* OrderedTarget = IsValid(Army->AttackTarget) ? Army->AttackTarget.Get() : nullptr;
			const AArmyUnit* TargetUnit = Cast<AArmyUnit>(OrderedTarget);
			const AHeadquarters* TargetHQ = Cast<AHeadquarters>(OrderedTarget);
			const bool bTargetAlive = (TargetUnit && TargetUnit->IsAlive()) || (TargetHQ && TargetHQ->IsAlive());
			const TCHAR* Order = bAttack ? (bTargetAlive ? TEXT("ATTACK TARGET") : TEXT("ATTACK AREA"))
				: Army->Order == EArmyOrder::Move ? TEXT("MOVE")
				: Army->Order == EArmyOrder::Retreat ? TEXT("RETREAT") : TEXT("HOLD");
			const FLinearColor OrderColor = bAttack ? FLinearColor::Red
				: Army->Order == EArmyOrder::Retreat ? FLinearColor(1.f, .5f, .1f)
				: Army->Order == EArmyOrder::Hold ? FLinearColor(0.f, 1.f, 1.f) : FLinearColor::Green;
			DrawText(FString::Printf(TEXT("YOUR army %d | %d units | %s #%u | Destination %.0f, %.0f"),
				Army->ArmyIndex + 1, Army->Units.Num(), Order, Army->OrderSerial, Army->Destination.X, Army->Destination.Y),
				OrderColor, 24, 159, Font);
			if (bAttack)
			{
				const FString Pursuit = TargetUnit && TargetUnit->IsAlive()
					? FString::Printf(TEXT("Pursuit: limited to %.0f from destination | Target: %s HP %d (%.0f away)"),
						Army->PursuitRadius, RoleName(TargetUnit->UnitRole), TargetUnit->Health,
						FVector::Dist2D(TargetUnit->GetActorLocation(), Army->Destination))
					: TargetHQ && TargetHQ->IsAlive()
						? FString::Printf(TEXT("Pursuit: limited to %.0f from destination | Enemy HQ HP %d/%d"),
							Army->PursuitRadius, TargetHQ->Health, TargetHQ->MaxHealth())
						: FString::Printf(TEXT("Pursuit: limited to %.0f from destination | Attack location"), Army->PursuitRadius);
				DrawText(Pursuit, FLinearColor(1.f, .55f, .45f), 24, 180, Font);
			}
			else
			{
				DrawText(TEXT("Pursuit: off | Move follows route; Hold stays nearby; Retreat disengages"),
					FLinearColor(0.7f, 0.75f, 0.8f), 24, 180, Font);
			}
			int32 Row = 0;
			for (const AArmyUnit* Unit : Army->Units)
			{
				if (!IsValid(Unit) || !Unit->IsAlive()) continue;
				const AArmyUnit* UnitTarget = IsValid(Unit->Target) ? Cast<AArmyUnit>(Unit->Target.Get()) : nullptr;
				const AHeadquarters* UnitTargetHQ = IsValid(Unit->Target) ? Cast<AHeadquarters>(Unit->Target.Get()) : nullptr;
				const FString TargetText = UnitTarget && UnitTarget->IsAlive()
					? FString::Printf(TEXT("%s HP %d"), RoleName(UnitTarget->UnitRole), UnitTarget->Health)
					: UnitTargetHQ && UnitTargetHQ->IsAlive()
						? FString::Printf(TEXT("HQ HP %d/%d"), UnitTargetHQ->Health, UnitTargetHQ->MaxHealth()) : TEXT("none");
				++Row;
				DrawText(FString::Printf(TEXT("%d. %s | HP %d/%d | Target: %s"),
					Row, RoleName(Unit->UnitRole), Unit->Health, Unit->MaxHealth(), *TargetText),
					(UnitTarget && UnitTarget->IsAlive()) || (UnitTargetHQ && UnitTargetHQ->IsAlive())
						? FLinearColor(1.f, .75f, .55f) : FLinearColor::White, 24, 205 + (Row - 1) * 21, Font);
			}
		}
		else DrawText(TEXT("No army selected — click a unit or press 1 / 2 (empty armies remain selectable)."),
			FLinearColor::Yellow, 24, 135, Font);
		DrawText(TEXT("TERRITORY | shared ownership, separate player wallets"),
			FLinearColor(0.7f, 0.85f, 1.f), 24, 336, Font);
		int32 SiteRow = 0;
		if (Territory)
		{
			for (const ACapturePoint* Site : Territory->CaptureSites)
			{
				if (!IsValid(Site)) continue;
				const TCHAR* SiteName = Site->SiteKind == ECaptureSiteKind::Resource ? TEXT("Resource") : TEXT("Forward reinforcement");
				const TCHAR* Owner = Site->ControllingTeam == 0 ? TEXT("Friendly")
					: Site->ControllingTeam == 5 ? TEXT("Enemy") : TEXT("Neutral");
				const TCHAR* Capturing = Site->CaptureProgress > 0.f ? TEXT("Friendly")
					: Site->CaptureProgress < 0.f ? TEXT("Enemy") : TEXT("Neutral");
				DrawText(FString::Printf(TEXT("%s %d | Owner: %s | Capture: %s %d%%"),
					SiteName, Site->SiteIndex + 1, Owner, Capturing,
					FMath::RoundToInt(FMath::Abs(Site->CaptureProgress) * 100.f)),
					Site->ControllingTeam == 0 ? FLinearColor(0.5f, 1.f, 0.65f) : FLinearColor::White,
					24, 357 + SiteRow * 22, Font);
				if (++SiteRow == 3) break;
			}
		}
		if (SiteRow == 0) DrawText(TEXT("Capture sites syncing..."), FLinearColor::Yellow, 24, 357, Font);
		DrawText(Controller->GetOrderFeedback(), FLinearColor::Yellow, 24, 447, Font);
		DrawRect(FLinearColor(0.015f, 0.025f, 0.04f, 0.88f), 12, 485, 880, 104);
		const AHeadquarters* FriendlyHQ = Territory && IsValid(Territory->FriendlyHeadquarters)
			? Territory->FriendlyHeadquarters.Get() : nullptr;
		const AHeadquarters* EnemyHQ = Territory && IsValid(Territory->EnemyHeadquarters)
			? Territory->EnemyHeadquarters.Get() : nullptr;
		const FString FriendlyHealth = FriendlyHQ
			? FString::Printf(TEXT("%d/%d"), FriendlyHQ->Health, FriendlyHQ->MaxHealth()) : TEXT("syncing");
		const FString EnemyHealth = EnemyHQ
			? FString::Printf(TEXT("%d/%d"), EnemyHQ->Health, EnemyHQ->MaxHealth()) : TEXT("syncing");
		DrawText(FString::Printf(TEXT("HEADQUARTERS | Friendly HP %s | Enemy HP %s"), *FriendlyHealth, *EnemyHealth),
			FLinearColor(0.85f, 0.9f, 1.f), 24, 495, Font);
		DrawText(FString::Printf(TEXT("Enemy plan: %s"), Territory ? *Territory->EnemyPlan.Left(95) : TEXT("syncing...")),
			FLinearColor(1.f, .7f, .5f), 24, 519, Font);
		DrawText(FString::Printf(TEXT("Why: %s"), Territory ? *Territory->EnemyPlanRationale.Left(110) : TEXT("syncing...")),
			FLinearColor(.8f, .83f, .9f), 24, 543, Font);
		if (Territory && Territory->MatchResult != EMatchResult::Ongoing)
		{
			const float OverlayX = Canvas->ClipX >= 1420.f ? 12.f : FMath::Max(12.f, Canvas->ClipX - 522.f);
			const float OverlayY = Canvas->ClipX >= 1420.f ? 600.f : 12.f;
			DrawRect(FLinearColor(.02f, .03f, .05f, .95f), OverlayX, OverlayY, 510, 124);
			const bool bVictory = Territory->MatchResult == EMatchResult::Victory;
			DrawText(bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"),
				bVictory ? FLinearColor(.5f, 1.f, .65f) : FLinearColor(1.f, .45f, .35f),
				OverlayX + 16, OverlayY + 12, Font, 2.f);
			DrawText(TEXT("Enter: restart a fresh match"), FLinearColor::White, OverlayX + 16, OverlayY + 67, Font);
			DrawText(TEXT("Camera, cursor and army selection still work."),
				FLinearColor(.8f, .83f, .9f), OverlayX + 16, OverlayY + 90, Font);
		}
		// PlayerArray and each player's doctrine/name replicate to everyone; no
		// other player's controller or wallet is accessible on remote clients.
		const bool bWide = Canvas->ClipX >= 1420.f;
		const float RosterX = bWide ? Canvas->ClipX - 560.f : 12.f;
		const float RosterY = bWide ? 12.f : 600.f;
		const float RosterWidth = bWide ? 548.f : FMath::Min(880.f, Canvas->ClipX - 24.f);
		DrawRect(FLinearColor(.015f, .025f, .04f, .9f), RosterX, RosterY, RosterWidth, 136.f);
		int32 Connected = 0;
		if (Territory)
			for (const APlayerState* Entry : Territory->PlayerArray)
				if (const ACommandPlayerState* Peer = Cast<ACommandPlayerState>(Entry))
					if (Peer->CommanderIndex >= 0 && Peer->CommanderIndex < 5) ++Connected;
		const TCHAR* NetRole = GetNetMode() == NM_ListenServer ? TEXT("LISTEN HOST")
			: GetNetMode() == NM_Client ? TEXT("CLIENT") : TEXT("LOCAL");
		const FString OwnCommander = Wallet && Wallet->CommanderIndex >= 0 && Wallet->CommanderIndex < 5
			? FString::Printf(TEXT("Commander %d / slot %d"), Wallet->CommanderIndex + 1, Wallet->CommanderIndex + 1)
			: TEXT("syncing...");
		DrawText(FString::Printf(TEXT("CONNECTED %d/5 | %s | YOU: %s"),
			Connected, NetRole, *OwnCommander), FLinearColor::White, RosterX + 12.f, RosterY + 8.f, Font);
		for (int32 Slot = 0; Slot < 5; ++Slot)
		{
			const ACommandPlayerState* Peer = nullptr;
			if (Territory)
				for (const APlayerState* Entry : Territory->PlayerArray)
					if (const ACommandPlayerState* Candidate = Cast<ACommandPlayerState>(Entry))
						if (Candidate->CommanderIndex == Slot) { Peer = Candidate; break; }
			if (!Peer) continue;
			const FString Name = Peer->GetPlayerName().IsEmpty()
				? FString::Printf(TEXT("Commander %d"), Slot + 1) : Peer->GetPlayerName().Left(18);
			DrawText(FString::Printf(TEXT("%s C%d | %s | %s"), Peer == Wallet ? TEXT("YOU ") : TEXT("ALLY"),
				Slot + 1, *Name, DoctrineName(Peer->Doctrine)),
				AArmyUnit::GetCommanderColor(Slot), RosterX + 12.f, RosterY + 30.f + Slot * 20.f, Font);
		}
		if (!Territory || !Wallet) DrawText(TEXT("Commander roster syncing..."),
			FLinearColor::Yellow, RosterX + 12.f, RosterY + 111.f, Font);
		const bool bTerminal = Territory && Territory->MatchResult != EMatchResult::Ongoing;
		const bool bChosen = Wallet && Wallet->Doctrine != EArmyDoctrine::None;
		const FDoctrinePanel Panel = DoctrinePanel(Canvas->ClipX, !Wallet || bChosen || bTerminal);
		if (Panel.Width > 0.f)
		{
			const float X = Panel.X;
			DrawRect(FLinearColor(.015f, .025f, .04f, .94f), X, DoctrineTop, Panel.Width, Panel.Height);
			if (bChosen)
			{
				const TCHAR* Title = TEXT("DOCTRINE | Unknown");
				const TCHAR* Effect1 = TEXT("");
				const TCHAR* Effect2 = TEXT("");
				switch (Wallet->Doctrine)
				{
				case EArmyDoctrine::SiegeOptics:
					Title = TEXT("DOCTRINE | Siege Optics");
					Effect1 = TEXT("Siege range +25%; siege damage -25%.");
					break;
				case EArmyDoctrine::FieldRepairs:
					Title = TEXT("DOCTRINE | Field Repairs");
					Effect1 = TEXT("+5 HP/sec after 5s continuously stationary.");
					Effect2 = TEXT("Firing, damage or movement resets timer.");
					break;
				case EArmyDoctrine::EntrenchedFrontline:
					Title = TEXT("DOCTRINE | Entrenched Frontline");
					Effect1 = TEXT("Stationary frontline while on Hold:");
					Effect2 = TEXT("-25% damage taken.");
					break;
				default: break;
				}
				DrawText(Title, FLinearColor(.5f, 1.f, .7f), X + 12, DoctrineTop + 12, Font, 1.15f);
				DrawText(Effect1, FLinearColor::White, X + 12, DoctrineTop + 42, Font);
				if (*Effect2) DrawText(Effect2, FLinearColor::White, X + 12, DoctrineTop + 63, Font);
				if (!Controller->GetDoctrineFeedback().IsEmpty())
					DrawText(Controller->GetDoctrineFeedback(), FLinearColor::Yellow,
						X + 12, DoctrineTop + 84, Font);
				DrawText(TEXT("Both owned armies and replacements"), FLinearColor(.72f, .8f, .9f),
					X + 12, DoctrineTop + 108, Font);
				DrawText(TEXT("No respec until a fresh restart"), FLinearColor(.72f, .8f, .9f),
					X + 12, DoctrineTop + 128, Font);
			}
			else if (bTerminal)
			{
				DrawText(TEXT("DOCTRINE | choices closed"), FLinearColor::Yellow,
					X + 12, DoctrineTop + 12, Font, 1.15f);
				DrawText(TEXT("Match over. Enter starts a fresh match."), FLinearColor::White,
					X + 12, DoctrineTop + 45, Font);
				DrawText(Controller->GetDoctrineFeedback(), FLinearColor::Yellow,
					X + 12, DoctrineTop + 78, Font);
			}
			else if (!Wallet || !Territory)
			{
				DrawText(TEXT("DOCTRINE | syncing..."), FLinearColor::Yellow,
					X + 12, DoctrineTop + 12, Font, 1.15f);
			}
			else
			{
				DrawText(TEXT("DOCTRINE | choose one (free)"),
					FLinearColor(.65f, .9f, 1.f), X + 12, DoctrineTop + 10, Font, 1.15f);
				DrawText(TEXT("F1 / F2 / F3 or click | once per match | no pause"),
					FLinearColor(.8f, .85f, .9f), X + 12, DoctrineTop + 38, Font);
				auto DrawCard = [this, Font, X, &Panel](int32 Index, const TCHAR* Name, const TCHAR* Effect1, const TCHAR* Effect2)
				{
					const float Y = DoctrineCardTop + Index * DoctrineCardStep;
					DrawRect(FLinearColor(.09f, .16f, .23f, .94f), X + 8, Y, Panel.Width - 16.f, DoctrineCardHeight);
					DrawText(Name, FLinearColor(.6f, 1.f, .8f), X + 18, Y + 9, Font, 1.1f);
					DrawText(Effect1, FLinearColor::White, X + 18, Y + 38, Font);
					if (*Effect2) DrawText(Effect2, FLinearColor::White, X + 18, Y + 60, Font);
				};
				DrawCard(0, TEXT("F1 | SIEGE OPTICS"), TEXT("Siege range +25%; siege damage -25%."), TEXT(""));
				DrawCard(1, TEXT("F2 | FIELD REPAIRS"), TEXT("+5 HP/sec after 5s continuously stationary."),
					TEXT("Firing, damage or movement resets timer."));
				DrawCard(2, TEXT("F3 | ENTRENCHED FRONTLINE"), TEXT("Stationary frontline while on Hold:"),
					TEXT("-25% damage taken."));
				const FString& Feedback = Controller->GetDoctrineFeedback();
				DrawText(Feedback.IsEmpty() ? TEXT("Choose when safe; no deadline or resource cost.") : *Feedback,
					FLinearColor::Yellow, X + 12, DoctrineTop + 400, Font);
			}
		}
	}
}
