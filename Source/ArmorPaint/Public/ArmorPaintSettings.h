#pragma once

#include "ArmorPaintSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class UArmorPaintSettings : public UObject
{
    GENERATED_UCLASS_BODY()

    UPROPERTY(config, EditAnywhere, Category = Settings)
    FDirectoryPath ArmorPaintPath;
};
