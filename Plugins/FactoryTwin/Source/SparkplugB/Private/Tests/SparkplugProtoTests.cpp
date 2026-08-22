#include "Misc/AutomationTest.h"
#include "SparkplugProto.h"
#include "SparkplugTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// ---------------------------------------------------------------------------
// Known-byte vectors. These are hand-computed from the protobuf encoding spec,
// so they catch a systematic error that a round-trip test would miss (an
// encoder and decoder that are wrong in the same direction still round-trip).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugKnownBytesTest,
	"FactoryTwin.SparkplugB.Proto.KnownBytes",
	TestFlags)

bool FSparkplugKnownBytesTest::RunTest(const FString& Parameters)
{
	{
		// Payload { timestamp: 1000, seq: 5 }
		//   field 1 varint -> tag 0x08, 1000 = 0xE8 0x07
		//   field 3 varint -> tag 0x18, 5    = 0x05
		FSparkplugPayload Payload;
		Payload.Timestamp = 1000;
		Payload.Seq = 5;
		Payload.bHasSeq = true;

		const TArray<uint8> Expected = { 0x08, 0xE8, 0x07, 0x18, 0x05 };
		TestEqual(TEXT("payload header bytes"), SparkplugProto::EncodePayload(Payload), Expected);
	}

	{
		// Metric { name: "a", alias: 1, datatype: 11 (Boolean), boolean_value: true }
		//   field 1  len-delim -> 0x0A 0x01 'a'
		//   field 2  varint    -> 0x10 0x01
		//   field 4  varint    -> 0x20 0x0B
		//   field 14 varint    -> 0x70 0x01
		FSparkplugMetric Metric;
		Metric.Name = TEXT("a");
		Metric.Alias = 1;
		Metric.DataType = ESparkplugDataType::Boolean;
		Metric.Timestamp = 0;  // omitted when zero
		Metric.IntValue = 1;

		const TArray<uint8> Expected = { 0x0A, 0x01, 0x61, 0x10, 0x01, 0x20, 0x0B, 0x70, 0x01 };
		TestEqual(TEXT("metric bytes"), SparkplugProto::EncodeMetric(Metric), Expected);
	}

	return true;
}

// ---------------------------------------------------------------------------
// NDEATH must omit seq entirely (Tahu 5.3), not send seq=0.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugNDeathOmitsSeqTest,
	"FactoryTwin.SparkplugB.Proto.NDeathOmitsSeq",
	TestFlags)

bool FSparkplugNDeathOmitsSeqTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Payload;
	Payload.Timestamp = 0;
	Payload.bHasSeq = false;
	Payload.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 7));
	Payload.Metrics[0].Timestamp = 0;

	const TArray<uint8> Encoded = SparkplugProto::EncodePayload(Payload);

	// Field 3 (seq) has tag byte 0x18. It must not appear at the top level.
	// The metric is nested inside a length-delimited field 2, so scanning for
	// the tag at top level is only valid because we decode to confirm.
	FSparkplugPayload Decoded;
	TestTrue(TEXT("decodes"), SparkplugProto::DecodePayload(Encoded, Decoded));
	TestFalse(TEXT("seq absent"), Decoded.bHasSeq);
	TestEqual(TEXT("bdSeq metric present"), Decoded.Metrics.Num(), 1);
	TestEqual(TEXT("bdSeq name"), Decoded.Metrics[0].Name, FString(TEXT("bdSeq")));
	TestEqual(TEXT("bdSeq value"), Decoded.Metrics[0].IntValue, static_cast<int64>(7));

	// And a payload that does carry seq must round-trip it, including 0.
	FSparkplugPayload WithZeroSeq;
	WithZeroSeq.bHasSeq = true;
	WithZeroSeq.Seq = 0;
	FSparkplugPayload DecodedZero;
	TestTrue(TEXT("zero-seq decodes"),
		SparkplugProto::DecodePayload(SparkplugProto::EncodePayload(WithZeroSeq), DecodedZero));
	TestTrue(TEXT("zero seq is present, not omitted"), DecodedZero.bHasSeq);
	TestEqual(TEXT("zero seq value"), DecodedZero.Seq, static_cast<int64>(0));

	return true;
}

// ---------------------------------------------------------------------------
// Every datatype the factory config actually uses.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugDataTypeRoundTripTest,
	"FactoryTwin.SparkplugB.Proto.DataTypeRoundTrip",
	TestFlags)

bool FSparkplugDataTypeRoundTripTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Payload;
	Payload.Timestamp = 1700000000123;
	Payload.Seq = 255;
	Payload.bHasSeq = true;

	Payload.Metrics.Add(FSparkplugMetric::MakeFloat(TEXT("oven_temp_c"), 23, 241.75f));
	Payload.Metrics.Add(FSparkplugMetric::MakeInt32(TEXT("state_code"), 26, 1));
	Payload.Metrics.Add(FSparkplugMetric::MakeString(TEXT("event_type"), 32, TEXT("CYCLE_COMPLETE")));
	Payload.Metrics.Add(FSparkplugMetric::MakeBool(TEXT("new_material"), 0, true));
	Payload.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 12345678901234ULL));
	Payload.Metrics.Add(FSparkplugMetric::MakeDouble(TEXT("precise"), 0, 0.1 + 0.2));
	// Negative Int32 rides in the unsigned int_value field as two's complement.
	Payload.Metrics.Add(FSparkplugMetric::MakeInt32(TEXT("offset"), 0, -42));

	FSparkplugPayload Decoded;
	TestTrue(TEXT("decodes"),
		SparkplugProto::DecodePayload(SparkplugProto::EncodePayload(Payload), Decoded));

	TestEqual(TEXT("timestamp"), Decoded.Timestamp, Payload.Timestamp);
	TestEqual(TEXT("seq"), Decoded.Seq, static_cast<int64>(255));
	TestEqual(TEXT("metric count"), Decoded.Metrics.Num(), Payload.Metrics.Num());

	// Float is 32-bit on the wire, so compare at float precision.
	TestEqual(TEXT("float value"),
		static_cast<float>(Decoded.Metrics[0].DoubleValue), 241.75f);
	TestEqual(TEXT("float alias"), Decoded.Metrics[0].Alias, static_cast<int64>(23));
	TestEqual(TEXT("float name survives"),
		Decoded.Metrics[0].Name, FString(TEXT("oven_temp_c")));

	TestEqual(TEXT("int32"), Decoded.Metrics[1].IntValue, static_cast<int64>(1));
	TestEqual(TEXT("string"), Decoded.Metrics[2].StringValue, FString(TEXT("CYCLE_COMPLETE")));
	TestEqual(TEXT("bool"), Decoded.Metrics[3].IntValue, static_cast<int64>(1));
	TestEqual(TEXT("uint64 beyond 2^32"),
		Decoded.Metrics[4].IntValue, static_cast<int64>(12345678901234ULL));
	// Double must be bit-exact, unlike float.
	TestEqual(TEXT("double keeps full precision"), Decoded.Metrics[5].DoubleValue, 0.1 + 0.2);
	TestEqual(TEXT("negative int32"), Decoded.Metrics[6].IntValue, static_cast<int64>(-42));

	return true;
}

// ---------------------------------------------------------------------------
// Varint boundaries and unknown-field tolerance.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugVarintTest,
	"FactoryTwin.SparkplugB.Proto.Varint",
	TestFlags)

bool FSparkplugVarintTest::RunTest(const FString& Parameters)
{
	const uint64 Values[] = {
		0, 1, 127, 128, 255, 300, 16383, 16384,
		0x7FFFFFFFULL, 0x80000000ULL, 0xFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
	};

	for (const uint64 Value : Values)
	{
		SparkplugProto::FWriter Writer;
		Writer.WriteVarint(Value);

		SparkplugProto::FReader Reader(Writer.GetBuffer());
		uint64 Decoded = 0;
		TestTrue(FString::Printf(TEXT("decode %llu"), Value), Reader.ReadVarint(Decoded));
		TestEqual(FString::Printf(TEXT("round trip %llu"), Value), Decoded, Value);
		TestTrue(TEXT("consumed fully"), Reader.IsAtEnd());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugUnknownFieldTest,
	"FactoryTwin.SparkplugB.Proto.UnknownFieldsAreSkipped",
	TestFlags)

bool FSparkplugUnknownFieldTest::RunTest(const FString& Parameters)
{
	// Simulate a producer using metric fields we do not model (properties,
	// metadata). Decoding must skip them rather than fail, so a richer upstream
	// implementation stays readable.
	SparkplugProto::FWriter Writer;
	Writer.WriteStringField(SparkplugProto::MetricField::Name, TEXT("m"));
	Writer.WriteUInt32Field(SparkplugProto::MetricField::DataType,
		static_cast<uint32>(ESparkplugDataType::Int32));
	// Field 9 (properties) as a nested message we do not understand.
	Writer.WriteBytesField(SparkplugProto::MetricField::Properties, { 0x01, 0x02, 0x03 });
	Writer.WriteUInt32Field(SparkplugProto::MetricField::IntValue, 99);

	const TArray<uint8> Encoded = Writer.GetBuffer();

	FSparkplugMetric Metric;
	TestTrue(TEXT("decodes despite unknown field"),
		SparkplugProto::DecodeMetric(Encoded.GetData(), Encoded.Num(), Metric));
	TestEqual(TEXT("name"), Metric.Name, FString(TEXT("m")));
	TestEqual(TEXT("value read past the skipped field"),
		Metric.IntValue, static_cast<int64>(99));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugMalformedInputTest,
	"FactoryTwin.SparkplugB.Proto.MalformedInputIsRejected",
	TestFlags)

bool FSparkplugMalformedInputTest::RunTest(const FString& Parameters)
{
	// A length-delimited field claiming more bytes than remain must fail rather
	// than read out of bounds. This is the path untrusted broker traffic takes.
	const TArray<uint8> Truncated = { 0x0A, 0x7F, 0x61 };
	FSparkplugMetric Metric;
	TestFalse(TEXT("truncated string rejected"),
		SparkplugProto::DecodeMetric(Truncated.GetData(), Truncated.Num(), Metric));

	// Field number 0 is invalid in protobuf.
	const TArray<uint8> ZeroField = { 0x00, 0x01 };
	FSparkplugPayload Payload;
	TestFalse(TEXT("zero field number rejected"),
		SparkplugProto::DecodePayload(ZeroField, Payload));

	// A varint with the continuation bit set forever must terminate.
	const TArray<uint8> RunawayVarint = {
		0x08, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
	TestFalse(TEXT("runaway varint rejected"),
		SparkplugProto::DecodePayload(RunawayVarint, Payload));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
