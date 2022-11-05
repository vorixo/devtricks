---
layout: single
title: "Understanding replication atomicity in Unreal Engine"
excerpt: In this post you'll learn the principles about atomic replication with a simple example, a Struct.
header:
  teaser: /assets/images/per-post/atomicity/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - UE4
  - Atomic
  - NetSerialize
  - Struct replication
---

In this article you'll learn the concept of replication atomicity and its importance when dealing with networked code.

# Introduction

[Atomicity](https://www.donnywals.com/what-does-atomic-mean-in-programming/) is a well-known concept in concurrent systems, which guarantees the state of a property when reading or writing, since reads and writes to the property can only happen sequentially.

In our case, atomicity ensures that we can guarantee the client state of a property when reading it after a network update. In Unreal Engine we can use OnReps to know [when a property has replicated to our client](https://vorixo.github.io/devtricks/stateful-events-multiplayer/), therefore we can guarantee that its client state is equivalent to the one on the server, or... can we? 

# Default Struct replication

Structs are one of the types that do not ensure replication atomicity, since their default serialization (at the date of writing) only replicates the properties that changed, we call this **delta serialization**. This type of serialization reduces the network bandwith when replicating structs, as it serializes a network delta from a base state, as we can see below.

![Default delta serialization]({{ '/' | absolute_url }}/assets/images/per-post/atomicity/deltaserialization.jpg){: .align-center}

However, this method of serializing structs can't ensure atomicity if we introduce a real world very common variable, [packet loss](https://en.wikipedia.org/wiki/Packet_loss). In the following figure, the replication of `Property A` did not reach the client due to packet loss, then `Property B` replicated, leaving the relevant client struct in a state that never existed on the server.

![Default delta serialization packet loss issue]({{ '/' | absolute_url }}/assets/images/per-post/atomicity/deltaserializationpacketloss.jpg){: .align-center}

In Unreal, we can simulate  packet loss in PIE by enabling and modifying the [network emulation settings](https://docs.unrealengine.com/5.0/en-US/using-network-emulation-in-unreal-engine/). 
{: .notice--info}

# Atomic Struct replication

There's a way to ensure atomicity when we replicate structs, but it comes with a cost. If we have a requirement such that we **need** to guarantee consistency between client and server, we can opt-out from the default delta serialization and implement our own struct net serializer. This ensures that every time the struct replicates, we send the whole struct to the relevant clients to correct possible hazardous values. The following figure displays the same packet loss scenario we saw before, but now using a custom net serializer:

![Custom net serialization packet loss issue]({{ '/' | absolute_url }}/assets/images/per-post/atomicity/netserializationpacketloss.jpg){: .align-center}

The main cost of this operation is that we are sending the whole struct rather than the replication delta, therefore our bandwidth can get affected and we might need to apply [compression techniques](https://en.wikipedia.org/wiki/Data_compression) in our custom serialization method to mitigate the bandwidth overhead.

## Hands on

To implement a custom atomic net serializer for our replicated struct we need to do the following:

{% highlight c++ %}
USTRUCT()
struct FExample
{
	GENERATED_BODY()

	UPROPERTY()
	float PropertyA;

	UPROPERTY()
	float PropertyB;
 
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
}
 
template<>
struct TStructOpsTypeTraits<FExample> : public TStructOpsTypeTraitsBase2<FExample>
{
	enum
	{
		WithNetSerializer = true
	};
};
{% endhighlight %}

Our `FExample` struct should define `NetSerialize` and we should enable on its `TStructOpsTypeTraits` the property `WithNetSerializer`, which will make the struct replication to go through the implementation of `NetSerialize`, provided below:

{% highlight c++ %}
bool FExample::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	Ar << PropertyA;
	Ar << PropertyB;
	bOutSuccess = true;
	return true;
}
{% endhighlight %}

In this function, we are writing to the Archive `Ar` everything we want to replicate/serialize every time the struct changes. In this case we are writing both properties in the Archive, therefore we can guarantee that these two will be replicated always together. Meaning that, we can ensure atomicity.

`NetSerialize` will be called on the server for serialization, and on the client, for deserialization. We can also detect when we are serializing or deserializing using [`Ar.IsLoading()`](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Core/Serialization/FArchiveState/IsLoading/). Therefore If we wish to reduce the network bandwidth produce by our `NetSerialize`implementation, we can apply lossless and lossy compression techniques.

# Conclusion

Today we learned the basics of replication atomicity and network serializers, but this is just the beginnning.

If you'd like to learn more about the matter I strongly recommend to take a look at [Giuseppe Portelli's](https://twitter.com/gportelli) [blog post about network serialization](http://www.aclockworkberry.com/custom-struct-serialization-for-networking-in-unreal-engine/), which provides a more elaborated explanation about how `TStructOpsTypeTraits` works and all the available properties for it, followed by some examples about compression techniques. 

In addition, I also recommend taking a good read to the [`NetSerialization.h`](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Engine/Classes/Engine/NetSerialization.h) header from the Engine, as it contains very valuable information about how serialization works.

Thank you so much for reading, remember that all the feedback is welcomed, I deeply appreciate corrections and contributions, so feel free!

Enjoy, vori.