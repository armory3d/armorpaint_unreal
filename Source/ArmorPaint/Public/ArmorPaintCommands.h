// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ArmorPaintStyle.h"

class FArmorPaintCommands : public TCommands<FArmorPaintCommands>
{
public:
	FArmorPaintCommands()
		: TCommands<FArmorPaintCommands>(TEXT("ArmorPaint"), NSLOCTEXT("Contexts", "ArmorPaint", "ArmorPaint Plugin"), NAME_None, FArmorPaintStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> PluginAction;
	TSharedPtr<FUICommandInfo> UnpaintAction;
};
