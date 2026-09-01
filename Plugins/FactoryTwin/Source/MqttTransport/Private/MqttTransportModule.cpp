#include "Modules/ModuleManager.h"
#include "MqttTransportTypes.h"

DEFINE_LOG_CATEGORY(LogMqttTransport);

FString FMqttTransportMessage::GetPayloadAsString() const
{
	if (Payload.Num() == 0)
	{
		return FString();
	}

	// Bounded conversion: the payload is not null-terminated, and for binary
	// payloads this will produce garbage by design. Callers handling protobuf
	// must read Payload directly. FString(const TCHAR*, int32) would treat the
	// second argument as extra slack and read past the end, so size explicitly.
	const FUTF8ToTCHAR Converted(
		reinterpret_cast<const ANSICHAR*>(Payload.GetData()), Payload.Num());
	return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
}

IMPLEMENT_MODULE(FDefaultModuleImpl, MqttTransport)
