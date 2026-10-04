#include "Content/MatchContent.h"

int32 UMatchContent::IndexOf(const UArmyUnitDefinition* Definition) const
{
	return Definition ? Units.IndexOfByPredicate([Definition](const UArmyUnitDefinition* Unit) { return Unit == Definition; }) : INDEX_NONE;
}

int32 UMatchContent::IndexOf(const UBuildingDefinition* Definition) const
{
	return Definition ? Buildings.IndexOfByPredicate([Definition](const UBuildingDefinition* Building) { return Building == Definition; }) : INDEX_NONE;
}

int32 UMatchContent::UnitIndexOf(FName Id) const
{
	return Units.IndexOfByPredicate([Id](const UArmyUnitDefinition* Unit) { return Unit && Unit->Id == Id; });
}

int32 UMatchContent::BuildingIndexOf(FName Id) const
{
	return Buildings.IndexOfByPredicate([Id](const UBuildingDefinition* Building) { return Building && Building->Id == Id; });
}

int32 UMatchContent::BranchIndexOf(int32 BaseIndex) const
{
	const UArmyUnitDefinition* Base = Unit(BaseIndex);
	if (!Base || Base->IsBranch())
		return INDEX_NONE;
	return Units.IndexOfByPredicate([Base](const UArmyUnitDefinition* Candidate) { return Candidate && Candidate->BranchOf == Base->Id; });
}

int32 UMatchContent::UnitIndexForRole(EUnitRole Role) const
{
	return Units.IndexOfByPredicate([Role](const UArmyUnitDefinition* Unit) { return Unit && !Unit->IsBranch() && Unit->Role == Role; });
}

int32 UMatchContent::BuildingIndexForKind(EBuildingKind Kind) const
{
	return Buildings.IndexOfByPredicate([Kind](const UBuildingDefinition* Building) { return Building && Building->GetKind() == Kind; });
}
