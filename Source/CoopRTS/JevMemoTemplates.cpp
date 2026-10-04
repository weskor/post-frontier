#include "JevMemoTemplates.h"
#include "Misc/ConfigCacheIni.h"
#include "Rules/JevPlanner.h"

DEFINE_LOG_CATEGORY_STATIC(LogJevMemos, Log, All);

namespace
{
struct FTemplateDefinition
{
	const TCHAR* Key;
	const TCHAR* Order;
};

constexpr FTemplateDefinition Definitions[] = {
	{ TEXT("MoveAndHold"), TEXT("Move & Hold:") },
	{ TEXT("Attack"), TEXT("Attack:") },
	{ TEXT("Retreat"), TEXT("Retreat:") },
	{ TEXT("Escalated"), TEXT("Escalated: defending {Region}") },
	{ TEXT("SplitBrainCut"), TEXT("Split-Brain Cut:") }
};

constexpr int32 CutTemplate = 4;

// The writer's template with the plan's values; the region text goes in last, so token-like names are never interpreted.
FString Fill(const FString& Template, int32 TicketNumber, int32 SizeBand, float EtaSeconds, const FString& RegionName)
{
	const int32 Eta = FMath::CeilToInt(EtaSeconds);
	FString Memo = Template;
	Memo.ReplaceInline(TEXT("{Ticket}"), *FString::FromInt(TicketNumber), ESearchCase::CaseSensitive);
	Memo.ReplaceInline(TEXT("{Size}"), *FString::Printf(TEXT("~%d units"), SizeBand), ESearchCase::CaseSensitive);
	Memo.ReplaceInline(TEXT("{ETA}"), *FString::Printf(TEXT("%d:%02d"), Eta / 60, Eta % 60), ESearchCase::CaseSensitive);
	Memo.ReplaceInline(TEXT("{Region}"), *RegionName, ESearchCase::CaseSensitive);
	return Memo;
}

struct FToken
{
	const TCHAR* Text;
	int32 Length;
};

constexpr FToken Tokens[] = {
	{ TEXT("{Ticket}"), 8 },
	{ TEXT("{Size}"), 6 },
	{ TEXT("{Region}"), 8 },
	{ TEXT("{ETA}"), 5 }
};

bool ValidateTemplate(const FString& Template, int32 Index)
{
	const FTemplateDefinition& Definition = Definitions[Index];
	for (const FToken& Token : Tokens)
	{
		if (!Template.Contains(Token.Text, ESearchCase::CaseSensitive))
		{
			UE_LOG(LogJevMemos, Error, TEXT("[JevMemos] %s is missing required token %s"), Definition.Key, Token.Text);
			return false;
		}
	}
	for (int32 Other = 0; Other < UE_ARRAY_COUNT(Definitions); ++Other)
	{
		const bool bHasOrder = Template.Contains(Definitions[Other].Order, ESearchCase::CaseSensitive);
		if (bHasOrder != (Other == Index))
		{
			UE_LOG(LogJevMemos, Error, TEXT("[JevMemos] %s must describe only its own order: %s"), Definition.Key, Definition.Order);
			return false;
		}
	}
	for (int32 Position = 0; Position < Template.Len(); ++Position)
	{
		if (Template[Position] != TEXT('{') && Template[Position] != TEXT('}'))
			continue;
		bool bKnownToken = false;
		if (Template[Position] == TEXT('{'))
		{
			for (const FToken& Token : Tokens)
			{
				if (FCString::Strncmp(*Template + Position, Token.Text, Token.Length) == 0)
				{
					Position += Token.Length - 1;
					bKnownToken = true;
					break;
				}
			}
		}
		if (!bKnownToken)
		{
			UE_LOG(LogJevMemos, Error, TEXT("[JevMemos] %s contains an unknown or malformed token at character %d"), Definition.Key, Position);
			return false;
		}
	}
	return true;
}
}

bool FJevMemoTemplates::Load()
{
	bLoaded = false;
	if (!GConfig || GGameIni.IsEmpty())
	{
		UE_LOG(LogJevMemos, Error, TEXT("Cannot load [JevMemos]: Game config is unavailable"));
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Definitions); ++Index)
	{
		if (!GConfig->GetString(TEXT("JevMemos"), Definitions[Index].Key, Templates[Index], GGameIni))
		{
			UE_LOG(LogJevMemos, Error, TEXT("Missing [JevMemos] %s in %s"), Definitions[Index].Key, *GGameIni);
			return false;
		}
		if (!ValidateTemplate(Templates[Index], Index))
			return false;
	}
	bLoaded = true;
	return true;
}

FString FJevMemoTemplates::Format(const JevPlanner::FPlan& Plan, int32 TicketNumber, const FString& RegionName) const
{
	if (!bLoaded)
	{
		UE_LOG(LogJevMemos, Error, TEXT("Cannot format ticket %d: memo templates have not loaded"), TicketNumber);
		return FString();
	}
	int32 Index = INDEX_NONE;
	switch (Plan.Verb)
	{
	case JevPlanner::EVerb::MoveAndHold:
		Index = 0;
		break;
	case JevPlanner::EVerb::Attack:
		Index = 1;
		break;
	case JevPlanner::EVerb::Retreat:
		Index = 2;
		break;
	}
	if (Index == INDEX_NONE || (Plan.bEscalated && (Plan.Verb != JevPlanner::EVerb::MoveAndHold || Plan.Target != Plan.Source))
		|| !FMath::IsFinite(Plan.EtaSeconds) || Plan.EtaSeconds < 0.f || Plan.EtaSeconds >= static_cast<float>(MAX_int32))
	{
		UE_LOG(LogJevMemos, Error, TEXT("Cannot format ticket %d: plan has an invalid verb, escalation or ETA"), TicketNumber);
		return FString();
	}
	if (Plan.bEscalated)
		Index = 3;
	return Fill(Templates[Index], TicketNumber, Plan.SizeBand, Plan.EtaSeconds, RegionName);
}

FString FJevMemoTemplates::FormatCut(int32 TicketNumber, int32 SizeBand, float EtaSeconds, const FString& RegionName) const
{
	if (!bLoaded || !FMath::IsFinite(EtaSeconds) || EtaSeconds < 0.f || EtaSeconds >= static_cast<float>(MAX_int32))
	{
		UE_LOG(LogJevMemos, Error, TEXT("Cannot format cut ticket %d: templates not loaded or invalid ETA"), TicketNumber);
		return FString();
	}
	return Fill(Templates[CutTemplate], TicketNumber, SizeBand, EtaSeconds, RegionName);
}
