// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArmorPaintCommands.h"

#define LOCTEXT_NAMESPACE "FArmorPaintModule"

void FArmorPaintCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "ArmorPaint", "Paint selected object in ArmorPaint", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnpaintAction, "Unpaint", "Restore original materials and delete the ArmorPaint project.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
