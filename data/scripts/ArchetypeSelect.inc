@ ===========================================================================
@ data/scripts/ArchetypeSelect.inc
@ After naming: player picks archetype, receives L50 starting party + filled boxes.
@ gSpecialVar_0x8004 = archetype index (0-7)
@ gSpecialVar_0x8005 = level (always 50 for the spawn party)
@
@ Boxes contain BOTH levels for v13 archetypes (slots 0-5 L50, 6-11 L100) and
@ all four Tower boxes regardless of choice.
@ ===========================================================================

ArchetypeSelect_Script::
	lockall
@ Wait for the "BATTLE FRONTIER" location-name banner to finish animating in,
@ then dismiss it before opening any windows. Without this, the banner's slide
@ animation breaks the multichoice scrollbox graphics.
	delay 120
	special HideMapNamePopUpWindow
	delay 10

@ v1.41: the overview first (also under OPTION > GUIDE), then the menu
	call FrontierGuide_ShowText

@ v1.45: settings preset (OPTION can change any of them later)
	msgbox ArchetypeSelect_Text_PresetInfo, MSGBOX_DEFAULT
ArchetypeSelect_ChoosePreset::
	message ArchetypeSelect_Text_PresetAsk
	waitmessage
	multichoicedefault 0, 0, MULTI_SETTINGS_PRESET, 1, FALSE
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, ArchetypeSelect_ChoosePreset
	copyvar VAR_0x8004, VAR_RESULT
	special ApplySettingsPreset

	message ArchetypeSelect_Text_Intro
	waitmessage

@ --- Choose archetype ---
ArchetypeSelect_ChooseArchetype::
	multichoice 0, 0, MULTI_ARCHETYPE_SELECT, FALSE
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, ArchetypeSelect_ChooseArchetype
	copyvar VAR_0x8004, VAR_RESULT

@ --- Always L50 spawn party ---
	setvar VAR_0x8005, 50

@ --- Build the party and load the preset boxes (baked into the ROM since v1.38, so no wait) ---
	special GivePresetPartyAndFillBoxes
	closemessage
	releaseall
	end

ArchetypeSelect_Text_Intro:
	.string "Choose your fighting style.$"

ArchetypeSelect_Text_PresetInfo:
	.string "First, pick your settings.\n"
	.string "You can change any of them in OPTION.\p"
	.string "VANILLA: EMERALD's own settings,\n"
	.string "with the new features off.\p"
	.string "VANILLA+: the new features on,\n"
	.string "with FAST text.\p"
	.string "MODERN: everything on, plus\n"
	.string "INSTANT text, no battle animations,\l"
	.string "SET battles and streaks kept on quit.$"

ArchetypeSelect_Text_PresetAsk:
	.string "Which settings would you like?$"

@ ---------------------------------------------------------------------------
@ v1.41: overview of the hack, shown before the fighting-style menu. The text
@ (gText_FrontierGuide) lives in src/option_menu.c, which also shows it from
@ OPTION > GUIDE.
@ ---------------------------------------------------------------------------
FrontierGuide_ShowText::
	msgbox gText_FrontierGuide, MSGBOX_DEFAULT
	return
