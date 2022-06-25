---
layout: single
title: "Unreal Engine 5 - The magic of network managers (Networking optimization)"
excerpt: How to replicate hundreds of actors without replicating them
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - Replication
  - UE5
  - UE4
  - Network manager
  - Optimization
---

In this post, we'll be touching on Network Managers and exploring how we can use them to minimise the inevitable server load that comes with cases requiring the replication of hundreds of actors across our World.

# What is a network manager?

A Network Manager is a replicated Actor that handles the data replication of a given set of Actors, negating the need to replicate the Actors themselves.

Each network manager tracks a set of Actors in a list (order of hundreds), and sends an event to the appropriate Actor  when a variable requires replicating.

While Network Managers handle data replication, we should note that **Actors which require RPCs to work still need to be replicated**. In these cases, it's recommended to reduce the `NetUpdateFrequency` of the tracked Actors as variable replication is handled by the manager.

## Why are network managers useful?

Quoting my dear friend `Zlo#1654` from [Slackers](https://unrealslackers.org/):

> *Replicating several hundred moving Actors is doable, however, replicating several hundred moving Actors while you have several thousand non moving replicated Actors cluttering the `NetBroadcastTick` is not.*

The `NetBroadcastTick` is a [Stat](https://docs.unrealengine.com/4.27/en-US/TestingAndOptimization/PerformanceAndProfiling/StatCommands/StatsSystemOverview/) that measures performance data from the `BroadcastTickFlush` function that is part of the `UWorld::Tick`. This function is in charge of flushing networking and ticking our net drivers (`UNetDriver::TickFlush`). In essence, it's the responsible of managing the replication of all of our replicated Actors.

This means that the more replicated Actors we have, the more processing time `BroadcastTickFlush` will need. Thus, the main objective in our quest to reduce server CPU processing time is to keep the values reported by `NetBroadcastTick` as low as possible.

CPU timing issues derived from bad `NetBroadcastTick` metrics are far more common than bandwidth issues in our Server instances.
{: .notice--info}

Thus, the main use of network managers is to reduce the amount of replicated Actors by [turning off their replication](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/GameFramework/AActor/SetReplicates/) and passing that responsibility on to a series of Actor managers, a process that yields a direct `NetBroadcastTick` optimization. 

We could argue that this same effect can be achieved by employing [Dormancy](https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Engine/ENetDormancy/) and a reduced `NetUpdateFrequency`. However,  this approach can result in less Net responsive Actors and complicated relevancy scenarios. To combat this, it would become necessary to boost the [`NetPriority`](https://docs.unrealengine.com/4.26/en-US/InteractiveExperiences/Networking/Actors/Relevancy/#prioritization) and the `NetUpdateFrequency` of all your replicated Actors, a disastrous change for any multiplayer use case.

This can also be mitigated with the use of Actor Managers. Due to the small number of network managers in our world, we can set a higher `NetPriority` (ie. 2.8) without causing bandwidth issues, which would translate to an increase in the responsivity of the tracked Actors.


## Performance implications

This Section showcases metrics from replicating 976 actors with and without a network manager. The test was conducted with 2 players: A listen server host player with a `-nullrhi` instance, and a `-nullrhi` client. The metrics were recorded from the server instance using [Unreal Insights](https://docs.unrealengine.com/4.26/en-US/TestingAndOptimization/PerformanceAndProfiling/UnrealInsights/), and all the experiments were performed on the same hardware under equivalent conditions.

The first experiment consists of replicating 976 Actors with two replicated variables, each Actor runs at a `NetUpdateFrequency` of 25 and has a `NetPriority` of 1:

![Without network manager]({{ '/' | absolute_url }}/assets/images/per-post/net-man/Profiling1000actors.jpg){: .align-center}

In the second experiment, 720 of those 976 actors use the [Push Model](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Runtime/Net/Core/Private/Net/Core/PushModel/PushModel.cpp) with a reduced `NetUpdateFrequency` of 1 (leaving the remaining 256 actors with default replication):

![Without network manager]({{ '/' | absolute_url }}/assets/images/per-post/net-man/pushmodel-samesetup.jpg){: .align-center}

In the third experiment, we delegated the handling of 720 of those 976 actors to a Network Manager running with a `NetUpdateFrequency` of 100 (leaving the remaining 256 actors with default replication with a `NetUpdateFrequency` of 25):

![Without network manager]({{ '/' | absolute_url }}/assets/images/per-post/net-man/Profiling1000actors720netman.jpg){: .align-center}

As we can see the performance metrics in the third experiment look more favorable, as we are reducing considerably the amount of replicated Actors by moving their data replication to a network manager (while keeping the same amount of Actors in the Level). The Push Model experiment provides speedup over the baseline setup, but the network manager is the absolute winner for this use case.

Actors that need RPCs to work will still need to be replicated.
{: .notice--info}


# Implementation

The network manager is an `AActor` that uses `FFastArraySerializer`(s) to hold the data of all their tracked Actors.

In this Section, we'll implement a simple network manager that handles the replication of a health component we designed for active Actors around the world.

Without the network manager, all the Actors with the health component would need to be replicated alongside the component.

This article focuses on the replication of Actors previously placed in the level, so it is not necessary to replicate them. Future posts linked in this article will expand the concept to Actors spawned in runtime.
{: .notice--info}

## `FFastArraySerializer`

Each [`FFastArraySerializer`](https://github.com/EpicGames/UnrealEngine/blob/ue5-main/Engine/Source/Runtime/Net/Core/Classes/Net/Serialization/FastArraySerializer.h) in our manager handles a type of Actor, in this example, our network manager handles the replication of health components:

{% highlight c++ %}
USTRUCT()
struct FHealthCompItem : public FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TArray<uint8> Data;

	FHealthCompItem();

	FHealthCompItem(const UHealthComponent* HealthComponent, const TArray<uint8>& Payload);

	void PostReplicatedChange(const struct FHealthCompContainer& InArraySerializer);
};

USTRUCT()
struct FHealthCompContainer : public FFastArraySerializer
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FHealthCompItem>	Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FHealthCompItem, FHealthCompContainer>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits< FHealthCompContainer > : public TStructOpsTypeTraitsBase2< FHealthCompContainer >
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

FHealthCompItem::FHealthCompItem()
{
	OwnerActor = nullptr;
}

FHealthCompItem::FHealthCompItem(const UHealthComponent* HealthComponent, const TArray<uint8>& Payload)
{
	if (ensureAlways(HealthComponent))
	{
		OwnerActor = HealthComponent->GetOwner();
		Data = Payload;
	}
}

void FHealthCompItem::PostReplicatedChange(const struct FHealthCompContainer& InArraySerializer)
{
	if (OwnerActor.Get())
	{
		if (UHealthComponent* Hc = OwnerActor.Get()->FindComponentByClass<UHealthComponent>())
		{
			Hc->PostReplication(Data);
		}
	}
}
{% endhighlight %}

The `FHealthCompItem` corresponds to a health component from an Actor and contains a reference to its owner and the data to replicate. In this example the data is contained in a `TArray<uint8> Data` to demonstrate encoding and decoding of generic data, but it is possible (and also recommended in gameplay-intensive instances) to have it in independent properties to ease debugging.

`PostReplicatedChange` calls a custom function in the health component called `PostReplication`, which is responsible of receiving and decoding the data on the client's end.

## The network manager class

The network manager uses the fast array declared in the previous step:

{% highlight c++ %}
UCLASS()
class GAME_API ANetMan : public AActor
{
	GENERATED_BODY()
	
public:	

	ANetMan();
	
	UPROPERTY(Replicated)
	FHealthCompContainer HealthTracker;

	UFUNCTION(BlueprintCallable)
	void RegisterActor(AActor* MyActor, const TArray<uint8>& Payload);

	UFUNCTION(BlueprintCallable)
	void UpdateActor(AActor* MyActor, const TArray<uint8>& Payload);

public:

	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

};
{% endhighlight %}

As we've seen above, the `FHealthCompItem` contains a copy constructor to ease registering/adding members in the `FHealthCompContainer`:

{% highlight c++ %}
ANetMan::ANetMan()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

void ANetMan::RegisterActor(AActor* MyActor, const TArray<uint8>& Payload)
{
	if (UHealthComponent* Hc = MyActor->FindComponentByClass<UHealthComponent>())
	{
		FHealthCompItem hci(Hc, Payload);
		HealthTracker.MarkItemDirty(HealthTracker.Items.Add_GetRef(hci));
	}
}

void ANetMan::UpdateActor(AActor* MyActor, const TArray<uint8>& Payload)
{
	FHealthCompItem* FoundEntry = HealthTracker.Items.FindByPredicate([MyActor](const FHealthCompItem& InItem)
		{
			return InItem.OwnerActor == MyActor;
		});
	FoundEntry->Data = Payload;
	HealthTracker.MarkItemDirty(*FoundEntry);
}

void ANetMan::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANetMan, HealthTracker);
}
{% endhighlight %}

Network managers can work with the [relevancy](https://docs.unrealengine.com/4.26/en-US/InteractiveExperiences/Networking/Actors/Relevancy/#relevancy) system, but it's not very convenient as we have a few of them, in this case, I decided to make them always relevant. The following two functions describe the core functionalities we require in our network managers:

- `RegisterActor:` Adds an element to our fast array constructing a `FHealthCompItem` that gets added to the array.
- `UpdateActor`: Updates the data of an element in our fast array. For that, we find the owner actor within the array and update the data on the found entry.

## The health component
Our health component gets registered in `BeginPlay`:

{% highlight c++ %}
void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	MyNetMan = Cast<ANetMan>(UGameplayStatics::GetActorOfClass(this, ANetMan::StaticClass()));
	if (MyNetMan.IsValid())
	{
		MyNetMan->RegisterActor(GetOwner(), Encode());
	}
}
{% endhighlight %}

And communicates with the network manager through events:

{% highlight c++ %}
void UHealthComponent::DoDamage(float Damage)
{
	ensureAlways(GetOwner()->HasAuthority());

	Health -= Damage;
	Shield = Damage;
	if (Health <= 0)
	{
		Health = 0;
		Die();
	}
	
	if (MyNetMan.IsValid())
	{
		MyNetMan.Get()->UpdateActor(GetOwner(), Encode());
	}
}
{% endhighlight %}

The `PostReplication` function handles the client data:

{% highlight c++ %}
void UHealthComponent::PostReplication(const TArray<uint8>& Payload)
{
	Decode(Payload);
	if (Health <= 0)
	{
		Die();
	}
}
{% endhighlight %}

As we can see, `Decode` and `Encode` are being called as our network manager handles generic data in the form of a byte array:

{% highlight c++ %}
TArray<uint8> UHealthComponent::Encode()
{
	TArray<uint8> Payload;
	FMemoryWriter Ar(Payload);
	Ar << Health;
	Ar << Shield;
	return Payload;
}

void UHealthComponent::Decode(const TArray<uint8>& Payload)
{
	FMemoryReader Ar(Payload);
	Ar << Health;
	Ar << Shield;
}
{% endhighlight %}

These two functions have the following responsibilities:
- `Encode` (Server): It writes the values stored in `Health` and `Shield` in a `Tarray<uint8>` using `FMemoryWriter`. These values will then be passed to the `FHealthCompContainer` to update the fast array values.
- `Decode` (Client): Once the `FHealthCompContainer` replicates, `PostReplicatedChange` is called in the fast array, that calls `PostReplication`, which receives the Payload that gets rearranged back in two variables using `FMemoryReader`.

And with that, `Health` and `Shield` are set appropriately in the client.

# Conclusion

With just a few lines of code, we have optimised the CPU time consumed on the server by processing hundreds of replicated Actors through a Network manager.

Network managers are a great solution in cases where we have to handle lots of actors of the same type, both for performance and responsiveness.

Before I finish, I would like to thank [Jambax](https://jambax.co.uk/) and Zlo for introducing and explaining these concepts in the [Unreal Slackers discord](https://unrealslackers.org/), which motivated me to do my own research and create this post.

Enjoy, vori.
