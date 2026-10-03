#include "ForceBar.h"

namespace CommandHUDPanels
{
void FormatForceCardStatus(const ForceCardPolicy::FState& State, FStringBuilderBase& Text)
{
	using ForceCardPolicy::EState;
	switch (State.State)
	{
	case EState::Responding:
		Text << TEXT("Responding \u00B7 ") << State.Threat;
		break;
	case EState::Withdrawing:
		Text.Appendf(TEXT("Withdrawing \u00B7 %d/%d"), State.Joined, State.Capacity);
		if (State.ResumeCount > 0)
			Text.Appendf(TEXT(" \u2192 resumes at %d/%d"), State.ResumeCount, State.Capacity);
		break;
	case EState::Refilling:
		Text.Appendf(TEXT("Refilling \u00B7 %d/%d"), State.Joined, State.Capacity);
		break;
	case EState::Holding:
		Text << TEXT("Holding ") << State.Target;
		break;
	case EState::Marching:
		Text << TEXT("Marching to ") << State.Target;
		break;
	case EState::Retreating:
		Text << TEXT("Retreating to ") << State.Target;
		break;
	}
	if (State.ETA >= 0 && (State.State == EState::Marching || State.State == EState::Retreating || State.State == EState::Withdrawing))
		Text.Appendf(TEXT(" \u00B7 %d:%02d"), State.ETA / 60, State.ETA % 60);
}
}
