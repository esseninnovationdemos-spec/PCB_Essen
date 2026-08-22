#include "Async/TaskGraphInterfaces.h"
#include "Misc/AutomationTest.h"
#include "MqttConnection.h"
#include "SparkplugEdgeNode.h"
#include "SparkplugProto.h"
#include "SparkplugTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SessionTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/**
	 * Spins while pumping the game thread.
	 *
	 * USparkplugEdgeNode receives its MQTT callbacks via AsyncTask on the game
	 * thread. Automation tests also run on the game thread, so a plain sleep
	 * loop would deadlock: the callbacks would sit in the queue forever.
	 */
	bool SpinAndPump(const TFunctionRef<bool()>& Predicate, const double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Predicate())
			{
				return true;
			}
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.02f);
		}
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		return Predicate();
	}

	/** One observed message, reduced to what the assertions care about. */
	struct FObserved
	{
		FString Verb;
		FString Device;
		int64 Seq = -1;
		bool bHasSeq = false;
		TArray<FString> MetricNames;
	};
}

/**
 * Full session lifecycle against the local broker.
 *
 * Asserts the ordering and sequence numbering the Sparkplug spec mandates,
 * because getting these wrong makes a consumer silently discard the stream:
 *   - NBIRTH is seq 0 and resets the run
 *   - every following message increments seq
 *   - DBIRTH follows NBIRTH, before any DDATA
 *   - a Rebirth command restarts the whole sequence at 0
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugSessionLifecycleTest,
	"FactoryTwin.SparkplugB.Live.SessionLifecycle",
	SessionTestFlags)

bool FSparkplugSessionLifecycleTest::RunTest(const FString& Parameters)
{
	const FString GroupId = TEXT("SMT_Line_Test");
	const FString NodeId = TEXT("SessionTest");

	// --- watcher, on the raw connection so it needs no game-thread pumping ---
	FMqttConnectionOptions WatcherOptions;
	WatcherOptions.Host = TEXT("127.0.0.1");
	WatcherOptions.Port = 1883;
	WatcherOptions.ClientId = TEXT("factorytwin-session-watcher");
	WatcherOptions.bAutoReconnect = false;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Watcher =
		FMqttConnection::Create(WatcherOptions);

	std::atomic<bool> bWatcherConnected{ false };
	FCriticalSection ObservedLock;
	TArray<FObserved> Observed;

	Watcher->OnConnected().AddLambda([&bWatcherConnected](const EMqttConnectReturnCode Code)
	{
		if (Code == EMqttConnectReturnCode::Accepted) { bWatcherConnected = true; }
	});

	Watcher->OnMessage().AddLambda(
		[&Observed, &ObservedLock](const FMqttTransportMessage& Message)
		{
			TArray<FString> Segments;
			Message.Topic.ParseIntoArray(Segments, TEXT("/"), false);
			if (Segments.Num() < 4) { return; }

			FSparkplugPayload Payload;
			if (!SparkplugProto::DecodePayload(Message.Payload, Payload)) { return; }

			FObserved Entry;
			Entry.Verb = Segments[2];
			Entry.Device = Segments.Num() >= 5 ? Segments[4] : FString();
			Entry.Seq = Payload.Seq;
			Entry.bHasSeq = Payload.bHasSeq;
			for (const FSparkplugMetric& Metric : Payload.Metrics)
			{
				Entry.MetricNames.Add(Metric.Name);
			}

			FScopeLock Lock(&ObservedLock);
			Observed.Add(MoveTemp(Entry));
		});

	Watcher->Open();
	if (!SpinAndPump([&bWatcherConnected]() { return bWatcherConnected.load(); }, 10.0))
	{
		AddWarning(TEXT("No MQTT broker on 127.0.0.1:1883 - skipping. ")
			TEXT("Start it with: cd Tools/broker && docker compose up -d"));
		Watcher->Close();
		return true;
	}

	Watcher->Subscribe({ MakeTuple(
		FString::Printf(TEXT("spBv1.0/%s/#"), *GroupId), EMqttQoS::AtLeastOnce) });
	FPlatformProcess::Sleep(0.5f);

	// --- edge node ---------------------------------------------------------
	USparkplugEdgeNode* Node = NewObject<USparkplugEdgeNode>();
	Node->AddToRoot();  // keep it alive across any GC during the test

	TArray<FSparkplugMetric> OvenBirth;
	OvenBirth.Add(FSparkplugMetric::MakeFloat(TEXT("oven_temp_c"), 23, 0.0f));
	OvenBirth.Add(FSparkplugMetric::MakeInt32(TEXT("state_code"), 26, 0));
	Node->RegisterDevice(TEXT("REFLOW_OVEN"), OvenBirth);

	TArray<FSparkplugMetric> SpiBirth;
	SpiBirth.Add(FSparkplugMetric::MakeFloat(TEXT("area"), 54, 0.0f));
	Node->RegisterDevice(TEXT("SOLDER_INSP"), SpiBirth);

	FSparkplugEdgeNodeConfig NodeConfig;
	NodeConfig.GroupId = GroupId;
	NodeConfig.EdgeNodeId = NodeId;
	NodeConfig.Mqtt.Host = TEXT("127.0.0.1");
	NodeConfig.Mqtt.Port = 1883;
	NodeConfig.Mqtt.ClientId = TEXT("factorytwin-session-node");
	NodeConfig.Mqtt.bAutoReconnect = false;

	Node->Connect(NodeConfig);

	TestTrue(TEXT("node came online"),
		SpinAndPump([Node]() { return Node->IsOnline(); }, 10.0));

	// NBIRTH + 2 DBIRTH
	TestTrue(TEXT("birth sequence observed"), SpinAndPump([&Observed, &ObservedLock]()
	{
		FScopeLock Lock(&ObservedLock);
		return Observed.Num() >= 3;
	}, 10.0));

	Node->PublishDeviceData(TEXT("REFLOW_OVEN"), {
		FSparkplugMetric::MakeFloat(TEXT("oven_temp_c"), 23, 241.5f) });

	TestTrue(TEXT("DDATA observed"), SpinAndPump([&Observed, &ObservedLock]()
	{
		FScopeLock Lock(&ObservedLock);
		return Observed.Num() >= 4;
	}, 10.0));

	{
		FScopeLock Lock(&ObservedLock);

		TestEqual(TEXT("first message is NBIRTH"), Observed[0].Verb, FString(TEXT("NBIRTH")));
		TestTrue(TEXT("NBIRTH has seq"), Observed[0].bHasSeq);
		TestEqual(TEXT("NBIRTH is seq 0"), Observed[0].Seq, static_cast<int64>(0));
		TestTrue(TEXT("NBIRTH advertises bdSeq"),
			Observed[0].MetricNames.Contains(TEXT("bdSeq")));
		TestTrue(TEXT("NBIRTH advertises Node Control/Rebirth"),
			Observed[0].MetricNames.Contains(TEXT("Node Control/Rebirth")));

		TestEqual(TEXT("second message is DBIRTH"), Observed[1].Verb, FString(TEXT("DBIRTH")));
		TestEqual(TEXT("DBIRTH is seq 1"), Observed[1].Seq, static_cast<int64>(1));
		TestEqual(TEXT("first DBIRTH is the oven"),
			Observed[1].Device, FString(TEXT("REFLOW_OVEN")));
		TestTrue(TEXT("DBIRTH advertises the alias map"),
			Observed[1].MetricNames.Contains(TEXT("oven_temp_c")));

		TestEqual(TEXT("third message is DBIRTH"), Observed[2].Verb, FString(TEXT("DBIRTH")));
		TestEqual(TEXT("second DBIRTH is seq 2"), Observed[2].Seq, static_cast<int64>(2));
		TestEqual(TEXT("registration order preserved"),
			Observed[2].Device, FString(TEXT("SOLDER_INSP")));

		TestEqual(TEXT("fourth message is DDATA"), Observed[3].Verb, FString(TEXT("DDATA")));
		TestEqual(TEXT("DDATA continues the run at seq 3"),
			Observed[3].Seq, static_cast<int64>(3));

		Observed.Reset();
	}

	// --- rebirth restarts the run ------------------------------------------
	Node->PublishRebirth();

	TestTrue(TEXT("rebirth observed"), SpinAndPump([&Observed, &ObservedLock]()
	{
		FScopeLock Lock(&ObservedLock);
		return Observed.Num() >= 3;
	}, 10.0));

	{
		FScopeLock Lock(&ObservedLock);
		TestEqual(TEXT("rebirth starts with NBIRTH"),
			Observed[0].Verb, FString(TEXT("NBIRTH")));
		TestEqual(TEXT("rebirth resets seq to 0"), Observed[0].Seq, static_cast<int64>(0));
		TestEqual(TEXT("rebirth re-announces devices"),
			Observed[1].Seq, static_cast<int64>(1));
		Observed.Reset();
	}

	// --- clean shutdown publishes deaths ------------------------------------
	Node->Disconnect();

	SpinAndPump([&Observed, &ObservedLock]()
	{
		FScopeLock Lock(&ObservedLock);
		return Observed.Num() >= 3;
	}, 10.0);

	{
		FScopeLock Lock(&ObservedLock);

		int32 DeviceDeaths = 0;
		int32 NodeDeaths = 0;
		for (const FObserved& Entry : Observed)
		{
			if (Entry.Verb == TEXT("DDEATH")) { ++DeviceDeaths; }
			if (Entry.Verb == TEXT("NDEATH"))
			{
				++NodeDeaths;
				TestFalse(TEXT("NDEATH omits seq"), Entry.bHasSeq);
			}
		}
		TestEqual(TEXT("one DDEATH per device"), DeviceDeaths, 2);
		// Exactly one: the will must not also fire, because we sent DISCONNECT.
		TestEqual(TEXT("exactly one NDEATH, will was suppressed"), NodeDeaths, 1);
	}

	Node->RemoveFromRoot();
	Watcher->Close();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
