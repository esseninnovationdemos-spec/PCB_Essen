#include "Misc/AutomationTest.h"
#include "SparkplugJson.h"
#include "SparkplugProto.h"
#include "SparkplugTypes.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString AsText(const TArray<uint8>& Utf8)
	{
		const auto Converted = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Utf8.GetData()), Utf8.Num());
		return FString(Converted.Length(), Converted.Get());
	}

	TArray<uint8> AsBytes(const FString& Text)
	{
		const auto Utf8 = StringCast<UTF8CHAR>(*Text, Text.Len());
		return TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	FSparkplugMetric Make(const TCHAR* Name, const ESparkplugDataType Type)
	{
		FSparkplugMetric Metric;
		Metric.Name = Name;
		Metric.DataType = Type;
		Metric.Timestamp = 1760000000123;
		return Metric;
	}

	/** One of every supported datatype, at the values most likely to break a codec. */
	FSparkplugPayload MakeEveryType()
	{
		FSparkplugPayload Payload;
		Payload.Timestamp = 1760000000000;
		Payload.Seq = 255;
		Payload.bHasSeq = true;
		Payload.UUID = TEXT("factory-twin");

		auto Add = [&Payload](FSparkplugMetric Metric) { Payload.Metrics.Add(MoveTemp(Metric)); };

		{ FSparkplugMetric M = Make(TEXT("i8"),  ESparkplugDataType::Int8);   M.IntValue = -128;               Add(M); }
		{ FSparkplugMetric M = Make(TEXT("i16"), ESparkplugDataType::Int16);  M.IntValue = -32768;             Add(M); }
		{ FSparkplugMetric M = Make(TEXT("i32"), ESparkplugDataType::Int32);  M.IntValue = MIN_int32;          Add(M); }
		{ FSparkplugMetric M = Make(TEXT("i64"), ESparkplugDataType::Int64);  M.IntValue = MIN_int64;          Add(M); }
		{ FSparkplugMetric M = Make(TEXT("u8"),  ESparkplugDataType::UInt8);  M.IntValue = 255;                Add(M); }
		{ FSparkplugMetric M = Make(TEXT("u32"), ESparkplugDataType::UInt32); M.IntValue = MAX_uint32;         Add(M); }
		// 2^53 + 1: the first integer a double cannot hold.
		{ FSparkplugMetric M = Make(TEXT("big"), ESparkplugDataType::Int64);  M.IntValue = 9007199254740993LL; Add(M); }
		// All bits set: only survives if written unsigned.
		{ FSparkplugMetric M = Make(TEXT("u64"), ESparkplugDataType::UInt64); M.IntValue = static_cast<int64>(MAX_uint64); Add(M); }
		{ FSparkplugMetric M = Make(TEXT("dt"),  ESparkplugDataType::DateTime); M.IntValue = 1760000000999;    Add(M); }
		{ FSparkplugMetric M = Make(TEXT("f"),   ESparkplugDataType::Float);  M.DoubleValue = static_cast<double>(0.1f); Add(M); }
		{ FSparkplugMetric M = Make(TEXT("d"),   ESparkplugDataType::Double); M.DoubleValue = 0.1;             Add(M); }
		{ FSparkplugMetric M = Make(TEXT("tiny"),ESparkplugDataType::Double); M.DoubleValue = 5e-324;          Add(M); }
		{ FSparkplugMetric M = Make(TEXT("b"),   ESparkplugDataType::Boolean); M.IntValue = 1;                 Add(M); }
		// Quotes, a backslash, a control character and non-ASCII: everything escaping can get wrong.
		{ FSparkplugMetric M = Make(TEXT("s"),   ESparkplugDataType::String); M.StringValue = TEXT("Grüße \"Linie\" \\ 1\n\t€"); Add(M); }
		{ FSparkplugMetric M = Make(TEXT("u"),   ESparkplugDataType::UUID);   M.StringValue = TEXT("0b35b6e0-0000-4000-8000-000000000001"); Add(M); }
		{ FSparkplugMetric M = Make(TEXT("raw"), ESparkplugDataType::Bytes);  M.BytesValue = { 0x00, 0x7B, 0xFF, 0x0A }; Add(M); }
		{ FSparkplugMetric M = Make(TEXT("nul"), ESparkplugDataType::Int32);  M.bIsNull = true;                Add(M); }
		{ FSparkplugMetric M = Make(TEXT("al"),  ESparkplugDataType::Int32);  M.Alias = 4294967296LL; M.IntValue = 7; Add(M); }

		return Payload;
	}

	void ExpectSame(FAutomationTestBase& Test, const FString& Context,
		const FSparkplugPayload& Expected, const FSparkplugPayload& Actual)
	{
		Test.TestEqual(Context + TEXT(" timestamp"), Actual.Timestamp, Expected.Timestamp);
		Test.TestEqual(Context + TEXT(" bHasSeq"), Actual.bHasSeq, Expected.bHasSeq);
		if (Expected.bHasSeq)
		{
			Test.TestEqual(Context + TEXT(" seq"), Actual.Seq, Expected.Seq);
		}
		Test.TestEqual(Context + TEXT(" uuid"), Actual.UUID, Expected.UUID);

		if (!Test.TestEqual(Context + TEXT(" metric count"), Actual.Metrics.Num(), Expected.Metrics.Num()))
		{
			return;
		}

		for (int32 Index = 0; Index < Expected.Metrics.Num(); ++Index)
		{
			const FSparkplugMetric& E = Expected.Metrics[Index];
			const FSparkplugMetric& A = Actual.Metrics[Index];
			const FString Where = FString::Printf(TEXT("%s '%s'"), *Context, *E.Name);

			Test.TestEqual(Where + TEXT(" name"), A.Name, E.Name);
			Test.TestEqual(Where + TEXT(" alias"), A.Alias, E.Alias);
			Test.TestEqual(Where + TEXT(" timestamp"), A.Timestamp, E.Timestamp);
			Test.TestEqual(Where + TEXT(" datatype"), A.DataType, E.DataType);
			Test.TestEqual(Where + TEXT(" null"), A.bIsNull, E.bIsNull);
			if (E.bIsNull)
			{
				continue;
			}
			if (E.UsesIntStorage())
			{
				Test.TestEqual(Where + TEXT(" int"), A.IntValue, E.IntValue);
			}
			else if (E.UsesDoubleStorage())
			{
				// Exact, not nearly-equal: the formatter promises a round trip.
				Test.TestTrue(Where + TEXT(" real exact"), A.DoubleValue == E.DoubleValue);
			}
			else
			{
				Test.TestEqual(Where + TEXT(" string"), A.StringValue, E.StringValue);
				Test.TestEqual(Where + TEXT(" bytes"), A.BytesValue, E.BytesValue);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Known text. A round trip alone would pass an encoder and decoder that agree
// on the wrong shape; this pins the shape a third-party consumer will parse.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonKnownTextTest,
	"FactoryTwin.SparkplugB.Json.KnownText",
	TestFlags)

bool FSparkplugJsonKnownTextTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Payload;
	Payload.Timestamp = 1000;
	Payload.Seq = 5;
	Payload.bHasSeq = true;

	FSparkplugMetric Metric;
	Metric.Name = TEXT("a");
	Metric.Alias = 1;
	Metric.DataType = ESparkplugDataType::Boolean;
	Metric.Timestamp = 0;  // omitted when zero, as in protobuf
	Metric.IntValue = 1;
	Payload.Metrics.Add(Metric);

	TestEqual(TEXT("payload text"),
		AsText(SparkplugJson::EncodePayload(Payload)),
		FString(TEXT("{\"timestamp\":1000,\"seq\":5,\"metrics\":[{\"name\":\"a\",\"alias\":1,\"dataType\":\"Boolean\",\"value\":true}]}")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonRoundTripTest,
	"FactoryTwin.SparkplugB.Json.RoundTripEveryType",
	TestFlags)

bool FSparkplugJsonRoundTripTest::RunTest(const FString& Parameters)
{
	const FSparkplugPayload Original = MakeEveryType();

	FSparkplugPayload Decoded;
	if (!TestTrue(TEXT("decodes"), SparkplugJson::DecodePayload(SparkplugJson::EncodePayload(Original), Decoded)))
	{
		return false;
	}
	ExpectSame(*this, TEXT("json"), Original, Decoded);
	return true;
}

// ---------------------------------------------------------------------------
// The two encodings must say the same thing. Encode once each way, decode
// both, and require identical structs -- otherwise Both mode publishes two
// streams that disagree.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonMatchesProtoTest,
	"FactoryTwin.SparkplugB.Json.MatchesProtobuf",
	TestFlags)

bool FSparkplugJsonMatchesProtoTest::RunTest(const FString& Parameters)
{
	const FSparkplugPayload Original = MakeEveryType();

	FSparkplugPayload ViaProto;
	FSparkplugPayload ViaJson;
	TestTrue(TEXT("proto decodes"), SparkplugProto::DecodePayload(SparkplugProto::EncodePayload(Original), ViaProto));
	TestTrue(TEXT("json decodes"), SparkplugJson::DecodePayload(SparkplugJson::EncodePayload(Original), ViaJson));

	ExpectSame(*this, TEXT("proto vs json"), ViaProto, ViaJson);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonDeathTest,
	"FactoryTwin.SparkplugB.Json.DeathHasNoSeq",
	TestFlags)

bool FSparkplugJsonDeathTest::RunTest(const FString& Parameters)
{
	// Tahu 5.3: NDEATH carries no seq. Absent must stay absent, not become 0.
	FSparkplugPayload Death;
	Death.Timestamp = 1000;
	Death.bHasSeq = false;
	Death.Metrics.Add(FSparkplugMetric::MakeUInt64(TEXT("bdSeq"), 0, 3));

	const TArray<uint8> Encoded = SparkplugJson::EncodePayload(Death);
	TestFalse(TEXT("no seq key"), AsText(Encoded).Contains(TEXT("\"seq\"")));

	FSparkplugPayload Decoded;
	TestTrue(TEXT("decodes"), SparkplugJson::DecodePayload(Encoded, Decoded));
	TestFalse(TEXT("bHasSeq false"), Decoded.bHasSeq);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonNonFiniteTest,
	"FactoryTwin.SparkplugB.Json.NonFinite",
	TestFlags)

bool FSparkplugJsonNonFiniteTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Payload;
	Payload.Metrics.Add(FSparkplugMetric::MakeDouble(TEXT("nan"), 0, std::numeric_limits<double>::quiet_NaN()));
	Payload.Metrics.Add(FSparkplugMetric::MakeDouble(TEXT("inf"), 0, std::numeric_limits<double>::infinity()));
	Payload.Metrics.Add(FSparkplugMetric::MakeFloat(TEXT("ninf"), 0, -std::numeric_limits<float>::infinity()));

	FSparkplugPayload Decoded;
	if (!TestTrue(TEXT("decodes"), SparkplugJson::DecodePayload(SparkplugJson::EncodePayload(Payload), Decoded))
		|| !TestEqual(TEXT("count"), Decoded.Metrics.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("NaN"), FMath::IsNaN(Decoded.Metrics[0].DoubleValue));
	TestTrue(TEXT("+Inf"), Decoded.Metrics[1].DoubleValue == std::numeric_limits<double>::infinity());
	TestTrue(TEXT("-Inf"), Decoded.Metrics[2].DoubleValue == -std::numeric_limits<double>::infinity());
	return true;
}

// ---------------------------------------------------------------------------
// Content sniffing is what lets one subscription serve both encodings, so it
// must never misread a protobuf payload as JSON.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonDetectTest,
	"FactoryTwin.SparkplugB.Json.Detection",
	TestFlags)

bool FSparkplugJsonDetectTest::RunTest(const FString& Parameters)
{
	const FSparkplugPayload Payload = MakeEveryType();

	const TArray<uint8> Proto = SparkplugProto::EncodePayload(Payload);
	const TArray<uint8> Json = SparkplugJson::EncodePayload(Payload);

	TestFalse(TEXT("protobuf is not JSON"), SparkplugJson::LooksLikeJson(Proto));
	TestTrue(TEXT("JSON is JSON"), SparkplugJson::LooksLikeJson(Json));
	TestTrue(TEXT("leading whitespace"), SparkplugJson::LooksLikeJson(AsBytes(TEXT("\r\n {\"metrics\":[]}"))));

	// Every payload the edge node can emit with no timestamp starts on a later
	// field; check those openings too, not just the common one.
	FSparkplugPayload SeqOnly;
	SeqOnly.Seq = 123;
	TestFalse(TEXT("seq-first protobuf"), SparkplugJson::LooksLikeJson(SparkplugProto::EncodePayload(SeqOnly)));
	FSparkplugPayload MetricsOnly;
	MetricsOnly.bHasSeq = false;
	MetricsOnly.Metrics.Add(FSparkplugMetric::MakeBool(TEXT("x"), 0, true));
	TestFalse(TEXT("metrics-first protobuf"), SparkplugJson::LooksLikeJson(SparkplugProto::EncodePayload(MetricsOnly)));

	FSparkplugPayload FromProto;
	FSparkplugPayload FromJson;
	TestTrue(TEXT("codec decodes protobuf"), SparkplugCodec::DecodePayload(Proto, FromProto));
	TestTrue(TEXT("codec decodes JSON"), SparkplugCodec::DecodePayload(Json, FromJson));
	ExpectSame(*this, TEXT("auto-detected"), FromProto, FromJson);
	return true;
}

// ---------------------------------------------------------------------------
// Inbound JSON comes from producers we do not control. Accept the reasonable
// variations; reject what cannot be interpreted.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonLenientTest,
	"FactoryTwin.SparkplugB.Json.LenientDecode",
	TestFlags)

bool FSparkplugJsonLenientTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Decoded;
	const bool bOk = SparkplugJson::DecodePayload(AsBytes(TEXT(
		"{\"timestamp\":1000,\"seq\":1,\"metrics\":["
		"{\"name\":\"code\",\"dataType\":3,\"value\":7},"          // numeric datatype code
		"{\"name\":\"case\",\"dataType\":\"int32\",\"value\":8},"  // lower-case name
		"{\"name\":\"text\",\"dataType\":\"Int32\",\"value\":\"9\"},"   // number in a string
		"{\"name\":\"real\",\"dataType\":\"Int32\",\"value\":10.0},"    // integral real
		"{\"name\":\"bit\",\"dataType\":\"Boolean\",\"value\":1},"      // 0/1 boolean
		"{\"name\":\"ib\",\"value\":true},"                        // no datatype: inferred
		"{\"name\":\"ii\",\"value\":11},"
		"{\"name\":\"id\",\"value\":1.5},"
		"{\"name\":\"is\",\"value\":\"x\"},"
		"{\"name\":\"extra\",\"dataType\":\"Int32\",\"value\":12,\"properties\":{},\"isHistorical\":false}"
		"]}")), Decoded);

	if (!TestTrue(TEXT("decodes"), bOk) || !TestEqual(TEXT("count"), Decoded.Metrics.Num(), 10))
	{
		return false;
	}

	TestEqual(TEXT("numeric code"),  Decoded.Metrics[0].IntValue, int64(7));
	TestEqual(TEXT("lower-case"),    Decoded.Metrics[1].IntValue, int64(8));
	TestEqual(TEXT("string number"), Decoded.Metrics[2].IntValue, int64(9));
	TestEqual(TEXT("integral real"), Decoded.Metrics[3].IntValue, int64(10));
	TestEqual(TEXT("0/1 boolean"),   Decoded.Metrics[4].IntValue, int64(1));
	TestEqual(TEXT("inferred bool"),   Decoded.Metrics[5].DataType, ESparkplugDataType::Boolean);
	TestEqual(TEXT("inferred int"),    Decoded.Metrics[6].DataType, ESparkplugDataType::Int64);
	TestEqual(TEXT("inferred double"), Decoded.Metrics[7].DataType, ESparkplugDataType::Double);
	TestEqual(TEXT("inferred string"), Decoded.Metrics[8].DataType, ESparkplugDataType::String);
	TestEqual(TEXT("unknown keys ignored"), Decoded.Metrics[9].IntValue, int64(12));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSparkplugJsonMalformedTest,
	"FactoryTwin.SparkplugB.Json.RejectsMalformed",
	TestFlags)

bool FSparkplugJsonMalformedTest::RunTest(const FString& Parameters)
{
	FSparkplugPayload Payload;
	TestFalse(TEXT("empty"),            SparkplugJson::DecodePayload(TArray<uint8>(), Payload));
	TestFalse(TEXT("not JSON"),         SparkplugJson::DecodePayload(AsBytes(TEXT("{nope")), Payload));
	TestFalse(TEXT("array root"),       SparkplugJson::DecodePayload(AsBytes(TEXT("[1,2]")), Payload));
	TestFalse(TEXT("metrics not array"),SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":{}}")), Payload));
	TestFalse(TEXT("metric not object"),SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":[1]}")), Payload));
	TestFalse(TEXT("unknown datatype"), SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":[{\"name\":\"x\",\"dataType\":\"Quaternion\",\"value\":1}]}")), Payload));
	TestFalse(TEXT("value wrong type"), SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":[{\"name\":\"x\",\"dataType\":\"Int32\",\"value\":\"abc\"}]}")), Payload));
	TestFalse(TEXT("fractional int"),   SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":[{\"name\":\"x\",\"dataType\":\"Int32\",\"value\":1.5}]}")), Payload));
	TestFalse(TEXT("bad base64"),       SparkplugJson::DecodePayload(AsBytes(TEXT("{\"metrics\":[{\"name\":\"x\",\"dataType\":\"Bytes\",\"value\":\"***\"}]}")), Payload));

	TestTrue(TEXT("no metrics key is a valid DDEATH"),
		SparkplugJson::DecodePayload(AsBytes(TEXT("{\"timestamp\":1}")), Payload));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
