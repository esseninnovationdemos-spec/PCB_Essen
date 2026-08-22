#include "MqttTransportClient.h"

#include "Async/TaskGraphInterfaces.h"
#include "MqttConnection.h"

void UMqttTransportClient::Connect(const FMqttConnectionOptions& InOptions)
{
	if (Connection.IsValid())
	{
		UE_LOG(LogMqttTransport, Warning,
			TEXT("MQTT client is already connected; call Disconnect first"));
		return;
	}

	Options = InOptions;
	Connection = FMqttConnection::Create(Options);
	if (WillPayloadProvider.IsBound())
	{
		Connection->SetWillPayloadProvider(WillPayloadProvider);
	}
	BindConnectionDelegates();
	Connection->Open();
}

void UMqttTransportClient::SetWillPayloadProvider(const FMqttWillPayloadProvider& Provider)
{
	WillPayloadProvider = Provider;
	// Allow installing it after Connect too, so ordering is not a trap.
	if (Connection.IsValid())
	{
		Connection->SetWillPayloadProvider(Provider);
	}
}

void UMqttTransportClient::BindConnectionDelegates()
{
	// The connection fires these on its worker thread. Hop to the game thread
	// before touching this UObject or broadcasting to Blueprint, and use a weak
	// pointer so a queued hop cannot resurrect a destroyed client.
	TWeakObjectPtr<UMqttTransportClient> WeakThis(this);

	Connection->OnConnected().AddLambda([WeakThis](const EMqttConnectReturnCode ReturnCode)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ReturnCode]()
		{
			if (UMqttTransportClient* Client = WeakThis.Get())
			{
				Client->OnConnected.Broadcast(ReturnCode);
			}
		});
	});

	Connection->OnDisconnected().AddLambda([WeakThis]()
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (UMqttTransportClient* Client = WeakThis.Get())
			{
				Client->OnDisconnected.Broadcast();
			}
		});
	});

	Connection->OnMessage().AddLambda([WeakThis](const FMqttTransportMessage& Message)
	{
		// Copy: the worker's buffer is reused as soon as this returns.
		FMqttTransportMessage Copy = Message;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]()
		{
			if (UMqttTransportClient* Client = WeakThis.Get())
			{
				Client->OnMessage.Broadcast(Copy);
			}
		});
	});
}

void UMqttTransportClient::Disconnect()
{
	if (!Connection.IsValid())
	{
		return;
	}

	Connection->Close();
	Connection.Reset();
}

bool UMqttTransportClient::PublishBytes(
	const FString& Topic, const TArray<uint8>& Payload, const EMqttQoS QoS, const bool bRetain)
{
	if (!Connection.IsValid())
	{
		UE_LOG(LogMqttTransport, Warning, TEXT("Publish to '%s' dropped: no connection"), *Topic);
		return false;
	}
	return Connection->Publish(Topic, Payload, QoS, bRetain);
}

bool UMqttTransportClient::PublishString(
	const FString& Topic, const FString& Payload, const EMqttQoS QoS, const bool bRetain)
{
	const FTCHARToUTF8 Converted(*Payload);

	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	return PublishBytes(Topic, Bytes, QoS, bRetain);
}

bool UMqttTransportClient::Subscribe(const FString& TopicFilter, const EMqttQoS QoS)
{
	if (!Connection.IsValid())
	{
		return false;
	}

	TArray<TPair<FString, EMqttQoS>> Subscriptions;
	Subscriptions.Emplace(TopicFilter, QoS);
	return Connection->Subscribe(Subscriptions);
}

bool UMqttTransportClient::Unsubscribe(const FString& TopicFilter)
{
	if (!Connection.IsValid())
	{
		return false;
	}
	return Connection->Unsubscribe({ TopicFilter });
}

bool UMqttTransportClient::IsConnected() const
{
	return Connection.IsValid() && Connection->IsConnected();
}

EMqttConnectionState UMqttTransportClient::GetConnectionState() const
{
	return Connection.IsValid() ? Connection->GetState() : EMqttConnectionState::Disconnected;
}

FString UMqttTransportClient::GetClientId() const
{
	return Connection.IsValid() ? Connection->GetOptions().ClientId : Options.ClientId;
}

void UMqttTransportClient::BeginDestroy()
{
	// Join the worker before the UObject goes away, otherwise a queued game-thread
	// hop could outlive us. The weak pointer guards that too, but joining here
	// also guarantees the socket is closed deterministically.
	if (Connection.IsValid())
	{
		Connection->Close();
		Connection.Reset();
	}

	Super::BeginDestroy();
}

// ---------------------------------------------------------------------------
// UMqttTransportSubsystem
// ---------------------------------------------------------------------------

UMqttTransportClient* UMqttTransportSubsystem::CreateClient()
{
	UMqttTransportClient* Client = NewObject<UMqttTransportClient>(this);
	Clients.Add(Client);
	return Client;
}

void UMqttTransportSubsystem::DestroyClient(UMqttTransportClient* Client)
{
	if (Client == nullptr)
	{
		return;
	}

	Client->Disconnect();
	Clients.Remove(Client);
}

void UMqttTransportSubsystem::Deinitialize()
{
	// Close every socket deterministically on PIE stop rather than leaving it to GC.
	for (const TObjectPtr<UMqttTransportClient>& Client : Clients)
	{
		if (Client != nullptr)
		{
			Client->Disconnect();
		}
	}
	Clients.Reset();

	Super::Deinitialize();
}
