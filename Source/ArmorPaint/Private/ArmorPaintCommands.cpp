// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ArmorPaintCommands.h"

#define LOCTEXT_NAMESPACE "FArmorPaintModule"

void FArmorPaintCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "ArmorPaint", "Paint selected object in ArmorPaint", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
