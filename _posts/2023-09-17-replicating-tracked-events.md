---
layout: single
title: "Replicating stateful sounds and animations"
excerpt: In this post we'll learn a pattern to replicate long stateful songs and animations that depend on time.
header:
  teaser: /assets/images/per-post/replicating-tracked-events/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Animations
  - Sound
  - Networking
---

Hello! In this post we'll expose a very simple pattern you can use to replicate any long **stateful** sound or animation. In addition, we'll learn about some caveats when it comes to **replication order guarantees**.

# Introduction

There are times when we want to replicate a long animation and still have it playing in the correct timeframe when a client joins. While the [Gameplay Ability System](https://docs.unrealengine.com/5.1/en-US/gameplay-ability-system-for-unreal-engine/) does this with the [`PlayMontageAndWait`](https://docs.unrealengine.com/4.27/en-US/BlueprintAPI/Ability/Tasks/PlayMontageAndWait/) node, GAS is not always appropriate for all of our use cases.

The same problem also occours with music or other animated elements, such as an elevator. In all the cases, we want [late joiners](https://vorixo.github.io/devtricks/stateful-events-multiplayer/#towards-relevancy-resilient-code) to see our ingredients in the correct state by the time they join.

# The solution

So... how can we solve this? The solution relies on using time! Yes, that simple!

If we store and replicate in a variable the time at which an action took place, we can figure out by the time someone joins to our game, where the animation/sound should be at for that client. Let's picture it this way:

  1. Player 1 starts playing an animation at t0 = 34 s
  2. Player 2 joins the game at t1 = 38 s
  3. By the time Player 2 joined, the animation has already advanced (t1 - t0) 4 seconds
  4. Player 2 should start playing the animation of Player 1 at t = 4 s.
  
Let's see how we can translate this to code.

## A practical example

In our imaginary game, characters can play the flute, and we want the melody to be synchronised between them, but that isnt complicated, since a Multicast can do the job. However, the melody is long, and we want incoming connections to hear the melody properly even if the player started playing it at an earlier time. These requirements made already the feature stateful; so... we have to discard Multicasts. 

## Coding our solution

One of the first concepts we should understand before coding this solution is that the [`GameState` is guaranteed to be valid when any Actor on client calls `BeginPlay`](https://wizardcell.com/unreal/multiplayer-tips-and-tricks/#1522-gamestate-replication-timing-guarantee), this is important since we will rely on a `GameState` replicated clock to set the correct animation/sound state.

To replicate to our simulated proxies the time at which we started playing the melody we are going to use a replicated property `LastTimePlayedMelody` that replicates only to simulated proxies.

{% highlight c++ %}
// Represents the time at which the melody was played
UPROPERTY(ReplicatedUsing = OnRepLastTimePlayedMelody)
float LastTimePlayedMelody = 0.f;

void AMyPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// We make sure only simulated proxies get this repli property
	DOREPLIFETIME_CONDITION(AMyPawn, LastTimePlayedMelody, COND_SimulatedOnly);
}
{% endhighlight %}

To play the melody at a specific time we are going to use `SpawnSoundAttached`, since it contains a parameter `StartTime` that we can input to start the sound at a time. For that, we created the following function that we should call **locally**, which starts playing a melody inmediately. As we can see, the function also notifies the server so that the rest of the clients can also play it.

{% highlight c++ %}
void AMyPawn::PlayMelody(float StartTime)
{
	UGameplayStatics::SpawnSoundAttached(FluteSong, GetMesh(), NAME_None, 
		FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, 
		false, 1.f, 1.f, StartTime);

	if (IsLocallyControlled())
	{
		if (HasAuthority())
		{
			LastTimePlayedMelody = GetWorld()->GetTimeSeconds();
		}
		else
		{
			ServerPlayMelody();
		}
	}
}

void AMyPawn::ServerPlayMelody()
{
	LastTimePlayedMelody = GetWorld()->GetTimeSeconds();
	if (GetWorld()->GetNetMode() != ENetMode::NM_DedicatedServer)
	{
		PlayMelody(0.f);
	}
}
{% endhighlight %}

In the server we should set `LastTimePlayedMelody`, which will replicate through the OnRep. In the case we are in a Listen Server, we should also play the melody.

Then, in the OnRep we do as follows:

{% highlight c++ %}
void AMyPawn::OnRepLastTimePlayedMelody()
{
	if (GetWorld()->TimeSeconds == CreationTime)
	{
		return;
	}

	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState)
	{
		return;
	}

	const float SoundTime = GameState->GetServerWorldTimeSeconds() - LastTimePlayedMelody;

	if (SoundTime < FluteSong->GetDuration())
	{
		PlayMelody(SoundTime);
	}
}
{% endhighlight %}

We calculate the time at which the sound should be at by substracting the current server time `GameState->GetServerWorldTimeSeconds()` with the OnRep variable. If the SoundTime is lesser than the total duration of the Sound, we can start playing the sound at the exact point!

If we were to use the same technique with animations, we can use `UAnimInstance::Montage_Play` which also has the analogous (`float InTimeToStartMontageAt`).
{: .notice--info}

## Problems: Late joiners

However, we are not done. If we execute the code from above we will notice that our late joiners dont execute anything, this is because in the OnRep code from above, we are doing two very specific conditions:
{% highlight c++ %}
if (GetWorld()->TimeSeconds == CreationTime)
{
	return;
}

AGameStateBase* GameState = GetWorld()->GetGameState();
if (!GameState)
{
	return;
}
{% endhighlight %}

These two conditions basically skip the OnRep, as we dont want to use it on the first spawn packet. But why? We do this because the GameState isnt guaranteed to be replicated at the time our OnRep gets called. Then, how do we solve this?

As we mentioned previously:

```
GameState is guaranteed to be valid when any Actor on client calls BeginPlay.
```

So... let's code our late join support in `BeginPlay`!

### BeginPlay guarantees

As I said above, `GameState` will be valid when client-side `BeginPlay` is called. 

However, having a valid `GameState` isn't enough, since we also need to have up-to-date properties by the time `BeginPlay` is called on the client. Now, we can only guarantee that our properties have replicated by then in runtime spawned replicated Actors. The Actors that are pre-placed in the level (also called [Net Startup Actors](https://vorixo.github.io/devtricks/procgen/#net-startup-actor-and-net-addressable)) don't guarantee correct replicated state in `BeginPlay`.

So, in this case, we can safely assume that, in our Character, since it isn't a *Net Startup Actor*, the variables will be up to date for the client in `BeginPlay`:

{% highlight c++ %}
void AMyPawn::BeginPlay()
{
	Super::BeginPlay();

	if (GetLocalRole() == ENetRole::ROLE_SimulatedProxy)
	{
		AGameStateBase* GameState = GetWorld()->GetGameState();
		const float SoundTime = GameState->GetServerWorldTimeSeconds() - LastTimePlayedMelody;

		if (SoundTime < FluteSong->GetDuration())
		{
			PlayMelody(SoundTime);
		}
	}
}
{% endhighlight %}

When we execute the `BeginPlay` of other Characters (simulated proxies) in our client, we can access the `GameState` (which will be valid) to retrieve `GetServerWorldTimeSeconds`, and then using the same technique we saw in the OnRep, we can play the melody at the specified time.

### What about Net Startup Actors?

In the case of Net Startup Actors we can't rely on `BeginPlay`, so we wouldn't put any initialization specific code on it. 

In this case, we can develop a more generic solution... however... it is very ugly:

{% highlight c++ %}
void AMyNetStartupActor::OnRepLastTimePlayedMelody()
{
	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState)
	{
		// Silly hack to prevent early initialization, if the GameState isn't valid we wait until it is to be able to get our clock
		FTimerHandle DummyTimer;
		GetWorld()->GetTimerManager().SetTimer(DummyTimer, this, &ThisClass::OnRepLastTimePlayedMelody, 0.1f, false);
		return;
	}

	const float SoundTime = GameState->GetServerWorldTimeSeconds() - LastTimePlayedMelody;

	if (SoundTime < FluteSong->GetDuration())
	{
		PlayMelody(SoundTime);
	}
}
{% endhighlight %}

Unfortuntely, waiting for the `GameState` is the only solution. We can implement this in a more elegant manner orchestrating our initialization with a subsystem or other methods, but this inconsistency between Net Startup and non Net Startup Actors makes networking more difficult, by default.

I really wish this gets changed in the future, but to date, this is our safest bet.

# Conclusion

Thanks for reading! 

This article covers a very simplistic pattern you can use to replicate stateful tracked ingredients, such as sounds or animations, and also covers the problem between Net Startup and non Net Startup Actors.

There are many things that could be improved about the techniques presented in the article, such as using a [better synced clock](https://vorixo.github.io/devtricks/non-destructive-synced-net-clock/), improving the waiting `GameState` method, or even employing another techniques to replicate tracks without depending on time.

However you got a chance to reason about different initialization techniques, and I hope that this article made you understand how important order is in multiplayer.

Enjoy, [vori](https://twitter.com/vorixo).