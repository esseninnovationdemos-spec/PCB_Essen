#include "FactoryTwinBlueprintLibrary.h"

#include "Engine/World.h"
#include "FactoryLineSubsystem.h"
#include "FactoryProductionLine.h"
#include "FactorySimTypes.h"
#include "EngineUtils.h"

UFactoryLineSubsystem* UFactoryTwinBlueprintLibrary::GetFactoryLine(
	const UObject* WorldContextObject)
{
	// Returns null rather than asserting outside a game world, so a Blueprint
	// running in an editor preview simply does nothing.
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World != nullptr ? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr;
}

bool UFactoryTwinBlueprintLibrary::StartFactoryLine(
	const UObject* WorldContextObject,
	const FString& BrokerHost,
	const int32 BrokerPort,
	const FString& BrokerUsername,
	const FString& BrokerPassword)
{
	UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	if (Line == nullptr)
	{
		UE_LOG(LogFactorySim, Warning, TEXT("StartFactoryLine: no line subsystem in this world"));
		return false;
	}
	return Line->StartLineWithBroker(BrokerHost, BrokerPort, BrokerUsername, BrokerPassword);
}

void UFactoryTwinBlueprintLibrary::StopFactoryLine(const UObject* WorldContextObject)
{
	if (UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject))
	{
		Line->StopLine();
	}
}

bool UFactoryTwinBlueprintLibrary::PublishFactoryDeviceEvent(
	const UObject* WorldContextObject, const FString& DeviceId, const FString& EventType)
{
	UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	return Line != nullptr && Line->PublishDeviceEvent(DeviceId, EventType);
}

bool UFactoryTwinBlueprintLibrary::PublishUnsString(
	const UObject* WorldContextObject, const FString& Topic, const FString& Payload)
{
	const UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	if (Line == nullptr || Line->GetEdgeNode() == nullptr)
	{
		return false;
	}
	// UNS values are current-state snapshots superseded by the next one, so QoS 0
	// matches how the stream is consumed.
	return Line->GetEdgeNode()->PublishRawString(
		Topic, Payload, EMqttQoS::AtMostOnce, false);
}

void UFactoryTwinBlueprintLibrary::RequestNewMaterial(const UObject* WorldContextObject)
{
	if (UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject))
	{
		Line->RequestNewMaterial();
	}
}

void UFactoryTwinBlueprintLibrary::StartAutoProduction(
	const UObject* WorldContextObject, const float IntervalSeconds)
{
	if (UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject))
	{
		Line->StartAutoProduction(IntervalSeconds);
	}
}

void UFactoryTwinBlueprintLibrary::StopAutoProduction(const UObject* WorldContextObject)
{
	if (UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject))
	{
		Line->StopAutoProduction();
	}
}

bool UFactoryTwinBlueprintLibrary::ToggleAutoProduction(
	const UObject* WorldContextObject, const float IntervalSeconds)
{
	UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	if (Line == nullptr)
	{
		return false;
	}

	if (Line->IsAutoProductionRunning())
	{
		Line->StopAutoProduction();
		return false;
	}

	Line->StartAutoProduction(IntervalSeconds);
	return true;
}

bool UFactoryTwinBlueprintLibrary::IsAutoProductionRunning(const UObject* WorldContextObject)
{
	const UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	return Line != nullptr && Line->IsAutoProductionRunning();
}

bool UFactoryTwinBlueprintLibrary::IsFactoryLineOnline(const UObject* WorldContextObject)
{
	const UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	return Line != nullptr && Line->IsOnline();
}

FString UFactoryTwinBlueprintLibrary::GetFactoryLotId(const UObject* WorldContextObject)
{
	const UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	return Line != nullptr ? Line->GetLotId() : FString();
}

FString UFactoryTwinBlueprintLibrary::StartNewFactoryLot(const UObject* WorldContextObject)
{
	UFactoryLineSubsystem* Line = GetFactoryLine(WorldContextObject);
	return Line != nullptr ? Line->StartNewLot() : FString();
}

FVector2D UFactoryTwinBlueprintLibrary::SnapToFactoryGrid(const FVector2D PositionMetres)
{
	return FactoryGrid::SnapMetres(PositionMetres);
}

FFactoryGridCoord UFactoryTwinBlueprintLibrary::FactoryGridCellAt(const FVector2D PositionMetres)
{
	return FactoryGrid::MetresToCell(PositionMetres);
}

FVector2D UFactoryTwinBlueprintLibrary::FactoryGridCellToMetres(const FFactoryGridCoord Cell)
{
	return FactoryGrid::CellToMetres(Cell);
}

FVector UFactoryTwinBlueprintLibrary::FactoryLayoutToWorld(
	const FVector2D PositionMetres, const float HeightCm)
{
	return FactoryGrid::MetresToWorld(PositionMetres, HeightCm);
}

float UFactoryTwinBlueprintLibrary::GetFactoryGridPitchMetres()
{
	return FactoryGrid::GetPitchMetres();
}

AFactoryProductionLine* UFactoryTwinBlueprintLibrary::GetProductionLine(
	const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<AFactoryProductionLine> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
