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

@ v1.41: the overview first (also under GUIDE in the START menu), then the menu
	call FrontierGuide_ShowText
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

@ ---------------------------------------------------------------------------
@ v1.41: overview of the hack, shown before the fighting-style menu and from
@ GUIDE in the START menu.
@ ---------------------------------------------------------------------------
FrontierGuide_Script::
	lockall
	call FrontierGuide_ShowText
	releaseall
	end

FrontierGuide_ShowText::
	msgbox FrontierGuide_Text, MSGBOX_DEFAULT
	return

FrontierGuide_Text:
	.string "Welcome to POKéMON EMERALD\n"
	.string "BATTLE FRONTIER!\p"
	.string "Every facility is open from the\n"
	.string "start. Pick a team and go!\p"
	.string "Your PC boxes hold ready-made\n"
	.string "teams at Lv50 and Lv100, the\l"
	.string "legendaries, and Battle Tower\l"
	.string "sample teams.\p"
	.string "In battle, press L+R to see stats,\n"
	.string "IVs, EVs and damage for both sides.\p"
	.string "When you pick a move, the foe's\n"
	.string "HP bar shows the damage it can do.\p"
	.string "Choose EDIT in the party menu or\n"
	.string "the PC to change any POKéMON.\p"
	.string "The move tutors' house has free\n"
	.string "tutors, a move relearner and a\l"
	.string "move deleter.\p"
	.string "You can switch these features on\n"
	.string "and off in OPTION.\p"
	.string "Press R + START on the map for\n"
	.string "the debug menu.\p"
	.string "To read this again, choose GUIDE\n"
	.string "in the START menu.$"
