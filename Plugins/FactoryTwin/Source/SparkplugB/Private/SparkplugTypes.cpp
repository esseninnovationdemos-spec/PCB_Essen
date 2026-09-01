#include "SparkplugTypes.h"

namespace
{
	/** Fills the fields every factory helper sets identically. */
	FSparkplugMetric MakeBase(
		const FString& InName, const int64 InAlias, const ESparkplugDataType InType)
	{
		FSparkplugMetric Metric;
		Metric.Name = InName;
		Metric.Alias = InAlias;
		Metric.DataType = InType;
		Metric.Timestamp = SparkplugUtils::UtcNowMilliseconds();
		return Metric;
	}
}

FSparkplugMetric FSparkplugMetric::MakeFloat(const FString& InName, const int64 InAlias, const float InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::Float);
	Metric.DoubleValue = static_cast<double>(InValue);
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeDouble(const FString& InName, const int64 InAlias, const double InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::Double);
	Metric.DoubleValue = InValue;
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeInt32(const FString& InName, const int64 InAlias, const int32 InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::Int32);
	Metric.IntValue = InValue;
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeInt64(const FString& InName, const int64 InAlias, const int64 InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::Int64);
	Metric.IntValue = InValue;
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeUInt64(const FString& InName, const int64 InAlias, const uint64 InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::UInt64);
	Metric.IntValue = static_cast<int64>(InValue);
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeBool(const FString& InName, const int64 InAlias, const bool bInValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::Boolean);
	Metric.IntValue = bInValue ? 1 : 0;
	return Metric;
}

FSparkplugMetric FSparkplugMetric::MakeString(const FString& InName, const int64 InAlias, const FString& InValue)
{
	FSparkplugMetric Metric = MakeBase(InName, InAlias, ESparkplugDataType::String);
	Metric.StringValue = InValue;
	return Metric;
}

bool FSparkplugMetric::UsesIntStorage() const
{
	switch (DataType)
	{
	case ESparkplugDataType::Int8:
	case ESparkplugDataType::Int16:
	case ESparkplugDataType::Int32:
	case ESparkplugDataType::Int64:
	case ESparkplugDataType::UInt8:
	case ESparkplugDataType::UInt16:
	case ESparkplugDataType::UInt32:
	case ESparkplugDataType::UInt64:
	case ESparkplugDataType::Boolean:
	case ESparkplugDataType::DateTime:
		return true;
	default:
		return false;
	}
}

bool FSparkplugMetric::UsesDoubleStorage() const
{
	return DataType == ESparkplugDataType::Float || DataType == ESparkplugDataType::Double;
}

namespace SparkplugUtils
{
	int64 UtcNowMilliseconds()
	{
		// Single sample: two UtcNow() calls could straddle a millisecond boundary
		// and yield a timestamp that is off by one second.
		const FDateTime Now = FDateTime::UtcNow();
		return Now.ToUnixTimestamp() * 1000LL + Now.GetMillisecond();
	}

	FString MessageTypeToString(const ESparkplugMessageType Type)
	{
		switch (Type)
		{
		case ESparkplugMessageType::NBIRTH: return TEXT("NBIRTH");
		case ESparkplugMessageType::NDEATH: return TEXT("NDEATH");
		case ESparkplugMessageType::NDATA:  return TEXT("NDATA");
		case ESparkplugMessageType::NCMD:   return TEXT("NCMD");
		case ESparkplugMessageType::DBIRTH: return TEXT("DBIRTH");
		case ESparkplugMessageType::DDEATH: return TEXT("DDEATH");
		case ESparkplugMessageType::DDATA:  return TEXT("DDATA");
		case ESparkplugMessageType::DCMD:   return TEXT("DCMD");
		case ESparkplugMessageType::STATE:  return TEXT("STATE");
		default:                            return TEXT("UNKNOWN");
		}
	}
}
