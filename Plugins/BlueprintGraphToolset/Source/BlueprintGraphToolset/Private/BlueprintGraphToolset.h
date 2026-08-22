#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "BlueprintGraphToolset.generated.h"

/// Tools for inspecting and editing Blueprint node graphs.
UCLASS(BlueprintType, MinimalAPI)
class UBlueprintGraphToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:

	// --- Discovery ---

	/**
	 * Lists the asset paths of all Blueprints in the project.
	 * @param PathFilter Optional package path to search under, e.g. '/Game/Blueprints'. Empty searches '/Game'.
	 * @param NameFilter Optional case-insensitive substring the Blueprint name must contain. Empty matches all.
	 * @return Sorted array of Blueprint asset paths, e.g. '/Game/BP_Thing.BP_Thing'.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static TArray<FString> ListBlueprints(const FString& PathFilter, const FString& NameFilter);

	/**
	 * Returns a JSON summary of a Blueprint: parent class, graphs, variables, functions, components.
	 * Raises an error if the Blueprint cannot be loaded.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @return JSON object describing the Blueprint.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString GetBlueprintInfo(const FString& BlueprintPath);

	/**
	 * Lists the names of every graph in a Blueprint (event graphs, functions, macros, delegates).
	 * Raises an error if the Blueprint cannot be loaded.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @return Array of graph names, e.g. ['EventGraph', 'MyFunction'].
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static TArray<FString> ListGraphs(const FString& BlueprintPath);

	/**
	 * Returns the full contents of a Blueprint graph as JSON: every node with its GUID, class,
	 * title, position, and every pin with direction, type, default value, and connections.
	 * This is the primary tool for reading a node graph before editing it.
	 * Raises an error if the Blueprint or graph cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @return JSON object with a 'nodes' array describing the graph.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString GetGraphNodes(const FString& BlueprintPath, const FString& GraphName);

	/**
	 * Finds nodes in a graph whose title or class contains a search term.
	 * Returns a compact summary without pin detail, so a large graph can be
	 * searched cheaply; follow up with GetNode for the ones you care about.
	 * Raises an error if the Blueprint or graph cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param SearchTerm Case-insensitive substring matched against node title and class. Empty returns all.
	 * @return JSON array of objects with 'guid', 'class', 'title', 'posX', 'posY', 'pinCount'.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString FindNodes(
		const FString& BlueprintPath, const FString& GraphName, const FString& SearchTerm);

	/**
	 * Returns full detail for a single node: every pin with direction, type,
	 * default value and connections. Use after FindNodes to inspect one node
	 * without dumping the whole graph.
	 * Raises an error if the Blueprint, graph, or node cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeGuid GUID of the node to inspect.
	 * @return JSON object describing the node and its pins.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString GetNode(
		const FString& BlueprintPath, const FString& GraphName, const FString& NodeGuid);

	/**
	 * Adds a component to a Blueprint's construction hierarchy.
	 * Raises an error if the Blueprint or component class cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param ComponentClassName The component class name, e.g. 'FactoryMachineComponent'.
	 * @param ComponentName The name for the new component.
	 * @return True if the component was added.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool AddComponent(
		const FString& BlueprintPath,
		const FString& ComponentClassName,
		const FString& ComponentName);

	/**
	 * Sets a property on a Blueprint component's template, by string value.
	 * Object properties accept an asset path, e.g. '/Game/Foo/Bar.Bar'.
	 * Raises an error if the Blueprint, component, or property cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param ComponentName The component to modify.
	 * @param PropertyName The property to set.
	 * @param Value The new value as a string.
	 * @return True if the property was set.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool SetComponentProperty(
		const FString& BlueprintPath,
		const FString& ComponentName,
		const FString& PropertyName,
		const FString& Value);

	/**
	 * Searches for Blueprint-callable UFUNCTIONs by name, to discover what can be spawned as a call node.
	 * @param SearchTerm Case-insensitive substring to match against function names.
	 * @param ClassNameFilter Optional class name to restrict the search to, e.g. 'KismetMathLibrary'. Empty searches all classes.
	 * @param MaxResults Maximum number of results to return. Use 0 for the default of 50.
	 * @return Array of strings formatted 'ClassName::FunctionName(ParamType ParamName, ...) -> ReturnType'.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static TArray<FString> FindCallableFunctions(const FString& SearchTerm, const FString& ClassNameFilter, int32 MaxResults);

	// --- Node creation ---

	/**
	 * Adds a function call node to a graph.
	 * Raises an error if the Blueprint, graph, or function cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param ClassName The class owning the function, e.g. 'KismetSystemLibrary'.
	 * @param FunctionName The function to call, e.g. 'PrintString'.
	 * @param NodePosX Horizontal position of the new node in graph space.
	 * @param NodePosY Vertical position of the new node in graph space.
	 * @return The GUID of the created node.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString AddFunctionCallNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& ClassName,
		const FString& FunctionName,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Adds a variable getter or setter node for a Blueprint member variable.
	 * Raises an error if the Blueprint, graph, or variable cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param VariableName The member variable name.
	 * @param bIsSetter True to create a setter node, false to create a getter node.
	 * @param NodePosX Horizontal position of the new node in graph space.
	 * @param NodePosY Vertical position of the new node in graph space.
	 * @return The GUID of the created node.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString AddVariableNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& VariableName,
		bool bIsSetter,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Adds an event node (such as BeginPlay or Tick) to an event graph.
	 * Raises an error if the Blueprint, graph, or event cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The event graph name, usually 'EventGraph'.
	 * @param EventName The event function name, e.g. 'ReceiveBeginPlay' or 'ReceiveTick'.
	 * @param NodePosX Horizontal position of the new node in graph space.
	 * @param NodePosY Vertical position of the new node in graph space.
	 * @return The GUID of the created node.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString AddEventNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& EventName,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Adds a node of an arbitrary K2Node class to a graph. Use this for control flow and utility
	 * nodes, e.g. 'K2Node_IfThenElse' (branch), 'K2Node_ExecutionSequence', 'K2Node_DynamicCast',
	 * 'K2Node_MakeArray', 'K2Node_Self', 'K2Node_Timeline'.
	 * Raises an error if the Blueprint, graph, or node class cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeClassName The K2Node class name, e.g. 'K2Node_IfThenElse'.
	 * @param NodePosX Horizontal position of the new node in graph space.
	 * @param NodePosY Vertical position of the new node in graph space.
	 * @return The GUID of the created node.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString AddNodeByClass(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeClassName,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Lists the names of all available K2Node classes that can be passed to AddNodeByClass.
	 * @param SearchTerm Optional case-insensitive substring filter. Empty returns all.
	 * @return Sorted array of node class names.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static TArray<FString> ListNodeClasses(const FString& SearchTerm);

	// --- Node editing ---

	/**
	 * Connects an output pin on one node to an input pin on another node.
	 * Use GetGraphNodes first to discover node GUIDs and exact pin names.
	 * Raises an error if either node or pin cannot be found, or the connection is not allowed.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param FromNodeGuid GUID of the source node.
	 * @param FromPinName Name of the source pin, e.g. 'then' or 'ReturnValue'.
	 * @param ToNodeGuid GUID of the destination node.
	 * @param ToPinName Name of the destination pin, e.g. 'execute' or 'InString'.
	 * @return True if the pins were connected.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool ConnectPins(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& FromNodeGuid,
		const FString& FromPinName,
		const FString& ToNodeGuid,
		const FString& ToPinName);

	/**
	 * Breaks all connections on a single pin.
	 * Raises an error if the node or pin cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeGuid GUID of the node owning the pin.
	 * @param PinName Name of the pin to disconnect.
	 * @return True if the pin was disconnected.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool DisconnectPin(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeGuid,
		const FString& PinName);

	/**
	 * Sets the literal default value of an unconnected input pin.
	 * Raises an error if the node or pin cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeGuid GUID of the node owning the pin.
	 * @param PinName Name of the pin to set.
	 * @param Value The new default value as a string, e.g. 'Hello' or '1.5' or 'true'.
	 * @return True if the default value was set.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool SetPinDefaultValue(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeGuid,
		const FString& PinName,
		const FString& Value);

	/**
	 * Moves a node to a new position in graph space.
	 * Raises an error if the node cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeGuid GUID of the node to move.
	 * @param NodePosX New horizontal position.
	 * @param NodePosY New vertical position.
	 * @return True if the node was moved.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool MoveNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeGuid,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Deletes a node from a graph, breaking all of its connections.
	 * Raises an error if the node cannot be found or is not allowed to be deleted.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The graph name, e.g. 'EventGraph'.
	 * @param NodeGuid GUID of the node to delete.
	 * @return True if the node was deleted.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool DeleteNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeGuid);

	// --- Blueprint structure ---

	/**
	 * Adds a member variable to a Blueprint.
	 * Raises an error if the Blueprint cannot be loaded or the variable could not be added.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param VariableName The name of the new variable.
	 * @param PinCategory The variable type category: 'bool', 'byte', 'int', 'int64', 'real', 'string', 'name', 'text', 'object', 'class', 'struct'.
	 * @param SubTypeName For 'object', 'class' and 'struct' categories, the type name, e.g. 'Actor' or 'Vector'. Ignored otherwise.
	 * @param bIsArray True to make the variable an array.
	 * @return True if the variable was added.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool AddVariable(
		const FString& BlueprintPath,
		const FString& VariableName,
		const FString& PinCategory,
		const FString& SubTypeName,
		bool bIsArray);

	/**
	 * Removes a member variable from a Blueprint.
	 * Raises an error if the Blueprint cannot be loaded.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param VariableName The name of the variable to remove.
	 * @return True if the variable was removed.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool RemoveVariable(const FString& BlueprintPath, const FString& VariableName);

	/**
	 * Sets the default value of a Blueprint member variable.
	 * Raises an error if the Blueprint or variable cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param VariableName The name of the variable.
	 * @param Value The new default value as a string.
	 * @return True if the default value was set.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool SetVariableDefault(
		const FString& BlueprintPath,
		const FString& VariableName,
		const FString& Value);

	/**
	 * Adds a new function graph to a Blueprint.
	 * Raises an error if the Blueprint cannot be loaded.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param FunctionName The name of the new function.
	 * @return The name of the created graph.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString AddFunctionGraph(const FString& BlueprintPath, const FString& FunctionName);

	/**
	 * Deletes a graph from a Blueprint.
	 * Raises an error if the Blueprint or graph cannot be found.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @param GraphName The name of the graph to delete.
	 * @return True if the graph was deleted.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool DeleteGraph(const FString& BlueprintPath, const FString& GraphName);

	/**
	 * Creates a new Blueprint asset.
	 * Raises an error if the parent class cannot be found or the asset could not be created.
	 * @param PackagePath The folder to create the asset in, e.g. '/Game/Blueprints'.
	 * @param AssetName The name of the new Blueprint, e.g. 'BP_Thing'.
	 * @param ParentClassName The parent class name, e.g. 'Actor' or 'Pawn'.
	 * @return The asset path of the created Blueprint.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString CreateBlueprint(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& ParentClassName);

	// --- Persistence ---

	/**
	 * Compiles a Blueprint and returns any errors or warnings.
	 * Call this after editing a graph to validate the result.
	 * Raises an error if the Blueprint cannot be loaded.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @return JSON object with 'status', 'errors' and 'warnings'.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static FString CompileBlueprint(const FString& BlueprintPath);

	/**
	 * Saves a Blueprint asset to disk.
	 * Raises an error if the Blueprint cannot be loaded or saved.
	 * @param BlueprintPath The Blueprint asset path, e.g. '/Game/BP_Thing'.
	 * @return True if the asset was saved.
	 */
	UFUNCTION(meta = (AICallable), Category = "BlueprintGraphToolset")
	static bool SaveBlueprint(const FString& BlueprintPath);
};
