// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArmorPaintStyle.h"
#include "ArmorPaint.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FArmorPaintStyle::StyleInstance = nullptr;

void FArmorPaintStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FArmorPaintStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FArmorPaintStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("ArmorPaintStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef< FSlateStyleSet > FArmorPaintStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("ArmorPaintStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("ArmorPaint")->GetBaseDir() / TEXT("Resources"));

	Style->Set("ArmorPaint.PluginAction", new IMAGE_BRUSH(TEXT("ButtonIcon_40x"), Icon40x40));
	return Style;
}

void FArmorPaintStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FArmorPaintStyle::Get()
{
	return *StyleInstance;
}
