#include "Misc/AutomationTest.h"
#include "MqttConnection.h"
#include "SparkplugProto.h"
#include "SparkplugTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags WillTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool SpinUntil(const TFunctionRef<bool()>& Predicate, const double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Predicate())
			{
				return true;
			}
			FPlatformProcess::Sleep(0.02f);
		}
		return Predicate();
	}
}

/**
 * Proves the Sparkplug NDEATH certificate actually fires.
 *
 * Two clients: a watcher subscribed to the NDEATH topic, and an edge node that
 * registers NDEATH as its will and then drops without sending DISCONNECT. The
 * broker must synthesise the NDEATH publish on the edge node's behalf.
 *
 * This is the capability the engine's built-in MQTT plugin does not expose, and
 * the reason MqttTransport exists.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugWillFiresTest,
	"FactoryTwin.SparkplugB.Live.WillFiresOnUngracefulDisconnect",
	WillTestFlags)

bool FSparkplugWillFiresTest::RunTest(const FString& Parameters)
{
	const FString DeathTopic = TEXT("spBv1.0/InnoLab:Essen:SMT/NDEATH/Line1");
	constexpr uint64 ExpectedBdSeq = 42;

	// --- watcher -----------------------------------------------------------
	FMqttConnectionOptions WatcherOptions;
	WatcherOptions.Host = TEXT("127.0.0.1");
	WatcherOptions.Port = 1883;
	WatcherOptions.ClientId = TEXT("factorytwin-will-watcher");
	WatcherOptions.bAutoReconnect = false;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Watcher =
		FMqttConnection::Create(WatcherOptions);

	std::atomic<bool> bWatcherConnected{ false };
	std::atomic<bool> bDeathReceived{ false };
	TArray<uint8> DeathPayloadBytes;
	FCriticalSection PayloadLock;

	Watcher->OnConnected().AddLambda([&bWatcherConnected](const EMqttConnectReturnCode Code)
	{
		if (Code == EMqttConnectReturnCode::Accepted)
		{
			bWatcherConnected = true;
		}
	});

	Watcher->OnMessage().AddLambda(
		[&bDeathReceived, &DeathPayloadBytes, &PayloadLock, DeathTopic]
		(const FMqttTransportMessage& Message)
		{
			if (Message.Topic == DeathTopic)
			{
				FScopeLock Lock(&PayloadLock);
				DeathPayloadBytes = Message.Payload;
				bDeathReceived = true;
			}
		});

	Watcher->Open();

	if (!SpinUntil([&bWatcherConnected]() { return bWatcherConnected.load(); }, 10.0))
	{
		AddWarning(TEXT("No MQTT broker on 127.0.0.1:1883 - skipping. ")
			TEXT("Start it with: cd Tools/broker && docker compose up -d"));
		Watcher->Close();
		return true;
	}

	Watcher->Subscribe({ MakeTuple(DeathTopic, EMqttQoS::AtLeastOnce) });
	// Let SUBACK land before the edge node connects, otherwise the will could
	// be published to nobody.
	FPlatformProcess::Sleep(0.5f);

	// --- edge node ---------------------------------------------------------
	FSparkplugPayload Death;
	Death.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	Death.bHasSeq = false;  // Tahu 5.3: NDEATH carries no seq
	Death.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, ExpectedBdSeq));

	FMqttConnectionOptions NodeOptions;
	NodeOptions.Host = TEXT("127.0.0.1");
	NodeOptions.Port = 1883;
	NodeOptions.ClientId = TEXT("factorytwin-will-node");
	NodeOptions.bAutoReconnect = false;
	NodeOptions.Will.bEnabled = true;
	NodeOptions.Will.Topic = DeathTopic;
	NodeOptions.Will.Payload = SparkplugProto::EncodePayload(Death);
	NodeOptions.Will.QoS = EMqttQoS::AtLeastOnce;
	NodeOptions.Will.bRetain = false;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Node =
		FMqttConnection::Create(NodeOptions);

	std::atomic<bool> bNodeConnected{ false };
	Node->OnConnected().AddLambda([&bNodeConnected](const EMqttConnectReturnCode Code)
	{
		if (Code == EMqttConnectReturnCode::Accepted)
		{
			bNodeConnected = true;
		}
	});

	Node->Open();
	TestTrue(TEXT("edge node connected"),
		SpinUntil([&bNodeConnected]() { return bNodeConnected.load(); }, 10.0));

	// Drop without DISCONNECT. This is the crash path.
	Node->CloseUngracefully();

	const bool bGotDeath =
		SpinUntil([&bDeathReceived]() { return bDeathReceived.load(); }, 10.0);
	TestTrue(TEXT("broker published NDEATH after ungraceful drop"), bGotDeath);

	if (bGotDeath)
	{
		FScopeLock Lock(&PayloadLock);

		FSparkplugPayload Decoded;
		TestTrue(TEXT("NDEATH payload decodes"),
			SparkplugProto::DecodePayload(DeathPayloadBytes, Decoded));
		TestFalse(TEXT("NDEATH omits seq"), Decoded.bHasSeq);
		TestEqual(TEXT("NDEATH carries one metric"), Decoded.Metrics.Num(), 1);

		if (Decoded.Metrics.Num() == 1)
		{
			TestEqual(TEXT("metric is bdSeq"),
				Decoded.Metrics[0].Name, FString(TEXT("bdSeq")));
			TestEqual(TEXT("bdSeq value survived the broker round trip"),
				Decoded.Metrics[0].IntValue, static_cast<int64>(ExpectedBdSeq));
		}

		UE_LOG(LogSparkplugB, Display,
			TEXT("NDEATH received: %d bytes, bdSeq=%lld"),
			DeathPayloadBytes.Num(),
			Decoded.Metrics.Num() > 0 ? Decoded.Metrics[0].IntValue : -1);
	}

	Watcher->Close();
	return true;
}

/** A graceful DISCONNECT must NOT trigger the will. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugWillSuppressedTest,
	"FactoryTwin.SparkplugB.Live.WillSuppressedOnCleanDisconnect",
	WillTestFlags)

bool FSparkplugWillSuppressedTest::RunTest(const FString& Parameters)
{
	const FString DeathTopic = TEXT("spBv1.0/InnoLab:Essen:SMT/NDEATH/CleanExit");

	FMqttConnectionOptions WatcherOptions;
	WatcherOptions.Host = TEXT("127.0.0.1");
	WatcherOptions.Port = 1883;
	WatcherOptions.ClientId = TEXT("factorytwin-clean-watcher");
	WatcherOptions.bAutoReconnect = false;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Watcher =
		FMqttConnection::Create(WatcherOptions);

	std::atomic<bool> bWatcherConnected{ false };
	std::atomic<bool> bDeathReceived{ false };

	Watcher->OnConnected().AddLambda([&bWatcherConnected](const EMqttConnectReturnCode Code)
	{
		if (Code == EMqttConnectReturnCode::Accepted) { bWatcherConnected = true; }
	});
	Watcher->OnMessage().AddLambda([&bDeathReceived, DeathTopic](const FMqttTransportMessage& Message)
	{
		if (Message.Topic == DeathTopic) { bDeathReceived = true; }
	});

	Watcher->Open();
	if (!SpinUntil([&bWatcherConnected]() { return bWatcherConnected.load(); }, 10.0))
	{
		AddWarning(TEXT("No MQTT broker on 127.0.0.1:1883 - skipping."));
		Watcher->Close();
		return true;
	}

	Watcher->Subscribe({ MakeTuple(DeathTopic, EMqttQoS::AtLeastOnce) });
	FPlatformProcess::Sleep(0.5f);

	FSparkplugPayload Death;
	Death.bHasSeq = false;
	Death.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 1));

	FMqttConnectionOptions NodeOptions;
	NodeOptions.Host = TEXT("127.0.0.1");
	NodeOptions.Port = 1883;
	NodeOptions.ClientId = TEXT("factorytwin-clean-node");
	NodeOptions.bAutoReconnect = false;
	NodeOptions.Will.bEnabled = true;
	NodeOptions.Will.Topic = DeathTopic;
	NodeOptions.Will.Payload = SparkplugProto::EncodePayload(Death);
	NodeOptions.Will.QoS = EMqttQoS::AtLeastOnce;

	const TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Node =
		FMqttConnection::Create(NodeOptions);

	std::atomic<bool> bNodeConnected{ false };
	Node->OnConnected().AddLambda([&bNodeConnected](const EMqttConnectReturnCode Code)
	{
		if (Code == EMqttConnectReturnCode::Accepted) { bNodeConnected = true; }
	});

	Node->Open();
	TestTrue(TEXT("edge node connected"),
		SpinUntil([&bNodeConnected]() { return bNodeConnected.load(); }, 10.0));

	// Graceful shutdown sends DISCONNECT, which cancels the will.
	Node->Close();

	// Give the broker ample time to (incorrectly) deliver a will.
	FPlatformProcess::Sleep(2.0f);
	TestFalse(TEXT("no NDEATH after a clean DISCONNECT"), bDeathReceived.load());

	Watcher->Close();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
