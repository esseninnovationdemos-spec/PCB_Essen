#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "FactoryRenderCommandlet.generated.h"

/**
 * Authors the flythrough: a Level Sequence over the plant, and the Movie Render
 * Queue configuration that turns it into an MP4.
 *
 * Only authoring happens here. Rendering is a second, separate invocation,
 * because Movie Render Queue drives a game world and needs a real RHI, whereas
 * a commandlet runs in the editor with the level merely loaded:
 *
 *   UnrealEditor-Cmd.exe <project> -run=FactoryRender [-Seconds=13] [-Fps=24]
 *   UnrealEditor-Cmd.exe <project> /Game/level4 -game
 *       -LevelSequence="/Game/Cinematics/LS_PlantFlythrough.LS_PlantFlythrough"
 *       -MoviePipelineConfig="/Game/Cinematics/MRQ_PlantFlythrough.MRQ_PlantFlythrough"
 *       -windowed -resx=1920 -resy=1080
 *
 * The camera itself lives in the level -- see the build commandlet -- and the
 * sequence possesses it.
 */
UCLASS()
class FACTORYSIM_API UFactoryRenderCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryRenderCommandlet();

	virtual int32 Main(const FString& Params) override;
};
