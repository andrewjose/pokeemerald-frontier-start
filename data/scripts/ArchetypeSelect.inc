@ ===========================================================================
@ data/scripts/ArchetypeSelect.inc
@ After naming: player picks archetype and level, receives party + PC boxes.
@ gSpecialVar_0x8004 = archetype index (0-7)
@ gSpecialVar_0x8005 = level (50 or 100)
@ ===========================================================================

ArchetypeSelect_Script::
	lockall
	msgbox ArchetypeSelect_Text_Intro, MSGBOX_DEFAULT

@ --- Step 1: Choose archetype ---
ArchetypeSelect_ChooseArchetype::
	multichoice 0, 0, MULTI_ARCHETYPE_SELECT, FALSE
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, ArchetypeSelect_ChooseArchetype
	copyvar VAR_0x8004, VAR_RESULT

@ --- Step 2: Choose level (B returns to archetype select) ---
ArchetypeSelect_ChooseLevel::
	multichoice 0, 0, MULTI_ARCHETYPE_LEVEL, FALSE
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, ArchetypeSelect_ChooseArchetype
	@ VAR_RESULT: 0 = Lv.50, 1 = Lv.100
	goto_if_eq VAR_RESULT, 0, ArchetypeSelect_SetLevel50
	setvar VAR_0x8005, 100
	goto ArchetypeSelect_GiveTeam

ArchetypeSelect_SetLevel50::
	setvar VAR_0x8005, 50

ArchetypeSelect_GiveTeam::
	special GivePresetPartyAndFillBoxes
	msgbox ArchetypeSelect_Text_Confirm, MSGBOX_DEFAULT
	releaseall
	end

ArchetypeSelect_Text_Intro:
	.string "Welcome to the Battle Frontier!\p"
	.string "Choose your fighting style.$"

ArchetypeSelect_Text_Confirm:
	.string "Your team is ready.\p"
	.string "Check your PC for the rest\n"
	.string "of the teams!\p"
	.string "Good luck, Trainer!$"
