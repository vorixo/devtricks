---
layout: single
title: "Iris - Replication filters"
excerpt: In this post we'll learn how to use simple replication filters in Unreal Engine Iris' networking system.
header:
  teaser: /assets/images/per-post/iris-replication-filter/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Networking
  - Iris
---

In this article we will dive a tiny little bit on Iris' replication system and we'll learn how to setup a simple replication filter.

# Introduction - Being an early adopter

Most of my articles/tutorials revolve around the vanilla replication system of Unreal Engine, which is still pretty industry standard to date. However, Epic's been cooking now for several years a new implementation to their replication system called Iris.

[Iris](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-iris-in-unreal-engine) has been designed from the ground up to minimize the CPU overhead introduced by property replication by maintaining a quantized copy of all replicated data, sharing work amongst connections, providing thus, the opportunity to do more work in parallel; alongside Iris' default push-based replication model.

Epic's intention with Iris is for it to become industry standard, some games, like Splitgate, are making use of it. Although as Epic points out on the [Iris' FAQ](https://dev.epicgames.com/community/learning/tutorials/z08b/unreal-engine-iris-faq) - the system is still Experimental and lacks support of basic features like [replays](https://dev.epicgames.com/documentation/en-us/unreal-engine/demonetdriver-and-streamers-in-unreal-engine).

With that said, early adoption helps the industry tremendously to uncover hidden bugs and priorize developments efforts, and sometimes, even cross-collaborations arise! See the recent CD Projekt + Epic work in The Witcher 4 technical demo:

<iframe width="480" height="270" src="https://www.youtube.com/embed/ji0Hfiswcjo" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

# The replication filter

A [replication filter](https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-filtering-in-unreal-engine) is a system that determines what objects are replicated to which connections. 

We've done this already in my previous [proc-gen article](https://vorixo.github.io/devtricks/procgen/) overriding Unreal's legacy `AActor::IsNetRelevantFor` function. This function is evaluated whenever the Actor's replication is considered for update, which is normally tied to its `NetUpdateFrequency`. Returning `false` for an arbitrary connection would mean that such connection won't get sent the last replication update for the Actor.

{% highlight c++ %}
bool AProjectile::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (const APlayerController* PC = Cast<APlayerController>(RealViewer))
	{
		// The projectile's owner is never relevant since it runs its own local simulation
		const APlayerController* ProjectileOnwer = GetPlayerOwner();
		return (GetPlayerOwner() != PC) && (Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation));
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}
{% endhighlight %}

In the code above, we are disallowing replication to the projectile's owner. As pointed aswell in the procgen article, there are probably better ways to do this through the [Replication Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/replication-graph-in-unreal-engine).

## The replication filter in Iris

However, Iris does not support the Replication Graph, since it has its own scheme for prioritizing and filtering objects to different connections. In this section, we'll learn how to setup a simple replication filter in Iris, mimicking the example we saw above.

{% highlight c++ %}
void AProjectile::BeginPlay()
{
	Super::BeginPlay();
	
	if (HasAuthority())
	{
		UReplicationSystem* ReplicationSystem = UE::Net::FReplicationSystemUtil::GetReplicationSystem(this);
		UEngineReplicationBridge* ActorReplicationBridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(this);
		UE::Net::FNetRefHandle RepActorNetRefHandle = ActorReplicationBridge->GetReplicatedRefHandle(this);

		constexpr int32 MaxConnections = 100; // Replace for max amount of connections supported on your game.
		TBitArray<> Connections;
		Connections.Init(false, MaxConnections);

		const APlayerController* PlayerController = GetPlayerOwner();
		if (PlayerController && PlayerController->GetNetConnection())
		{
			UE::Net::FConnectionHandle ConnectionId = PlayerController->GetNetConnection()->GetConnectionHandle();
			Connections.Insert(true, ConnectionId.GetParentConnectionId());
			ReplicationSystem->SetConnectionFilter(RepActorNetRefHandle, Connections, UE::Net::ENetFilterStatus::Disallow);
		}
	}
}
{% endhighlight %}

In the above code we've disallowed replication to the projectile owner player. Note how the above code is event-driven rather than poll-based.

Similar logic applies to add allowed connections by using `UE::Net::ENetFilterStatus::Allow`.
{: .notice--info}

The same way, to clear the filter, we can simply do the following:

{% highlight c++ %}
void AProjectile::ClearFilter()
{	
	if (HasAuthority())
	{
		UReplicationSystem* ReplicationSystem = UE::Net::FReplicationSystemUtil::GetReplicationSystem(this);
		UEngineReplicationBridge* ActorReplicationBridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(this);
		UE::Net::FNetRefHandle RepActorNetRefHandle = ActorReplicationBridge->GetReplicatedRefHandle(this);

		TBitArray<> NoConnections;
		ReplicationSystem->SetConnectionFilter(RepActorNetRefHandle, NoConnections, UE::Net::ENetFilterStatus::Disallow);

	}
}
{% endhighlight %}

With this, now you know how to skip or allow replication to specific connections, however, this is just a start!

# Additional resources

Besides this post, you can also take a look in these resources/files:
- `Engine\Plugins\Runtime\ReplicationSystemTestPlugin\Source\Private\Tests\ReplicationSystem\Filtering\TestFiltering.cpp`: A test suite for filtering replication to connections.
- [Official Epic filtering documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-filtering-in-unreal-engine): Provides more in depth examples as well as other types of filtering - such as groups.

Finally, I strongly recommend everyone to take a look at Epic's Unreal Fest talk about Iris, where they dive into the design behind the Iris replication system:

<iframe width="480" height="270" src="https://www.youtube.com/embed/K472O2rVvG0" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

Thanks Epic!

# Conclusion

Today we explored together a simple way to set up replication filters in Iris with a very trivial example.

As I always say, I encourage you all to do your own experiments and exploration, let this article serve as a helping hand to get started in the matter!

As always, feel free to [contact me](https://twitter.com/vorixo) (and follow me! hehe) if you find any issues with the article or have any questions about the whole topic.

Enjoy, vori.