#include "FactoryLineSubsystem.h"

#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "FactoryTwinSettings.h"

// ---------------------------------------------------------------------------
// UFactoryTwinSettings
// ---------------------------------------------------------------------------

UFactoryTwinSettings::UFactoryTwinSettings()
{
	// Default to the local development broker from Tools/broker. The production
	// broker (192.168.100.102:31883) is a settings change away once reachable.
	EdgeNode.Mqtt.Host = TEXT("127.0.0.1");
	EdgeNode.Mqtt.Port = 1883;
	EdgeNode.Mqtt.ClientId = TEXT("ue5_smt_simulator");
	EdgeNode.Mqtt.KeepAliveSeconds = 60;
}

const UFactoryTwinSettings* UFactoryTwinSettings::Get()
{
	return GetDefault<UFactoryTwinSettings>();
}

// ---------------------------------------------------------------------------
// UFactoryLineSubsystem
// ---------------------------------------------------------------------------

void UFactoryLineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Deliberately does not auto-start here; see OnWorldBeginPlay.
}

void UFactoryLineSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings != nullptr && Settings->bAutoStartOnBeginPlay && Settings->bUseNativeSimulation)
	{
		StartLine();
	}
}

void UFactoryLineSubsystem::Deinitialize()
{
	StopLine();
	Machines.Reset();
	Super::Deinitialize();
}

bool UFactoryLineSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. An editor-preview world must not open a broker session.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFactoryLineSubsystem::StartLine()
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings == nullptr)
	{
		return;
	}

	if (!Settings->bUseNativeSimulation)
	{
		UE_LOG(LogFactorySim, Log,
			TEXT("Native simulation is disabled in Factory Twin settings; not starting. "
				 "The legacy Python layer owns publishing while this is off."));
		return;
	}

	StartLineWithConfig(Settings->EdgeNode);
}

void UFactoryLineSubsystem::StartLineWithConfig(const FSparkplugEdgeNodeConfig& InConfig)
{
	if (EdgeNode != nullptr)
	{
		UE_LOG(LogFactorySim, Warning, TEXT("Line is already started"));
		return;
	}

	if (LotId.IsEmpty())
	{
		StartNewLot();
	}

	EdgeNode = NewObject<USparkplugEdgeNode>(this);
	EdgeNode->OnOnlineStateChanged.AddDynamic(this, &UFactoryLineSubsystem::HandleEdgeNodeOnline);
	EdgeNode->OnNodeCommand.AddDynamic(this, &UFactoryLineSubsystem::HandleNodeCommand);

	PendingConfig = InConfig;

	// Defer the actual session by a tick.
	//
	// Whichever Blueprint calls this does so from its own BeginPlay, and actor
	// BeginPlay order is arbitrary -- connecting immediately announced the node
	// with only the two machines that happened to have registered first, leaving
	// the rest to trickle in as late DBIRTHs. One tick is enough for every actor
	// in the level to have begun play and registered.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			this, &UFactoryLineSubsystem::BeginEdgeNodeSession);
	}
	else
	{
		BeginEdgeNodeSession();
	}
}

void UFactoryLineSubsystem::BeginEdgeNodeSession()
{
	if (EdgeNode == nullptr)
	{
		return;
	}

	// Devices must be registered before Connect so their DBIRTHs are part of the
	// birth sequence rather than trickling in afterwards.
	RegisterDevicesWithEdgeNode();

	UE_LOG(LogFactorySim, Log, TEXT("Starting line with %d machine(s), lot '%s'"),
		Machines.Num(), *LotId);

	EdgeNode->Connect(PendingConfig);

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings != nullptr && Settings->bAutoProduceOnStart)
	{
		StartAutoProduction(Settings->AutoProductionIntervalSeconds);
	}
}

bool UFactoryLineSubsystem::HasDeviceId(const FString& DeviceId) const
{
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr
			&& Machine->Instance != nullptr
			&& Machine->Instance->DeviceId == DeviceId)
		{
			return true;
		}
	}
	return false;
}

bool UFactoryLineSubsystem::StartLineWithBroker(
	const FString& BrokerHost,
	const int32 BrokerPort,
	const FString& BrokerUsername,
	const FString& BrokerPassword)
{
	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	if (Settings == nullptr)
	{
		return false;
	}

	if (!Settings->bUseNativeSimulation)
	{
		UE_LOG(LogFactorySim, Log,
			TEXT("Native simulation is disabled; ignoring StartLineWithBroker."));
		return false;
	}

	// Take identity and everything else from settings; override only what the
	// operator supplied, so an empty field falls back rather than blanking.
	FSparkplugEdgeNodeConfig Config = Settings->EdgeNode;
	if (!BrokerHost.IsEmpty())
	{
		Config.Mqtt.Host = BrokerHost;
	}
	if (BrokerPort > 0)
	{
		Config.Mqtt.Port = BrokerPort;
	}
	Config.Mqtt.Username = BrokerUsername;
	Config.Mqtt.Password = BrokerPassword;

	StartLineWithConfig(Config);
	return EdgeNode != nullptr;
}

bool UFactoryLineSubsystem::PublishDeviceEvent(const FString& DeviceId, const FString& EventType)
{
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr
			&& Machine->Instance != nullptr
			&& Machine->Instance->DeviceId == DeviceId)
		{
			Machine->PublishEvent(EventType);
			return true;
		}
	}

	UE_LOG(LogFactorySim, Warning,
		TEXT("PublishDeviceEvent: no registered machine with device id '%s'"), *DeviceId);
	return false;
}

void UFactoryLineSubsystem::StopLine()
{
	// Releasing boards onto a line that is going offline would emit events with
	// nowhere to publish them.
	StopAutoProduction();

	if (EdgeNode == nullptr)
	{
		return;
	}

	EdgeNode->Disconnect();
	EdgeNode = nullptr;
}

bool UFactoryLineSubsystem::IsOnline() const
{
	return EdgeNode != nullptr && EdgeNode->IsOnline();
}

FString UFactoryLineSubsystem::StartNewLot()
{
	// Matches the LOT-YYYYmmdd-HHMMSS shape the Python layer produced.
	LotId = FString::Printf(TEXT("LOT-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	return LotId;
}

void UFactoryLineSubsystem::RegisterMachine(UFactoryMachineComponent* Machine)
{
	if (Machine == nullptr || Machines.Contains(Machine))
	{
		return;
	}

	// A Sparkplug device id must be unique on the edge node. Two components
	// claiming the same id would publish interleaved DDATA under one identity
	// and re-announce a DBIRTH each time, which is how a machine component
	// accidentally placed on a per-board spawned actor shows up.
	if (Machine->Instance != nullptr && HasDeviceId(Machine->Instance->DeviceId))
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("%s claims device id '%s', which is already registered. Ignoring it: a device "
				 "id maps to one physical machine, so this component likely belongs on a "
				 "persistent actor rather than one spawned per board."),
			*GetNameSafe(Machine->GetOwner()), *Machine->Instance->DeviceId);
		return;
	}

	Machines.Add(Machine);

	// Joining an already-live line announces immediately.
	if (EdgeNode != nullptr && Machine->Instance != nullptr)
	{
		EdgeNode->RegisterDevice(Machine->Instance->DeviceId, Machine->BuildBirthMetrics());
	}
}

void UFactoryLineSubsystem::UnregisterMachine(UFactoryMachineComponent* Machine)
{
	if (Machine == nullptr)
	{
		return;
	}

	if (EdgeNode != nullptr && Machine->Instance != nullptr)
	{
		EdgeNode->UnregisterDevice(Machine->Instance->DeviceId);
	}
	Machines.Remove(Machine);
}

void UFactoryLineSubsystem::RegisterDevicesWithEdgeNode()
{
	if (EdgeNode == nullptr)
	{
		return;
	}

	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine == nullptr || Machine->Instance == nullptr)
		{
			continue;
		}

		if (Machine->Instance->DeviceId.IsEmpty())
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Machine on '%s' has an instance with no device id; skipping"),
				*GetNameSafe(Machine->GetOwner()));
			continue;
		}

		EdgeNode->RegisterDevice(Machine->Instance->DeviceId, Machine->BuildBirthMetrics());
		UE_LOG(LogFactorySim, Log, TEXT("  device '%s' from %s"),
			*Machine->Instance->DeviceId, *GetNameSafe(Machine->GetOwner()));
	}
}

void UFactoryLineSubsystem::HandleEdgeNodeOnline(const bool bOnline)
{
	UE_LOG(LogFactorySim, Log, TEXT("Line is %s"), bOnline ? TEXT("online") : TEXT("offline"));
	OnLineOnlineChanged.Broadcast(bOnline);
}

void UFactoryLineSubsystem::HandleNodeCommand(const FSparkplugPayload& Payload)
{
	for (const FSparkplugMetric& Metric : Payload.Metrics)
	{
		// `new_material` is the command the retired Python spawn_hook bridged to
		// the PCB spawner; keeping the name keeps existing controllers working.
		if (Metric.Name == TEXT("new_material") && Metric.IntValue != 0)
		{
			UE_LOG(LogFactorySim, Log, TEXT("new_material command received"));
			RequestNewMaterial();
		}
	}
}

void UFactoryLineSubsystem::RequestNewMaterial()
{
	const FString NewLot = StartNewLot();
	++BoardsReleased;

	// Stamp every machine, matching what the Python layer did on this command.
	for (const TObjectPtr<UFactoryMachineComponent>& Machine : Machines)
	{
		if (Machine != nullptr)
		{
			Machine->PublishEvent(FactoryEventTypes::NewMaterial);
		}
	}

	UE_LOG(LogFactorySim, Log, TEXT("New material released, lot '%s' (board %d)"),
		*NewLot, BoardsReleased);

	OnNewMaterialRequested.Broadcast(NewLot);
}

void UFactoryLineSubsystem::StartAutoProduction(const float IntervalSeconds)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
	float Interval = IntervalSeconds;
	if (Interval <= 0.0f)
	{
		Interval = Settings != nullptr ? Settings->AutoProductionIntervalSeconds : 30.0f;
	}
	// A zero or negative period would schedule a timer that fires every frame.
	Interval = FMath::Max(Interval, 0.1f);

	StopAutoProduction();

	AutoProductionInterval = Interval;
	BoardsReleased = 0;

	World->GetTimerManager().SetTimer(
		AutoProductionTimer, this, &UFactoryLineSubsystem::RequestNewMaterial,
		Interval, /*bLoop*/ true, /*FirstDelay*/ 0.0f);

	UE_LOG(LogFactorySim, Log,
		TEXT("Auto production started, releasing a board every %.1fs"), Interval);
}

void UFactoryLineSubsystem::StopAutoProduction()
{
	if (UWorld* World = GetWorld())
	{
		if (AutoProductionTimer.IsValid())
		{
			World->GetTimerManager().ClearTimer(AutoProductionTimer);
			UE_LOG(LogFactorySim, Log,
				TEXT("Auto production stopped after %d board(s)"), BoardsReleased);
		}
	}
	AutoProductionTimer.Invalidate();
	AutoProductionInterval = 0.0f;
}

bool UFactoryLineSubsystem::IsAutoProductionRunning() const
{
	const UWorld* World = GetWorld();
	return World != nullptr
		&& AutoProductionTimer.IsValid()
		&& World->GetTimerManager().IsTimerActive(AutoProductionTimer);
}
