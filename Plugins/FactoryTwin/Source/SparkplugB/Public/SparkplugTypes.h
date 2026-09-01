#pragma once

#include "CoreMinimal.h"

#include "SparkplugTypes.generated.h"

SPARKPLUGB_API DECLARE_LOG_CATEGORY_EXTERN(LogSparkplugB, Log, All);

/**
 * Sparkplug B datatype codes (Tahu spec, appendix A).
 * The numeric values are wire-visible; do not renumber.
 */
UENUM(BlueprintType)
enum class ESparkplugDataType : uint8
{
	Unknown  = 0,
	Int8     = 1,
	Int16    = 2,
	Int32    = 3,
	Int64    = 4,
	UInt8    = 5,
	UInt16   = 6,
	UInt32   = 7,
	UInt64   = 8,
	Float    = 9,
	Double   = 10,
	Boolean  = 11,
	String   = 12,
	DateTime = 13,
	Text     = 14,
	UUID     = 15,
	DataSet  = 16,
	Bytes    = 17,
	File     = 18,
	Template = 19
};

/** The message verb, which becomes a segment of the topic. */
UENUM(BlueprintType)
enum class ESparkplugMessageType : uint8
{
	NBIRTH,
	NDEATH,
	NDATA,
	NCMD,
	DBIRTH,
	DDEATH,
	DDATA,
	DCMD,
	STATE
};

/**
 * One Sparkplug metric.
 *
 * Value storage is split by width rather than using a variant so the struct stays
 * Blueprint-exposable and lossless: routing every integer through a double would
 * corrupt anything above 2^53.  Only the field matching DataType is meaningful.
 */
USTRUCT(BlueprintType)
struct SPARKPLUGB_API FSparkplugMetric
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString Name;

	/**
	 * Wire alias established by BIRTH. 0 means "not aliased".
	 * DDATA deliberately sends name AND alias: the downstream ClickHouse bridge
	 * fans out on the name field, so alias-only messages stop ingest.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	int64 Alias = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	ESparkplugDataType DataType = ESparkplugDataType::Unknown;

	/** Milliseconds since the Unix epoch. 0 omits the field. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	int64 Timestamp = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	bool bIsNull = false;

	/** Holds Int8..UInt64, Boolean and DateTime. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	int64 IntValue = 0;

	/** Holds Float and Double. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	double DoubleValue = 0.0;

	/** Holds String, Text and UUID. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString StringValue;

	/** Holds Bytes and File. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	TArray<uint8> BytesValue;

	FSparkplugMetric() = default;

	static FSparkplugMetric MakeFloat(const FString& InName, int64 InAlias, float InValue);
	static FSparkplugMetric MakeDouble(const FString& InName, int64 InAlias, double InValue);
	static FSparkplugMetric MakeInt32(const FString& InName, int64 InAlias, int32 InValue);
	static FSparkplugMetric MakeInt64(const FString& InName, int64 InAlias, int64 InValue);
	static FSparkplugMetric MakeUInt64(const FString& InName, int64 InAlias, uint64 InValue);
	static FSparkplugMetric MakeBool(const FString& InName, int64 InAlias, bool bInValue);
	static FSparkplugMetric MakeString(const FString& InName, int64 InAlias, const FString& InValue);

	/** True when this datatype uses IntValue. */
	bool UsesIntStorage() const;
	/** True when this datatype uses DoubleValue. */
	bool UsesDoubleStorage() const;
};

/** A decoded Sparkplug payload. */
USTRUCT(BlueprintType)
struct SPARKPLUGB_API FSparkplugPayload
{
	GENERATED_BODY()

	/** Milliseconds since the Unix epoch. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	int64 Timestamp = 0;

	/** 0-255, incremented per message. NDEATH omits it entirely (Tahu 5.3). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	int64 Seq = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	bool bHasSeq = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	TArray<FSparkplugMetric> Metrics;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString UUID;
};

namespace SparkplugUtils
{
	/** Milliseconds since the Unix epoch, which is what Sparkplug timestamps use. */
	SPARKPLUGB_API int64 UtcNowMilliseconds();

	SPARKPLUGB_API FString MessageTypeToString(ESparkplugMessageType Type);
}
