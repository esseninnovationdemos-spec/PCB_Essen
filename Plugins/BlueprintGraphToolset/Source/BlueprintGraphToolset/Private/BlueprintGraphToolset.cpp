#include "BlueprintGraphToolset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Factories/BlueprintFactory.h"
#include "IAssetTools.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

namespace UE::BlueprintGraphToolset::Private
{
	void RaiseError(const FString& Message)
	{
		UKismetSystemLibrary::RaiseScriptError(
			FString::Printf(TEXT("BlueprintGraphToolset: %s"), *Message));
	}

	/** Accepts '/Game/BP_Thing' or '/Game/BP_Thing.BP_Thing' and returns the full object path. */
	FString NormalizeObjectPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			return Path;
		}

		// Strip a trailing _C that callers sometimes copy from generated class names.
		if (Path.EndsWith(TEXT("_C")))
		{
			Path.LeftChopInline(2);
		}

		if (!Path.Contains(TEXT(".")))
		{
			FString AssetName;
			Path.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (!AssetName.IsEmpty())
			{
				Path = Path + TEXT(".") + AssetName;
			}
		}
		return Path;
	}

	UBlueprint* LoadBlueprint(const FString& BlueprintPath)
	{
		const FString ObjectPath = NormalizeObjectPath(BlueprintPath);
		if (ObjectPath.IsEmpty())
		{
			RaiseError(TEXT("Blueprint path is empty."));
			return nullptr;
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			RaiseError(FString::Printf(TEXT("Could not load Blueprint '%s'."), *BlueprintPath));
		}
		return Blueprint;
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}

		TArray<FString> Available;
		for (const UEdGraph* Graph : AllGraphs)
		{
			if (Graph)
			{
				Available.Add(Graph->GetName());
			}
		}
		RaiseError(FString::Printf(
			TEXT("Graph '%s' not found in '%s'. Available graphs: %s"),
			*GraphName, *Blueprint->GetName(), *FString::Join(Available, TEXT(", "))));
		return nullptr;
	}

	UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeGuid)
	{
		FGuid Guid;
		if (!FGuid::Parse(NodeGuid, Guid))
		{
			RaiseError(FString::Printf(TEXT("'%s' is not a valid node GUID."), *NodeGuid));
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid)
			{
				return Node;
			}
		}

		RaiseError(FString::Printf(
			TEXT("Node '%s' not found in graph '%s'."), *NodeGuid, *Graph->GetName()));
		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection PreferredDirection)
	{
		UEdGraphPin* AnyDirectionMatch = nullptr;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			const bool bNameMatches =
				Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) ||
				Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase);

			if (bNameMatches)
			{
				if (Pin->Direction == PreferredDirection)
				{
					return Pin;
				}
				if (!AnyDirectionMatch)
				{
					AnyDirectionMatch = Pin;
				}
			}
		}

		if (AnyDirectionMatch)
		{
			return AnyDirectionMatch;
		}

		TArray<FString> Available;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				Available.Add(FString::Printf(TEXT("%s(%s)"),
					*Pin->PinName.ToString(),
					Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out")));
			}
		}
		RaiseError(FString::Printf(
			TEXT("Pin '%s' not found on node '%s'. Available pins: %s"),
			*PinName, *Node->GetName(), *FString::Join(Available, TEXT(", "))));
		return nullptr;
	}

	UClass* FindClassByName(const FString& ClassName)
	{
		if (ClassName.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* Found = UClass::TryFindTypeSlow<UClass>(ClassName))
		{
			return Found;
		}

		// Fall back to a name scan, tolerating missing U/A/F prefixes.
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (Name.Equals(ClassName, ESearchCase::IgnoreCase) ||
				Name.RightChop(1).Equals(ClassName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	FString DescribePinType(const FEdGraphPinType& PinType)
	{
		FString Type = PinType.PinCategory.ToString();
		if (PinType.PinSubCategoryObject.IsValid())
		{
			Type += TEXT("<") + PinType.PinSubCategoryObject->GetName() + TEXT(">");
		}
		else if (!PinType.PinSubCategory.IsNone())
		{
			Type += TEXT("<") + PinType.PinSubCategory.ToString() + TEXT(">");
		}

		if (PinType.ContainerType == EPinContainerType::Array)
		{
			Type += TEXT("[]");
		}
		else if (PinType.ContainerType == EPinContainerType::Set)
		{
			Type += TEXT("{set}");
		}
		else if (PinType.ContainerType == EPinContainerType::Map)
		{
			Type += TEXT("{map}");
		}
		return Type;
	}

	/** Builds an FEdGraphPinType from the simplified category/subtype strings the tools accept. */
	bool BuildPinType(const FString& PinCategory, const FString& SubTypeName, bool bIsArray, FEdGraphPinType& OutPinType)
	{
		const FString Category = PinCategory.ToLower();

		if (Category == TEXT("bool"))        { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; }
		else if (Category == TEXT("byte"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; }
		else if (Category == TEXT("int"))    { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int; }
		else if (Category == TEXT("int64"))  { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64; }
		else if (Category == TEXT("float") || Category == TEXT("double") || Category == TEXT("real"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (Category == TEXT("string")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_String; }
		else if (Category == TEXT("name"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name; }
		else if (Category == TEXT("text"))   { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text; }
		else if (Category == TEXT("object") || Category == TEXT("class"))
		{
			UClass* SubClass = FindClassByName(SubTypeName);
			if (!SubClass)
			{
				RaiseError(FString::Printf(TEXT("Class '%s' not found for pin type."), *SubTypeName));
				return false;
			}
			OutPinType.PinCategory = (Category == TEXT("object"))
				? UEdGraphSchema_K2::PC_Object
				: UEdGraphSchema_K2::PC_Class;
			OutPinType.PinSubCategoryObject = SubClass;
		}
		else if (Category == TEXT("struct"))
		{
			UScriptStruct* SubStruct = UClass::TryFindTypeSlow<UScriptStruct>(SubTypeName);
			if (!SubStruct)
			{
				RaiseError(FString::Printf(TEXT("Struct '%s' not found for pin type."), *SubTypeName));
				return false;
			}
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = SubStruct;
		}
		else
		{
			RaiseError(FString::Printf(
				TEXT("Unknown pin category '%s'. Use bool, byte, int, int64, real, string, name, text, object, class or struct."),
				*PinCategory));
			return false;
		}

		OutPinType.ContainerType = bIsArray ? EPinContainerType::Array : EPinContainerType::None;
		return true;
	}

	/** Places an already-constructed node into a graph and allocates its pins. */
	void FinalizeNewNode(UEdGraph* Graph, UEdGraphNode* Node, int32 NodePosX, int32 NodePosY)
	{
		Node->NodePosX = NodePosX;
		Node->NodePosY = NodePosY;
		Graph->AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
	}
}

namespace BGT = UE::BlueprintGraphToolset::Private;

// --- Discovery ---

TArray<FString> UBlueprintGraphToolset::ListBlueprints(const FString& PathFilter, const FString& NameFilter)
{
	const FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(*(PathFilter.IsEmpty() ? FString(TEXT("/Game")) : PathFilter)));

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FString> Result;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Result.Add(Asset.GetObjectPathString());
	}

	Result.Sort();
	return Result;
}

FString UBlueprintGraphToolset::GetBlueprintInfo(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("name"), Blueprint->GetName());
	Root->SetStringField(TEXT("path"), Blueprint->GetPathName());
	Root->SetStringField(TEXT("parentClass"),
		Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	TArray<TSharedPtr<FJsonValue>> GraphValues;
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}
		const TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
		GraphObject->SetStringField(TEXT("name"), Graph->GetName());
		GraphObject->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		GraphValues.Add(MakeShared<FJsonValueObject>(GraphObject));
	}
	Root->SetArrayField(TEXT("graphs"), GraphValues);

	TArray<TSharedPtr<FJsonValue>> VariableValues;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		const TSharedRef<FJsonObject> VariableObject = MakeShared<FJsonObject>();
		VariableObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
		VariableObject->SetStringField(TEXT("type"), BGT::DescribePinType(Variable.VarType));
		VariableObject->SetStringField(TEXT("default"), Variable.DefaultValue);
		VariableValues.Add(MakeShared<FJsonValueObject>(VariableObject));
	}
	Root->SetArrayField(TEXT("variables"), VariableValues);

	TArray<TSharedPtr<FJsonValue>> ComponentValues;
	if (Blueprint->SimpleConstructionScript)
	{
		for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			const TSharedRef<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
			ComponentObject->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			ComponentObject->SetStringField(TEXT("class"),
				Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));
			ComponentValues.Add(MakeShared<FJsonValueObject>(ComponentObject));
		}
	}
	Root->SetArrayField(TEXT("components"), ComponentValues);

	return BGT::ToJsonString(Root);
}

TArray<FString> UBlueprintGraphToolset::ListGraphs(const FString& BlueprintPath)
{
	TArray<FString> Result;

	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return Result;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			Result.Add(Graph->GetName());
		}
	}
	return Result;
}

FString UBlueprintGraphToolset::GetGraphNodes(const FString& BlueprintPath, const FString& GraphName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
		NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
		NodeObject->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObject->SetStringField(TEXT("title"),
			Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		NodeObject->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObject->SetNumberField(TEXT("posY"), Node->NodePosY);

		TArray<TSharedPtr<FJsonValue>> PinValues;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden)
			{
				continue;
			}

			const TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();
			PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObject->SetStringField(TEXT("direction"),
				Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObject->SetStringField(TEXT("type"), BGT::DescribePinType(Pin->PinType));
			PinObject->SetStringField(TEXT("defaultValue"), Pin->GetDefaultAsString());

			TArray<TSharedPtr<FJsonValue>> LinkValues;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode())
				{
					continue;
				}
				const TSharedRef<FJsonObject> LinkObject = MakeShared<FJsonObject>();
				LinkObject->SetStringField(TEXT("nodeGuid"),
					Linked->GetOwningNode()->NodeGuid.ToString());
				LinkObject->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
				LinkValues.Add(MakeShared<FJsonValueObject>(LinkObject));
			}
			PinObject->SetArrayField(TEXT("connections"), LinkValues);
			PinValues.Add(MakeShared<FJsonValueObject>(PinObject));
		}
		NodeObject->SetArrayField(TEXT("pins"), PinValues);
		NodeValues.Add(MakeShared<FJsonValueObject>(NodeObject));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetArrayField(TEXT("nodes"), NodeValues);
	return BGT::ToJsonString(Root);
}

namespace
{
	/** Serialises one node's pins, shared by GetGraphNodes and GetNode. */
	TSharedRef<FJsonObject> DescribeNode(const UEdGraphNode& Node, const bool bIncludePins)
	{
		const TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
		NodeObject->SetStringField(TEXT("guid"), Node.NodeGuid.ToString());
		NodeObject->SetStringField(TEXT("class"), Node.GetClass()->GetName());
		NodeObject->SetStringField(TEXT("title"),
			Node.GetNodeTitle(ENodeTitleType::ListView).ToString());
		NodeObject->SetNumberField(TEXT("posX"), Node.NodePosX);
		NodeObject->SetNumberField(TEXT("posY"), Node.NodePosY);

		if (!bIncludePins)
		{
			NodeObject->SetNumberField(TEXT("pinCount"), Node.Pins.Num());
			return NodeObject;
		}

		TArray<TSharedPtr<FJsonValue>> PinValues;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin == nullptr || Pin->bHidden)
			{
				continue;
			}

			const TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();
			PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObject->SetStringField(TEXT("direction"),
				Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObject->SetStringField(TEXT("type"), BGT::DescribePinType(Pin->PinType));
			PinObject->SetStringField(TEXT("defaultValue"), Pin->GetDefaultAsString());

			TArray<TSharedPtr<FJsonValue>> LinkValues;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked == nullptr || Linked->GetOwningNode() == nullptr)
				{
					continue;
				}
				const TSharedRef<FJsonObject> LinkObject = MakeShared<FJsonObject>();
				LinkObject->SetStringField(TEXT("nodeGuid"),
					Linked->GetOwningNode()->NodeGuid.ToString());
				LinkObject->SetStringField(TEXT("nodeTitle"),
					Linked->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString());
				LinkObject->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
				LinkValues.Add(MakeShared<FJsonValueObject>(LinkObject));
			}
			PinObject->SetArrayField(TEXT("connections"), LinkValues);
			PinValues.Add(MakeShared<FJsonValueObject>(PinObject));
		}
		NodeObject->SetArrayField(TEXT("pins"), PinValues);
		return NodeObject;
	}
}

FString UBlueprintGraphToolset::FindNodes(
	const FString& BlueprintPath, const FString& GraphName, const FString& SearchTerm)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> Matches;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}

		const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		const FString ClassName = Node->GetClass()->GetName();

		if (!SearchTerm.IsEmpty()
			&& !Title.Contains(SearchTerm, ESearchCase::IgnoreCase)
			&& !ClassName.Contains(SearchTerm, ESearchCase::IgnoreCase))
		{
			continue;
		}

		Matches.Add(MakeShared<FJsonValueObject>(DescribeNode(*Node, /*bIncludePins*/ false)));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetNumberField(TEXT("totalNodes"), Graph->Nodes.Num());
	Root->SetNumberField(TEXT("matched"), Matches.Num());
	Root->SetArrayField(TEXT("nodes"), Matches);
	return BGT::ToJsonString(Root);
}

FString UBlueprintGraphToolset::GetNode(
	const FString& BlueprintPath, const FString& GraphName, const FString& NodeGuid)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	UEdGraphNode* Node = BGT::FindNode(Graph, NodeGuid);
	if (!Node)
	{
		return FString();
	}

	return BGT::ToJsonString(DescribeNode(*Node, /*bIncludePins*/ true));
}

bool UBlueprintGraphToolset::AddComponent(
	const FString& BlueprintPath, const FString& ComponentClassName, const FString& ComponentName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	if (Blueprint->SimpleConstructionScript == nullptr)
	{
		BGT::RaiseError(TEXT("Blueprint has no construction script; only Actor Blueprints "
			"can host components."));
		return false;
	}

	UClass* ComponentClass = BGT::FindClassByName(ComponentClassName);
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("'%s' is not an ActorComponent class."), *ComponentClassName));
		return false;
	}

	const FName DesiredName(*ComponentName);
	for (const USCS_Node* Existing : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Existing != nullptr && Existing->GetVariableName() == DesiredName)
		{
			BGT::RaiseError(FString::Printf(
				TEXT("Component '%s' already exists on '%s'."),
				*ComponentName, *Blueprint->GetName()));
			return false;
		}
	}

	Blueprint->Modify();
	Blueprint->SimpleConstructionScript->Modify();

	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(
		ComponentClass, DesiredName);
	if (NewNode == nullptr)
	{
		BGT::RaiseError(FString::Printf(TEXT("Could not create component '%s'."), *ComponentName));
		return false;
	}

	// Non-scene components attach at the root of the construction script.
	Blueprint->SimpleConstructionScript->AddNode(NewNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UBlueprintGraphToolset::SetComponentProperty(
	const FString& BlueprintPath,
	const FString& ComponentName,
	const FString& PropertyName,
	const FString& Value)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	if (Blueprint->SimpleConstructionScript == nullptr)
	{
		BGT::RaiseError(TEXT("Blueprint has no construction script."));
		return false;
	}

	UActorComponent* Template = nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node != nullptr && Node->GetVariableName() == FName(*ComponentName))
		{
			Template = Node->ComponentTemplate;
			break;
		}
	}

	if (Template == nullptr)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Component '%s' not found on '%s'."), *ComponentName, *Blueprint->GetName()));
		return false;
	}

	FProperty* Property = Template->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (Property == nullptr)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Property '%s' not found on component class '%s'."),
			*PropertyName, *Template->GetClass()->GetName()));
		return false;
	}

	Template->Modify();
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Template);
	if (!Property->ImportText_Direct(*Value, ValuePtr, Template, PPF_None))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("'%s' is not a valid value for property '%s'."), *Value, *PropertyName));
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

TArray<FString> UBlueprintGraphToolset::FindCallableFunctions(
	const FString& SearchTerm, const FString& ClassNameFilter, int32 MaxResults)
{
	TArray<FString> Result;
	const int32 Limit = MaxResults > 0 ? MaxResults : 50;

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!ClassNameFilter.IsEmpty() && !Class->GetName().Contains(ClassNameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
			{
				continue;
			}

			const FString FunctionName = Function->GetName();
			if (!SearchTerm.IsEmpty() && !FunctionName.Contains(SearchTerm, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TArray<FString> Params;
			FString ReturnType = TEXT("void");
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (!Param->HasAnyPropertyFlags(CPF_Parm))
				{
					continue;
				}
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ReturnType = Param->GetCPPType();
					continue;
				}
				Params.Add(FString::Printf(TEXT("%s %s"), *Param->GetCPPType(), *Param->GetName()));
			}

			Result.Add(FString::Printf(TEXT("%s::%s(%s) -> %s"),
				*Class->GetName(), *FunctionName, *FString::Join(Params, TEXT(", ")), *ReturnType));

			if (Result.Num() >= Limit)
			{
				return Result;
			}
		}
	}
	return Result;
}

// --- Node creation ---

FString UBlueprintGraphToolset::AddFunctionCallNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& ClassName,
	const FString& FunctionName,
	int32 NodePosX,
	int32 NodePosY)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	UClass* TargetClass = BGT::FindClassByName(ClassName);
	if (!TargetClass)
	{
		BGT::RaiseError(FString::Printf(TEXT("Class '%s' not found."), *ClassName));
		return FString();
	}

	UFunction* Function = TargetClass->FindFunctionByName(FName(*FunctionName));
	if (!Function)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Function '%s' not found on class '%s'."), *FunctionName, *ClassName));
		return FString();
	}

	Graph->Modify();
	UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
	Node->SetFromFunction(Function);
	BGT::FinalizeNewNode(Graph, Node, NodePosX, NodePosY);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->NodeGuid.ToString();
}

FString UBlueprintGraphToolset::AddVariableNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& VariableName,
	bool bIsSetter,
	int32 NodePosX,
	int32 NodePosY)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	const FName VarName(*VariableName);
	if (!FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, VarName).IsValid() &&
		(!Blueprint->GeneratedClass || !Blueprint->GeneratedClass->FindPropertyByName(VarName)))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Variable '%s' not found on Blueprint '%s'."), *VariableName, *Blueprint->GetName()));
		return FString();
	}

	Graph->Modify();
	UEdGraphNode* Node = nullptr;
	if (bIsSetter)
	{
		UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
		SetNode->VariableReference.SetSelfMember(VarName);
		Node = SetNode;
	}
	else
	{
		UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
		GetNode->VariableReference.SetSelfMember(VarName);
		Node = GetNode;
	}
	BGT::FinalizeNewNode(Graph, Node, NodePosX, NodePosY);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->NodeGuid.ToString();
}

FString UBlueprintGraphToolset::AddEventNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& EventName,
	int32 NodePosX,
	int32 NodePosY)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	UClass* ParentClass = Blueprint->ParentClass;
	if (!ParentClass || !ParentClass->FindFunctionByName(FName(*EventName)))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Event '%s' not found on parent class '%s'."),
			*EventName, ParentClass ? *ParentClass->GetName() : TEXT("None")));
		return FString();
	}

	Graph->Modify();
	UK2Node_Event* Node = NewObject<UK2Node_Event>(Graph);
	Node->EventReference.SetExternalMember(FName(*EventName), ParentClass);
	Node->bOverrideFunction = true;
	BGT::FinalizeNewNode(Graph, Node, NodePosX, NodePosY);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->NodeGuid.ToString();
}

FString UBlueprintGraphToolset::AddNodeByClass(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeClassName,
	int32 NodePosX,
	int32 NodePosY)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}

	UClass* NodeClass = BGT::FindClassByName(NodeClassName);
	if (!NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("'%s' is not a valid graph node class. Use ListNodeClasses to discover valid names."),
			*NodeClassName));
		return FString();
	}
	if (NodeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		BGT::RaiseError(FString::Printf(TEXT("Node class '%s' is abstract."), *NodeClassName));
		return FString();
	}

	Graph->Modify();
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass);
	BGT::FinalizeNewNode(Graph, Node, NodePosX, NodePosY);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->NodeGuid.ToString();
}

TArray<FString> UBlueprintGraphToolset::ListNodeClasses(const FString& SearchTerm)
{
	TArray<FString> Result;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UK2Node::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		const FString Name = Class->GetName();
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")))
		{
			continue;
		}
		if (!SearchTerm.IsEmpty() && !Name.Contains(SearchTerm, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Result.Add(Name);
	}
	Result.Sort();
	return Result;
}

// --- Node editing ---

bool UBlueprintGraphToolset::ConnectPins(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& FromNodeGuid,
	const FString& FromPinName,
	const FString& ToNodeGuid,
	const FString& ToPinName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* FromNode = BGT::FindNode(Graph, FromNodeGuid);
	UEdGraphNode* ToNode = BGT::FindNode(Graph, ToNodeGuid);
	if (!FromNode || !ToNode)
	{
		return false;
	}

	UEdGraphPin* FromPin = BGT::FindPin(FromNode, FromPinName, EGPD_Output);
	UEdGraphPin* ToPin = BGT::FindPin(ToNode, ToPinName, EGPD_Input);
	if (!FromPin || !ToPin)
	{
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Cannot connect '%s' to '%s': %s"),
			*FromPinName, *ToPinName, *Response.Message.ToString()));
		return false;
	}

	Graph->Modify();
	FromNode->Modify();
	ToNode->Modify();

	if (!Schema->TryCreateConnection(FromPin, ToPin))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Failed to connect '%s' to '%s'."), *FromPinName, *ToPinName));
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UBlueprintGraphToolset::DisconnectPin(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeGuid,
	const FString& PinName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* Node = BGT::FindNode(Graph, NodeGuid);
	if (!Node)
	{
		return false;
	}

	UEdGraphPin* Pin = BGT::FindPin(Node, PinName, EGPD_Output);
	if (!Pin)
	{
		return false;
	}

	Graph->Modify();
	Node->Modify();
	Pin->BreakAllPinLinks();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UBlueprintGraphToolset::SetPinDefaultValue(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeGuid,
	const FString& PinName,
	const FString& Value)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* Node = BGT::FindNode(Graph, NodeGuid);
	if (!Node)
	{
		return false;
	}

	UEdGraphPin* Pin = BGT::FindPin(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Schema->DefaultValueSimpleValidation(Pin->PinType, Pin->PinName, Value, nullptr, FText::GetEmpty()))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("'%s' is not a valid value for pin '%s' of type '%s'."),
			*Value, *PinName, *BGT::DescribePinType(Pin->PinType)));
		return false;
	}

	Node->Modify();
	Schema->TrySetDefaultValue(*Pin, Value);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UBlueprintGraphToolset::MoveNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeGuid,
	int32 NodePosX,
	int32 NodePosY)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* Node = BGT::FindNode(Graph, NodeGuid);
	if (!Node)
	{
		return false;
	}

	Node->Modify();
	Node->NodePosX = NodePosX;
	Node->NodePosY = NodePosY;

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UBlueprintGraphToolset::DeleteNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeGuid)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	UEdGraphNode* Node = BGT::FindNode(Graph, NodeGuid);
	if (!Node)
	{
		return false;
	}

	if (!Node->CanUserDeleteNode())
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Node '%s' cannot be deleted."),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		return false;
	}

	Graph->Modify();
	Node->Modify();
	FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// --- Blueprint structure ---

bool UBlueprintGraphToolset::AddVariable(
	const FString& BlueprintPath,
	const FString& VariableName,
	const FString& PinCategory,
	const FString& SubTypeName,
	bool bIsArray)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	FEdGraphPinType PinType;
	if (!BGT::BuildPinType(PinCategory, SubTypeName, bIsArray, PinType))
	{
		return false;
	}

	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Could not add variable '%s'. It may already exist or the name may be reserved."),
			*VariableName));
		return false;
	}
	return true;
}

bool UBlueprintGraphToolset::RemoveVariable(const FString& BlueprintPath, const FString& VariableName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));
	return true;
}

bool UBlueprintGraphToolset::SetVariableDefault(
	const FString& BlueprintPath,
	const FString& VariableName,
	const FString& Value)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	if (!Blueprint->GeneratedClass)
	{
		BGT::RaiseError(TEXT("Blueprint has no generated class. Compile it first."));
		return false;
	}

	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(FName(*VariableName));
	if (!Property || !DefaultObject)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Variable '%s' not found on Blueprint '%s'."), *VariableName, *Blueprint->GetName()));
		return false;
	}

	DefaultObject->Modify();
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultObject);
	if (!Property->ImportText_Direct(*Value, ValuePtr, DefaultObject, PPF_None))
	{
		BGT::RaiseError(FString::Printf(
			TEXT("'%s' is not a valid value for variable '%s'."), *Value, *VariableName));
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

FString UBlueprintGraphToolset::AddFunctionGraph(const FString& BlueprintPath, const FString& FunctionName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	const FName GraphName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FunctionName);
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
	{
		BGT::RaiseError(FString::Printf(TEXT("Could not create function graph '%s'."), *FunctionName));
		return FString();
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated*/ true, nullptr);
	return NewGraph->GetName();
}

bool UBlueprintGraphToolset::DeleteGraph(const FString& BlueprintPath, const FString& GraphName)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UEdGraph* Graph = BGT::FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return false;
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
	return true;
}

FString UBlueprintGraphToolset::CreateBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentClassName)
{
	UClass* ParentClass = BGT::FindClassByName(ParentClassName);
	if (!ParentClass)
	{
		BGT::RaiseError(FString::Printf(TEXT("Parent class '%s' not found."), *ParentClassName));
		return FString();
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewAsset)
	{
		BGT::RaiseError(FString::Printf(
			TEXT("Could not create Blueprint '%s' in '%s'."), *AssetName, *PackagePath));
		return FString();
	}

	return NewAsset->GetPathName();
}

// --- Persistence ---

FString UBlueprintGraphToolset::CompileBlueprint(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString();
	}

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		const FString Text = Message->ToText().ToString();
		if (Message->GetSeverity() == EMessageSeverity::Error)
		{
			ErrorValues.Add(MakeShared<FJsonValueString>(Text));
		}
		else if (Message->GetSeverity() == EMessageSeverity::Warning)
		{
			WarningValues.Add(MakeShared<FJsonValueString>(Text));
		}
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"),
		Results.NumErrors > 0 ? TEXT("failed") : TEXT("succeeded"));
	Root->SetNumberField(TEXT("errorCount"), Results.NumErrors);
	Root->SetNumberField(TEXT("warningCount"), Results.NumWarnings);
	Root->SetArrayField(TEXT("errors"), ErrorValues);
	Root->SetArrayField(TEXT("warnings"), WarningValues);
	return BGT::ToJsonString(Root);
}

bool UBlueprintGraphToolset::SaveBlueprint(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = BGT::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}

	UPackage* Package = Blueprint->GetOutermost();
	Package->MarkPackageDirty();

	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs))
	{
		BGT::RaiseError(FString::Printf(TEXT("Failed to save '%s'."), *FileName));
		return false;
	}
	return true;
}
