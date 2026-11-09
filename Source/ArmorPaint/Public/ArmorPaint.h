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

// Paints the selected object in ArmorPaint and brings the painted textures back.
// Each painted object gets a project at <Project>/ArmorPaint/<Name>/<Name>.arm.
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
		TWeakObjectPtr<UStaticMeshComponent> Component;
	};

	void RegisterMenus();
	bool Tick(float DeltaTime);
	void OnApplicationActivationStateChanged(bool bActive);

	FString GetExecutable(bool bPrompt);
	bool StartArmorPaint(const FString& Exe, const FString& Args, bool bHidden, FProcHandle& OutProcess);
	bool ExportMeshToObj(UStaticMesh* StaticMesh, const FString& ObjPath, const FString& Name);
	bool ExportTextures(const FString& ArmPath);
	bool ImportTextures(const FString& Name, TMap<FString, UTexture2D*>& OutTextures);
	UMaterialInterface* GetOrCreateMaterial(const FString& Name, const TMap<FString, UTexture2D*>& Textures);
	void ApplyMaterial(UStaticMeshComponent* Component, UMaterialInterface* Material);
	void UpdateProject(const FString& Name, UStaticMeshComponent* Component);

	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<FPending> Pending;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ActivationHandle;
	bool bUpdating = false;
};
