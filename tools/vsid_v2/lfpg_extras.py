"""LFPG strategies of the CT manual (p.40), which the v1 config cannot express (spec §4.2).

NIGHT is not a SID assignment by runway pair and EVX no longer exists: no strategy for them.
ALIMO is not in the config.
"""

DEFAULT_STRATEGY = "roulage-mini"

STRATEGIES = {
	"roulage-mini": {"label": "Roulage mini", "assign": {"*": "nearest"}},
	"ranux-dikol-sud": {"label": "RANUX DIKOL -> Sud", "assign": {"RANUX": "south", "DIKOL": "south"}},
	"lanvi-bubli-baxir-nord": {"label": "LANVI BUBLI BAXIR -> Nord",
		"assign": {"LANVI": "north", "BUBLI": "north", "BAXIR": "north"}},
}
