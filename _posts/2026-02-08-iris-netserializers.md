---
layout: single
title: "Iris - Network serializers"
excerpt: In this post we'll learn how to add replication support (serialization) to intricate types in Unreal Engine 5 Iris' networking system.
header:
  teaser: /assets/images/per-post/iris-netserializers/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Networking
  - Serialization
  - Quantization
  - Iris
---

In this article we will explore Iris' serialization by implementing a custom network serializer.

# Introduction

Unreal Engine's legacy replication system offers a very simplistic way to support replication to non-POD properties by implementing `NetSerialize` or `NetDeltaSerialize`, depending on whether [atomic or differential replication](https://vorixo.github.io/devtricks/atomicity/) is needed. In this model, serialization logic is executed as part of building replication data and may be re-run per connection, including when sending initial state to newly connected players.

[Iris](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-iris-in-unreal-engine) serialization differs in that it **Quantizes** and stores replicated state internally, allowing work to be reused across relevant connections. By centralizing and sharing this work instead of performing it separately per client, Iris reduces per-connection overhead thus, improving scalability.

However, implementing custom serializers in Iris is more involved than in the legacy system, as there are many moving pieces one must understand before writing one. 

As Epic developer Peter Engstrom explained ([source](https://forums.unrealengine.com/t/iris-why-are-netserializers-so-complex/2585061)):

```
Generally speaking we would like for people to not having to 
implement serializer at all and cover the most common use cases.

...

...not implementing one if one can avoid it can be a big 
maintenance win.

- Peter Engstrom
```

That said, there are still scenarios where a custom serializer is necessary - such as edge cases not covered natively or for maximum bandwidth optimization (e.g., the engine's own [`FHitResult` custom serialization](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Engine/Private/Engine/HitResultNetSerializer.cpp)). In this article, we'll explore the key pieces involved and implement a serializer for a simple struct.

# Network Serialization in Iris

In the Iris replication system, the serialization process follows a very specific pipeline to ensure data is compressed, compared, and applied to our target. Here is the sequence of events in the order they occur:

  1. **Quantize**: Converts high-precision "Source" data (like a 32-bit float) into a compressed "Internal" representation (like a scaled integer).
  2. **IsEqual**: Called during dirty-checking to determine if quantized state changed since last replication. If true, serialization is skipped entirely.
  3. **Serialize**: Takes the quantized "Internal" state and writes it into the bitstream using a [`FNetBitStreamWriter`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetBitStreamWriter.h).
  4. **Deserialize**: Reads the bits from the [`FNetBitStreamReader`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetBitStreamReader.h) and reconstructs the "Internal" (quantized) state on the receiving end.
  5. **Dequantize**: Reverses the quantization process, converting the quantized data back into the original "External" types (like a `FVector`).
  6. **Apply**: Serializers that want to be selective about which members to modify in the target instance when applying state should implement `Apply` where the serializer is responsible for setting the members of the target instance.

So... **what is sent over the network?** You send over the network exactly and precisely **what you write in your `FNetBitStreamWriter`** within your `Serialize` function.  

**Note:** Serialize and Deserialize are the only strictly required functions. Quantize and Dequantize are optional unless your "Source" data (like a class) must be converted into a simpler "Internal" POD type for serialization. Apply is optional and only used if you need custom logic to write the final value back to the target.
{: .notice--info}

# Serializing a struct in Iris

Before we get started I recommend you opening [NetSerializer.h](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetSerializer.h) to have an inline documentation at hand. 

The struct we will be serializing in this example is the following one:

{% highlight c++ %}
USTRUCT(BlueprintType)
struct FMyStruct
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bHasImpactPoint = false;

	UPROPERTY(BlueprintReadOnly, NotReplicated)
	float Distance;
};
{% endhighlight %}

Our serialization intentions are as follows:

  * Serialize always `StartLocation` and `bHasImpactPoint`
  * If `bHasImpactPoint`, serialize `ImpactPoint`, but only if `StartLocation` has a different value (thus saving some bits).
  * Compute `Distance` on the receiving end.

Note that we desire to serialize the struct with the intention of sending around on RPCs and replicate it.

## Before we start

Remember that the intention of this article is purely informative and, as I mentioned in the introduction, it is important to understand the burden and time consumption involved in maintaining custom Iris serializers. In most cases, it is preferable to design within Iris' constraints rather than introduce a custom serializer. Before writing one, carefully consider whether your use case really needs it - and most importantly (specially if you are new to Iris), don't guess performance: profile.

I decided to serialize `FMyStruct` for this tutorial since it is a **non-polymorphyc** struct that doesn't contain pointer references or dynamic state (e.g. `FString` or `TArray`), so in my opinion it is the perfect candidate for an introduction. With that said, Iris supports [polymorphic serializers](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/PolymorphicNetSerializerImpl.h) and a series of tools to enable dynamic serialization.

Since the documentation is scarce at the time of writing, I recommend the reader to look at the [`Iris\Serialization\`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization) directory in GithHub, specially I suggest paying attention to the following files for what concerns this article:
  
  * [`NetSerializer.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetSerializer.h): Here you will find the definition and documentation of each variable, function, class and struct that can be part of your network serializers. Contains very valuable information of how Iris' serializers work.
  * [`PolymorphicNetSerializerImpl.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/PolymorphicNetSerializerImpl.h): In this header you will find the definition and documentation for implementing polymorphic struct serializers (such as [`FGameplayEffectContextHandle`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/Serialization/GameplayEffectContextHandleNetSerializer.cpp)).
  * [`NetSerializerArrayStorage.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetSerializerArrayStorage.h): Main hub for dynamic storage serialization.
  * [`Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h): Here you will find a series of useful macros to register/forward your net serializers.

Pay attention to the comments within the showcased code sections, they provide important context!
{: .notice--warning}

## Declaring the serializer
 
The first step to do when declaring a serializer is figuring out which serializer config we need, in this case, since our struct isn't polymorphic, we use `FNetSerializerConfig`:

{% highlight c++ %}
Header file (.h):

USTRUCT()
struct FMyStructNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	UE_NET_DECLARE_SERIALIZER(FMyStructNetSerializer, MYGAME_API);
}
{% endhighlight %}

**Note:** Polymorphic structs require to use `FPolymorphicStructNetSerializerConfig`.
{: .notice--info}

## Registering the serializer

If we want replication for our type to occur through our custom Iris net serializer, we need to register it to the `FPropertyNetSerializerInfoRegistry` via some macros:

{% highlight c++ %}
Implementation file (.cpp):

namespace UE::Net
{
	struct FMyStructNetSerializer
	{
		...

	private:
		class FRegistryDelegates final : public FNetSerializerRegistryDelegates
		{
		public:
			virtual ~FRegistryDelegates() override;
			virtual void OnPreFreezeNetSerializerRegistry() override;
		};

		static FRegistryDelegates RegistryDelegates;
	};

	FMyStructNetSerializer::FRegistryDelegates FMyStructNetSerializer::RegistryDelegates;

	...
}

namespace UE::Net
{
	static const FName PropertyNetSerializerRegistry_NAME_MyStruct("MyStruct");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct, FMyStructNetSerializer);

	FMyStructNetSerializer::FRegistryDelegates::~FRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct);
	}

	void FMyStructNetSerializer::FRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct);
	}
}
{% endhighlight %}

In the case we are inheriting our struct from one that has already a net serializer implemented, we can forward* their serialization.

**Note:** Net Serializer forwarding allows you to reuse an existing serializer for a different type. For instance, [`LyraGameplayEffectContext.cpp`](https://github.com/EpicGames/UnrealEngine/blob/release/Samples/Games/Lyra/Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.cpp) uses [`FGameplayEffectContext`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/Serialization/GameplayEffectContextNetSerializer.cpp) for serialization via the `UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES` macro.
{: .notice--info}

## Implementing the serializer

Now that we've let Iris know it should use our serializer, let's fill the gaps, we'll start by defining our serializer struct:

{% highlight c++ %}
Implementation file (.cpp):

namespace UE::Net
{
	struct FMyStructNetSerializer
	{
		static const uint32 Version = 0;
		
		/** These flags help to define the amount of data we'll need to replicate for a specific event. */
		enum EReplicationFlags : uint8
		{
			HasImpact = 1U,
			ImpactDifferentStart = 2U,
		};

		/** Number of flags in the above enum. */
		static constexpr uint32 ReplicatedFlagCount = 2U;
		
		/** 
		* Quantized storage representing our type. Sizes are determined by type's QuantizeType.
		* this is not the amount of data we send around
		*/
		struct FQuantizedType
		{
			uint64 StartLocation[4];
			uint64 ImpactPoint[4];
			uint8 ReplicationFlags;
		};

		/**
		* Needed in order to calculate external state size and alignment
		* and provide default implementations of some functions.
		*/
		typedef FMyStruct SourceType;

		/**
		* A typedef for the QuantizedType is optional unless the SourceType isn't POD. Assumed to be SourceType if not specified.
		* The QuantizedType needs to be POD.
		*/
		typedef FQuantizedType QuantizedType;
		
		typedef FMyStructNetSerializerConfig ConfigType;

		/** DefaultConfig is optional but highly recommended as the serializer can then be used without requiring special configuration setup. */
		static const ConfigType DefaultConfig;
		
		static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
		static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);
		static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
		static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);		
		static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);
		static void Apply(FNetSerializationContext&, const FNetApplyArgs&);

	private:
		/** Registering our Net Serializer */
		class FRegistryDelegates final : public FNetSerializerRegistryDelegates
		{
		public:
			virtual ~FRegistryDelegates() override;
			virtual void OnPreFreezeNetSerializerRegistry() override;
		};

		static FRegistryDelegates RegistryDelegates;
	};

	const FMyStructNetSerializer::ConfigType FMyStructNetSerializer::DefaultConfig;
	FMyStructNetSerializer::FRegistryDelegates FMyStructNetSerializer::RegistryDelegates;
	
	/** Macro that Constructs our Net Serializer and provides Iris our struct's size and alignment. */
	UE_NET_IMPLEMENT_SERIALIZER(FMyStructNetSerializer);

	...
}
{% endhighlight %}

Once it's defined we can start implementing each one of our methods immediately below `UE_NET_IMPLEMENT_SERIALIZER(FMyStructNetSerializer);`. For that, we'll go in the order of operations defined in the above section.

### 1. Quantize

First, we Quantize our data:

{% highlight c++ %}
Implementation file (.cpp):

void FMyStructNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType TempValue = {};

	// We set our replication flags based on "Source" state.
	uint8 ReplicationFlags = 0;
	ReplicationFlags |= Source.bHasImpactPoint ? EReplicationFlags::HasImpact : 0U;
	ReplicationFlags |= Source.StartLocation != Source.ImpactPoint ? EReplicationFlags::ImpactDifferentStart : 0U;

	TempValue.ReplicationFlags = ReplicationFlags;

	// Start location quantize
	{
		// We use the FVectorNetQuantizeNetSerializer to Quantize the StartLocation vector.
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetQuantizeArgs MemberArgs = Args;
		MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		MemberArgs.Source = NetSerializerValuePointer(&Source.StartLocation);
		MemberArgs.Target = NetSerializerValuePointer(&TempValue.StartLocation[0]);
		VectorSerializer.Quantize(Context, MemberArgs);
	}

	// Impact point quantize
	if ((TempValue.ReplicationFlags & HasImpact) && (TempValue.ReplicationFlags & ImpactDifferentStart))
	{
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetQuantizeArgs MemberArgs = Args;
		MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		MemberArgs.Source = NetSerializerValuePointer(&Source.ImpactPoint);
		MemberArgs.Target = NetSerializerValuePointer(&TempValue.ImpactPoint[0]);
		VectorSerializer.Quantize(Context, MemberArgs);
	}

	// Finally assign by ref our quantized data to target
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	Target = TempValue;
}
{% endhighlight %}

In this step, we set our `ReplicationFlags` based on the `Source` state. If `bHasImpactPoint` is false, we skip both the quantization and transmission of `ImpactPoint`. Similarly, we compare `ImpactPoint` to `StartLocation`; if they are identical, we avoid sending `ImpactPoint` over the network, as we can simply use the `StartLocation` value we've already provided.

The remainder of the implementation is syntax sugar, primarily acting as a wrapper to forward the specialized Quantize calls to the appropriate sub-serializers.

**Note:** Further Quantization is possible in `FVector`, see: `FVectorNetQuantize10NetSerializer`, `FVectorNetQuantize100NetSerializer` in [`PackedVectorNetSerializers.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/PackedVectorNetSerializers.h).
{: .notice--info}

### 2. IsEqual

This function is a requirement if your `FNetSerializer` has a `Quantize` function. `IsEqual` job is to compare the Quantized (internal) state of a property against its previously cached version to see if anything has actually changed.

If `IsEqual` returns true, Iris assumes the data is identical and completely skips serialization, saving you from sending a redundant packet. Note that you have to implement both branches, when the data is quantized, and dequantized.

{% highlight c++ %}
Implementation file (.cpp):
  
bool FMyStructNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		// If the data is quantized we can simply Memcmp it
		const QuantizedType& QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
		return FPlatformMemory::Memcmp(&QuantizedValue0, &QuantizedValue1, sizeof(QuantizedType)) == 0;
	}
	else
	{
		const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);

		if (SourceValue0.bHasImpactPoint != SourceValue1.bHasImpactPoint)
		{
			return false;
		}
		
		/**
		* When the data is dequantize it, we quantize it again to compare it
		* This is a bit expensive, so bear in mind, you could also perform
		* a normal == comparison.
		*/
		QuantizedType QuantizedValue0 = {};
		QuantizedType QuantizedValue1 = {};

		FNetQuantizeArgs QuantizeArgs = {};
		QuantizeArgs.NetSerializerConfig = Args.NetSerializerConfig;

		QuantizeArgs.Source = NetSerializerValuePointer(Args.Source0);
		QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue0);
		Quantize(Context, QuantizeArgs);

		QuantizeArgs.Source = NetSerializerValuePointer(Args.Source1);
		QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue1);
		Quantize(Context, QuantizeArgs);

		return FPlatformMemory::Memcmp(&QuantizedValue0, &QuantizedValue1, sizeof(QuantizedType)) == 0;
	}
}
{% endhighlight %}
  
### 3. Serialize

The next step is to send the quantized data over the network:

{% highlight c++ %}
Implementation file (.cpp):

void FMyStructNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	// Acquire the quantized data.
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	
	// This is where we write to send fluff through the network
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// We send our replication flags (using just 2 bits)
	const uint8 ReplicationFlags = Value.ReplicationFlags;
	Writer->WriteBits(ReplicationFlags, ReplicatedFlagCount);

	// Start location
	{
		UE_NET_TRACE_SCOPE(StartLocation, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetSerializeArgs VectorNetSerializeArgs = Args;
		VectorNetSerializeArgs.Source = NetSerializerValuePointer(&Value.StartLocation[0]);
		VectorNetSerializeArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		VectorSerializer.Serialize(Context, VectorNetSerializeArgs);
	}
	
	// Impact point
	if ((Value.ReplicationFlags & HasImpact) && (Value.ReplicationFlags & ImpactDifferentStart))
	{
		UE_NET_TRACE_SCOPE(ImpactPoint, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetSerializeArgs VectorNetSerializeArgs = Args;
		VectorNetSerializeArgs.Source = NetSerializerValuePointer(&Value.ImpactPoint[0]);
		VectorNetSerializeArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		VectorSerializer.Serialize(Context, VectorNetSerializeArgs);
	}
}
{% endhighlight %}

As seen above, we write our replication flags directly to the `FNetBitStreamWriter` using only 2 bits. The other two properties simply forward their serialization logic to the `FVectorNetQuantizeNetSerializer`. By checking the flags first, we ensure `ImpactPoint` bits are only written when necessary, keeping the packet small.

### 4. Deserialize

Third, we deserialize the quantized data:

{% highlight c++ %}
Implementation file (.cpp):

void FMyStructNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	// This is where we read the quantized data from
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	
	// We read the flags (2 bits) and store them in Target
	Target.ReplicationFlags = Reader->ReadBits(ReplicatedFlagCount);

	// Start location
	{
		UE_NET_TRACE_SCOPE(StartLocation, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetDeserializeArgs MemberArgs = Args;
		MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		MemberArgs.Target = NetSerializerValuePointer(&Target.StartLocation[0]);
		VectorSerializer.Deserialize(Context, MemberArgs);
	}

	// Impact point
	if ((Target.ReplicationFlags & HasImpact) && (Target.ReplicationFlags & ImpactDifferentStart))
	{
		UE_NET_TRACE_SCOPE(ImpactPoint, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetDeserializeArgs MemberArgs = Args;
		MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		MemberArgs.Target = NetSerializerValuePointer(&Target.ImpactPoint[0]);
		VectorSerializer.Deserialize(Context, MemberArgs);
	}
}
{% endhighlight %}

Deserialize will read from the `FNetBitStreamReader` the quantized data we serialized. Note how our replication flags get passed along and are actually avoiding us to read data we don't need.

### 5. Dequantize

After we deserialize we need to recompose our data back to a dequantized state:

{% highlight c++ %}
Implementation file (.cpp):

void FMyStructNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	const uint8 ReplicationFlags = Source.ReplicationFlags;
	
	// Start location
	{
		const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

		FNetDequantizeArgs MemberArgs = Args;
		MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
		MemberArgs.Source = NetSerializerValuePointer(&Source.StartLocation[0]);
		MemberArgs.Target = NetSerializerValuePointer(&Target.StartLocation);
		VectorSerializer.Dequantize(Context, MemberArgs);
	}

	/**
	* The logic below Dequantizes ImpactPoint if deemed necessary by our flags.
	* ImpactPoint is set to StartLocation if they aren't different.
	*/
	if (ReplicationFlags & HasImpact)
	{
		if (ReplicationFlags & ImpactDifferentStart)
		{
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetDequantizeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Source = NetSerializerValuePointer(&Source.ImpactPoint[0]);
			MemberArgs.Target = NetSerializerValuePointer(&Target.ImpactPoint);
			VectorSerializer.Dequantize(Context, MemberArgs);
		}
		else
		{
			Target.ImpactPoint = Target.StartLocation;
		}
	}
	else
	{
		// We reset it to 0 intentionally, although we could simply skip it.
		Target.ImpactPoint = FVector::ZeroVector;
	}
	
	Target.bHasImpactPoint = (ReplicationFlags & HasImpact) ? 1 : 0;
}
{% endhighlight %}

After deserializing, we must recompose the data into its original form. Here, we dequantize `StartLocation` and check our flags to determine how to handle `ImpactPoint`. If the flags indicate the impact point is identical to the start, we simply copy the value over, otherwise, we either dequantize `ImpactPoint` or zero it out.

By the end of this process, the Target struct is fully restored. Note that while I left `Distance` out here to handle it in `Apply`, it would be equally valid to compute it during this stage.

### 6. (Optional) Apply

In this example `Apply` is not necessary, but I'm implementing it for demonstration. This step occurs after dequantization and is responsible for writing the data into the actual target. While Iris does this _automagically_ by default, overriding it allows you to skip specific properties or perform additional logic/fixup - like calculating `Distance` - before the game uses the values.

{% highlight c++ %}
Implementation file (.cpp):

void FMyStructNetSerializer::Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	Target.bHasImpactPoint = Source.bHasImpactPoint;
	Target.ImpactPoint = Source.ImpactPoint;
	Target.StartLocation = Source.StartLocation;
	Target.Distance = (Target.ImpactPoint - Target.StartLocation).Size();
}
{% endhighlight %}

Et voila, serializer done! That was... interesting!
  
## Full code

I provide below the full code of the complete serializer so that you can copy and paste it directly without the need of going fragment by fragment:

{% highlight c++ %}
Header file (.h): 

USTRUCT(BlueprintType)
struct FMyStruct
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bHasImpactPoint = false;

	UPROPERTY(BlueprintReadOnly, NotReplicated)
	float Distance;
};

USTRUCT()
struct FMyStructNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	UE_NET_DECLARE_SERIALIZER(FMyStructNetSerializer, MYGAME_API);
}
{% endhighlight %} 
  
{% highlight c++ %}
Implementation file (.cpp):

namespace UE::Net
{
	struct FMyStructNetSerializer
	{
		// Version
		static const uint32 Version = 0;
		
		enum EReplicationFlags : uint8
		{
			HasImpact = 1U,
			ImpactDifferentStart = 2U,
		};

		static constexpr uint32 ReplicatedFlagCount = 2U;
		
		struct FQuantizedType
		{
			uint64 StartLocation[4];
			uint64 ImpactPoint[4];
			uint8 ReplicationFlags;
		};

		typedef FMyStruct SourceType;
		typedef FQuantizedType QuantizedType;
		typedef FMyStructNetSerializerConfig ConfigType;

		static const ConfigType DefaultConfig;
		
		static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
		static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);
		static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
		static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);
		static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);

		static void Apply(FNetSerializationContext&, const FNetApplyArgs&);

	private:
		class FRegistryDelegates final : public FNetSerializerRegistryDelegates
		{
		public:
			virtual ~FRegistryDelegates() override;
			virtual void OnPreFreezeNetSerializerRegistry() override;
		};

		static FRegistryDelegates RegistryDelegates;
	};

	const FMyStructNetSerializer::ConfigType FMyStructNetSerializer::DefaultConfig;
	FMyStructNetSerializer::FRegistryDelegates FMyStructNetSerializer::RegistryDelegates;
	
	UE_NET_IMPLEMENT_SERIALIZER(FMyStructNetSerializer);
	
	void FMyStructNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
	{
		const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

		FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

		// Replicated flags
		const uint8 ReplicationFlags = Value.ReplicationFlags;
		Writer->WriteBits(ReplicationFlags, ReplicatedFlagCount);

		// Start location
		{
			UE_NET_TRACE_SCOPE(StartLocation, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetSerializeArgs VectorNetSerializeArgs = Args;
			VectorNetSerializeArgs.Source = NetSerializerValuePointer(&Value.StartLocation[0]);
			VectorNetSerializeArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			VectorSerializer.Serialize(Context, VectorNetSerializeArgs);
		}
		
		// Impact point
		if ((Value.ReplicationFlags & HasImpact) && (Value.ReplicationFlags & ImpactDifferentStart))
		{
			UE_NET_TRACE_SCOPE(ImpactPoint, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetSerializeArgs VectorNetSerializeArgs = Args;
			VectorNetSerializeArgs.Source = NetSerializerValuePointer(&Value.ImpactPoint[0]);
			VectorNetSerializeArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			VectorSerializer.Serialize(Context, VectorNetSerializeArgs);
		}
	}

	void FMyStructNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
	{
		QuantizedType TempValue = {};

		FNetBitStreamReader* Reader = Context.GetBitStreamReader();
		TempValue.ReplicationFlags = Reader->ReadBits(ReplicatedFlagCount);

		// Start location
		{
			UE_NET_TRACE_SCOPE(StartLocation, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetDeserializeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Target = NetSerializerValuePointer(&TempValue.StartLocation[0]);
			VectorSerializer.Deserialize(Context, MemberArgs);
		}

		// Impact point
		if ((TempValue.ReplicationFlags & HasImpact) && (TempValue.ReplicationFlags & ImpactDifferentStart))
		{
			UE_NET_TRACE_SCOPE(ImpactPoint, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetDeserializeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Target = NetSerializerValuePointer(&TempValue.ImpactPoint[0]);
			VectorSerializer.Deserialize(Context, MemberArgs);
		}

		QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
		Target = TempValue;
	}

	void FMyStructNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
	{
		const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
		QuantizedType TempValue = {};

		uint8 ReplicationFlags = 0;

		ReplicationFlags |= Source.bHasImpactPoint ? EReplicationFlags::HasImpact : 0U;
		ReplicationFlags |= Source.StartLocation != Source.ImpactPoint ? EReplicationFlags::ImpactDifferentStart : 0U;

		TempValue.ReplicationFlags = ReplicationFlags;

		// Start location
		{
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetQuantizeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Source = NetSerializerValuePointer(&Source.StartLocation);
			MemberArgs.Target = NetSerializerValuePointer(&TempValue.StartLocation[0]);
			VectorSerializer.Quantize(Context, MemberArgs);
		}

		// Impact point
		if ((TempValue.ReplicationFlags & HasImpact) && (TempValue.ReplicationFlags & ImpactDifferentStart))
		{
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetQuantizeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Source = NetSerializerValuePointer(&Source.ImpactPoint);
			MemberArgs.Target = NetSerializerValuePointer(&TempValue.ImpactPoint[0]);
			VectorSerializer.Quantize(Context, MemberArgs);
		}

		// Finally copy-over quantized data to target
		QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
		Target = TempValue;
	}

	void FMyStructNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
	{
		const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
		SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

		const uint8 ReplicationFlags = Source.ReplicationFlags;
		
		// Start location
		{
			const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

			FNetDequantizeArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
			MemberArgs.Source = NetSerializerValuePointer(&Source.StartLocation[0]);
			MemberArgs.Target = NetSerializerValuePointer(&Target.StartLocation);
			VectorSerializer.Dequantize(Context, MemberArgs);
		}

		// Impact point
		if (ReplicationFlags & HasImpact)
		{
			if (ReplicationFlags & ImpactDifferentStart)
			{
				const FNetSerializer& VectorSerializer = UE_NET_GET_SERIALIZER(FVectorNetQuantizeNetSerializer);

				FNetDequantizeArgs MemberArgs = Args;
				MemberArgs.NetSerializerConfig = NetSerializerConfigParam(VectorSerializer.DefaultConfig);
				MemberArgs.Source = NetSerializerValuePointer(&Source.ImpactPoint[0]);
				MemberArgs.Target = NetSerializerValuePointer(&Target.ImpactPoint);
				VectorSerializer.Dequantize(Context, MemberArgs);
			}
			else
			{
				Target.ImpactPoint = Target.StartLocation;
			}
		}
		else
		{
			Target.ImpactPoint = FVector::ZeroVector;
		}
		
		Target.bHasImpactPoint = (ReplicationFlags & HasImpact) ? 1 : 0;
	}

	bool FMyStructNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
	{
		if (Args.bStateIsQuantized)
		{
			const QuantizedType& QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
			const QuantizedType& QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
			return FPlatformMemory::Memcmp(&QuantizedValue0, &QuantizedValue1, sizeof(QuantizedType)) == 0;
		}
		else
		{
			const SourceType& SourceValue0 = *reinterpret_cast<const SourceType*>(Args.Source0);
			const SourceType& SourceValue1 = *reinterpret_cast<const SourceType*>(Args.Source1);
	
			if (SourceValue0.bHasImpactPoint != SourceValue1.bHasImpactPoint)
			{
				return false;
			}
			
			QuantizedType QuantizedValue0 = {};
			QuantizedType QuantizedValue1 = {};
	
			FNetQuantizeArgs QuantizeArgs = {};
			QuantizeArgs.NetSerializerConfig = Args.NetSerializerConfig;
	
			QuantizeArgs.Source = NetSerializerValuePointer(Args.Source0);
			QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue0);
			Quantize(Context, QuantizeArgs);
	
			QuantizeArgs.Source = NetSerializerValuePointer(Args.Source1);
			QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedValue1);
			Quantize(Context, QuantizeArgs);
	
			return FPlatformMemory::Memcmp(&QuantizedValue0, &QuantizedValue1, sizeof(QuantizedType)) == 0;
		}
	}

	void FMyStructNetSerializer::Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
	{
		const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
		SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

		Target.bHasImpactPoint = Source.bHasImpactPoint;
		Target.ImpactPoint = Source.ImpactPoint;
		Target.StartLocation = Source.StartLocation;
		Target.Distance = (Target.ImpactPoint - Target.StartLocation).Size();

		UE_LOG(LogTemp, Warning, TEXT("FMyStructNetSerializer::Apply"));
	}
}

namespace UE::Net
{
	static const FName PropertyNetSerializerRegistry_NAME_MyStruct("MyStruct");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct, FMyStructNetSerializer);
	
	FMyStructNetSerializer::FRegistryDelegates::~FRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct);
	}
	
	void FMyStructNetSerializer::FRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_MyStruct);
	}
}
{% endhighlight %} 
  
# Conclusion

Well, that was certainly an adventure! I’m sure you can now see why Peter Engstrom said, "Not implementing one if one can avoid it can be a big maintenance win".

Iris serializers offer many more options than I was able to explore in this article. I encourage you all to do your own experiments and exploration (please, don't just take my word for it!), let this post serve as a helping hand to get you started!

And... yeah! Feel free to [contact me](https://twitter.com/vorixo) (and follow me! hehe) if you find any issues with the article or have any questions about the whole topic.

Enjoy, vori.