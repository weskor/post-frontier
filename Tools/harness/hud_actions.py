"""Shared EHUDAction ordinals from Source/CoopRTS/CommandHUD.h."""

BUILD_BARRACKS, RECIPE_RANGED, RECIPE_SIEGE, TOGGLE_PRODUCTION, RESEARCH_REPAIRS = (
    1,
    6,
    7,
    8,
    13,
)
CONSTRUCTION = 15
SELECT_FORCE = 44
# RecipeSlot0..5: the first three sit in the fixed 0..15 block, the rest were appended later.
RECIPE_SLOTS = frozenset({5, 6, 7, 19, 20, 21})
BRANCH_PURCHASE = 54
