"""Match actors every arena level places: the ArenaBounds, both Headquarters and the three sectors.

Imported by Build/GenerateCommandMap.py (Boot) and Build/GenerateCampusZero.py; not run directly.
ACommandGameMode discovers these actors at BeginPlay instead of spawning them at coordinates, and a level
missing the arena or either HQ refuses to start a match. The coordinates are the prototype's layout.
"""
import unreal


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


# Sector order is the ACapturePoint SiteIndex order (ACommandGameState::CaptureSites[0] is Substation7).
SITES = {"Substation7": (-850, -1800), "CoolingPlant": (1450, 1100), "FibreJunction": (600, -2200)}
SITE_Z = 5
FRIENDLY_HQ = (-3500, -600)
ENEMY_HQ = (3200, 2300)
HQ_Z = 110  # AHeadquarters hit box half height: the body stands on the floor
FRIENDLY_TEAM = 0
ENEMY_TEAM = 5


def native_class(name):
    return require(unreal.load_class(None, "/Script/CoopRTS." + name),
                   "Build CoopRTSEditor before generating the map")


def place(spawn):
    """Place the match actors with `spawn(actor_class, label, location)`.

    Returns the arena half extent (x, y) in cm from the placed ArenaBounds so the caller sizes navigation,
    floors and walls from the same definition the game reads.
    """
    arena = spawn(native_class("ArenaBounds"), "Arena", (0, 0, 0))
    for index, (name, (x, y)) in enumerate(SITES.items()):
        site = spawn(native_class("CapturePoint"), name, (x, y, SITE_Z))
        site.set_editor_property("site_kind", unreal.CaptureSiteKind.RESOURCE)
        site.set_editor_property("site_index", index)
    for label, (x, y), team in (("FriendlyHeadquarters", FRIENDLY_HQ, FRIENDLY_TEAM),
                                ("EnemyHeadquarters", ENEMY_HQ, ENEMY_TEAM)):
        spawn(native_class("Headquarters"), label, (x, y, HQ_Z)).set_editor_property("team_index", team)
    extent = arena.get_editor_property("half_extent")
    require(extent.x > 0 and extent.y > 0, "ArenaBounds has no extent")
    return float(extent.x), float(extent.y)
