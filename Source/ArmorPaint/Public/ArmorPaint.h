// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UTexture2D;

// Paints the selected objects in ArmorPaint and brings the painted textures back.
// The selection gets a project at <Project>/ArmorPaint/<Name>/<Name>.arm, multiple selected
// objects are painted together in one project and share the resulting material.
// When ArmorPaint closes, textures are exported from the project next to it,
// imported into /Game/ArmorPaint/<Name> and applied to the object through a material.
class FArmorPaintModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command. */
	void PluginButtonClicked();

private:

	// An ArmorPaint instance started by the plugin, textures are applied when it closes
	struct FPending
	{
		FProcHandle Process;
		FString Name;
		FString ArmPath;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Components;
	};

	// A selected object to paint
	struct FPaintTarget
	{
		FString Name;
		UStaticMeshComponent* Component;
	};

	void RegisterMenus();
	bool Tick(float DeltaTime);
	void OnApplicationActivationStateChanged(bool bActive);

	FString GetExecutable(bool bPrompt);
	bool StartArmorPaint(const FString& Exe, const FString& Args, bool bHidden, FProcHandle& OutProcess);
	bool ExportMeshesToObj(const TArray<FPaintTarget>& Targets, const FString& ObjPath);
	bool ExportTextures(const FString& ArmPath);
	bool ImportTextures(const FString& Name, TMap<FString, UTexture2D*>& OutTextures);
	UMaterialInterface* GetOrCreateMaterial(const FString& Name, const TMap<FString, UTexture2D*>& Textures);
	void ApplyMaterial(const TArray<UStaticMeshComponent*>& Components, UMaterialInterface* Material);
	void UpdateProject(const FString& Name, const TArray<UStaticMeshComponent*>& Components);

	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<FPending> Pending;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ActivationHandle;
	bool bUpdating = false;
};
