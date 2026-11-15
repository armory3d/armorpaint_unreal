#pragma once

#include "ArmorPaintSettings.generated.h"

UENUM()
enum class EArmorPaintBakeResolution : uint8
{
    Res512 UMETA(DisplayName = "512"),
    Res1024 UMETA(DisplayName = "1024"),
    Res2048 UMETA(DisplayName = "2048"),
    Res4096 UMETA(DisplayName = "4096"),
    Res8192 UMETA(DisplayName = "8192"),
};

UCLASS(config = Engine, defaultconfig)
class UArmorPaintSettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(config, EditAnywhere, Category = Settings, meta = (DisplayName = "ArmorPaint Path"))
    FDirectoryPath ArmorPaintPath;

    // Resolution of the textures baked from the current materials when a new ArmorPaint project is created
    UPROPERTY(config, EditAnywhere, Category = Settings)
    EArmorPaintBakeResolution BakeResolution = EArmorPaintBakeResolution::Res2048;

    int32 GetBakeSize() const
    {
        return 512 << (int32)BakeResolution;
    }
};
