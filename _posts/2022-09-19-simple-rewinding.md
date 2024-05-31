---
layout: single
title: "Rewinding a networked game"
excerpt: In this post you'll learn the principles about network rewinding, and we'll provide a very simple reconciliation implementation for Unreal Engine.
header:
  teaser: /assets/images/per-post/simple-rewinding/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - Rewinding
  - UE5
  - UE4
  - Security
  - ShooterGame
---

In this writeup we provide a continuation to the [previous article](https://vorixo.github.io/devtricks/shootergame-vulnerability/) and showcase a somewhat nice solution to the exposed vulnerability.

# Introduction

In the previous post we showcased a [ShooterGame exploit](https://vorixo.github.io/devtricks/shootergame-vulnerability/) found by the Unreal community that affected several projects that use ShooterGame as a base. After [some digging](https://twitter.com/flassari/status/1556383864947412994?s=20&t=l-OrgFlE5AybAZlRjclA8g), we found that this same exploit is present in [Lyra](https://docs.unrealengine.com/5.0/en-US/lyra-sample-game-in-unreal-engine/), which was expected, since the sample provides no server validation code in its source code.

The article produced several reactions that went in different directions, like: "Is it really a [network vulnerability](https://en.wikipedia.org/wiki/Vulnerability_(computing))?" or "Shall it be fixed?". In addition, in my [twitter post](https://twitter.com/vorixo/status/1556251193114071041?s=20&t=KI5WuVOO9A0xcEbpq7qq-Q) I promised a more elaborated solution for shoot validation involving rewinding.

So... Let's get started! But first, lets clear out the remaining questions.

## Is it really a network vulnerability?

I agree that I was too catastrophic when deciding the title for the article, as we traditionally understand a network vulnerability as a flaw that directly affects the network, and not the application. However, we can classify the issue as a server-side application-level vulnerability according to [ENISA](https://www.enisa.europa.eu/):

> **Vulnerability**: The existence of a weakness, design, or implementation error that can lead to an unexpected, undesirable event ... compromising the security of the computer system, network, application, or protocol involved.

In summary, I think it is absolutely correct to use a broader term to include all types of vulnerabilities that interact with the network layer, hence the title I chose.

## Shall it be fixed?

This reaction is natural, as Lyra is a sample that is supposed to be employed to create many different games, and each one likely requires different validation heuristics:

<iframe width="480" height="270" src="https://www.youtube.com/embed/m80NJzUWq8A?start=4435" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

Like [Michael Noland](https://twitter.com/joatski) mentioned in the above video, generalising validation heuristics is complicated as each game has different requirements.

With that said, I think Lyra is the best opportunity for Epic to provide a scalable validation solution, like Fortnite's, for people to learn best practices when building big, enjoyable and secure games.

# Rewinding a networked game

But first... What is rewinding? As the word suggests, rewinding consists of returning to a previous state of any entity in the past. But, why is this useful?

Have you ever heard of ping? Latency is the biggest enemy of networked games, since every time we perform a predictive action on our local client, it reaches the server *ping* milliseconds later. For example, many shooters perform predictive shooting, where we don't wait for the server to process our input request, but fire our weapon locally on the client. This provides highly responsive gameplay that our players will love, but opens some room to cheaters.

There are many different exploits that require sanitization if we perform predictive shooting, here are the most popular ones:
- **Modifying the rate of fire:** This exploit consists in increasing the rate of fire of our weapon, it can be sanitized by calculating the timestamp difference between consecutive shoots canceling those that violate the defined rate of fire.
- **Shooting at impossible angles and locations:** This exploit consists in shooting in directions impossible for our current location and rotation, it can be sanitized by ensuring the client-side shooting direction and location against the server with some tolerance (for ping).
- **[Modifying the position of our victims to easily kill them](https://vorixo.github.io/devtricks/shootergame-vulnerability/):** As mentioned in the linked article, this exploit consists in modifying the location of an enemy player in our local client so we can easily kill them, it can be sanitized by ensuring that the shoot impact falls within the server side enemy bounding box, accounting with some tolerance (for ping). 

Note that most of the sanitization approaches noted above can be strategically improved given the context of the videogame. For example a competitive shooter like Valorant or CSGO [might require a more precise (but "expensive") method in order to validate the shoots](https://www.unrealengine.com/en-US/tech-blog/valorant-s-foundation-is-unreal-engine).
{: .notice--info}

By rewinding the game by the instigator's latency time when processing the server-side shot, we ensure that the victim is at the same position and rotation where the instigator saw it locally at timestamp *t*, meaning that we can compute [accurate bounding box computations on the server equivalent to what the instigator viewed locally](https://youtu.be/zrIY0eIyqmI?t=2146) in the past time *t*.

![Rewinding a pawn]({{ '/' | absolute_url }}/assets/images/per-post/simple-rewinding/rewind-expl.gif){: .align-center}

The above gif ilustrates the problem, **the red box showcases where the server saw the pawn**, and **the white box displays where the client instigator saw it locally**. As we can see, there is a difference between the server position and the client position. With that said, let's build a rewinding solution that we can use to rewind by *client latency* any movable object in our game.

## A generic rewinding component

To keep things simple I have decided to create a component that we can reuse on any Actor we want to rewind. Our rewind method rewinds the bounding box of the victim Actor, however, as I mentioned earlier, depending on the game you are making you might need a more involved solution.

Part of the rewinding code is inspired on [Unreal Tournament 4](https://github.com/EpicGames/UnrealTournament) rewinding solution, which implements **character capsule rewinding**, instead of bounding box rewinding. 

First, we start by creating a `UActorComponent` that holds all the data and functions needed to rewind our Actors:

{% highlight c++ %}
#pragma once

#include "Components/ActorComponent.h"
#include "ShooterRewindableComponent.generated.h"

USTRUCT(BlueprintType)
struct FSavedMove
{

	GENERATED_BODY()

	/** Hitbox of Actor at time Time. */
	UPROPERTY()
	FBox Hitbox;

	/** True if a teleport occurred getting to current position (so don't interpolate) */
	UPROPERTY()
	bool bTeleported;

	/** Current server world time when this position was updated. */
	UPROPERTY()
	float Time;

	FSavedMove() : Hitbox(EForceInit::ForceInit), bTeleported(false), Time(0.f) {};

	FSavedMove(const FBox &InHitbox, bool bInTeleported, float InTime) : Hitbox(InHitbox), bTeleported(bInTeleported), Time(InTime) {};

};


UCLASS(meta = (BlueprintSpawnableComponent))
class SHOOTERGAME_API UShooterRewindableComponent : public UActorComponent
{
	GENERATED_BODY()

	UShooterRewindableComponent();

public:

	/** Updates the saved moves array */
	void UpdateSavedMoves(bool bInteleported);

	/** Returns this character's position in the desired timestamp */
	FBox GetRewoundHitbox(float InTime) const;
	
	/** Ticks */
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Call this whenever you teleport the owning pawn **/
	void SetJustTeleported(bool bInTeleported);

protected:

	uint8 bJustTeleported : 1;

	UPROPERTY()
	TArray<FSavedMove> SavedMoves;
};
{% endhighlight %}

The `SavedMoves` array is an array of type `FSavedMove` which holds a history of bounding boxes and times. We can consult this array to retrieve the Actor's bounding box some miliseconds ago. The `SavedMoves` array updates every tick removing the oldest entry (at the begining of the array), and adds a new one (at the end of the array):

{% highlight c++ %}
void UShooterRewindableComponent::UpdateSavedMoves(bool bInTeleported)
{
	const float WorldTime = GetWorld()->GetTimeSeconds();
	new(SavedMoves)FSavedMove(GetOwner()->GetComponentsBoundingBox(), bInTeleported, WorldTime);

	// maintain one position beyond MaxSavedPositionAge for interpolation
	if (SavedMoves.Num() > 1 && SavedMoves[1].Time < WorldTime - 0.5f)
	{
		SavedMoves.RemoveAt(0);
	}
}


void UShooterRewindableComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner()->HasAuthority())
	{
		UpdateSavedMoves(bJustTeleported);
		bJustTeleported = false;
	}
}
{% endhighlight %}

`UShooterRewindableComponent::UpdateSavedMoves` keeps a hitbox history of approximately 500 ms, any shoot that exceeds this history should be rejected. However, some games might find that performing predictive shooting beyond 200 ms is unfair for the gameplay experience, so a solution in which we do [authorative shooting after some ping threshold](https://youtu.be/YWJGowdcwOU?t=256) might bring the best of both worlds (if the instigator has high ping, they should **latency-lead** their shoots, thus no rewinding involved).

As we can see, the history is only kept on authority, since the server is the responsible of rewinding our Actors to the requested time:

{% highlight c++ %}
FBox UShooterRewindableComponent::GetRewoundHitbox(float TargetTime) const
{
	FBox TargetHitbox = GetOwner()->GetComponentsBoundingBox();
	FBox PreHitbox = TargetHitbox;
	FBox PostHitbox = TargetHitbox;
	float PredictionTime = GetWorld()->GetTimeSeconds() - TargetTime;
	float Percent = 0.999f;
	bool bTeleported = false;
	if (PredictionTime > 0.f)
	{
		for (int32 i = SavedMoves.Num() - 1; i >= 0; i--)
		{
			TargetHitbox = SavedMoves[i].Hitbox;
			if (SavedMoves[i].Time < TargetTime)
			{
				if (!SavedMoves[i].bTeleported && (i < SavedMoves.Num() - 1))
				{
					PreHitbox = SavedMoves[i].Hitbox;
					PostHitbox = SavedMoves[i + 1].Hitbox;
					if (SavedMoves[i + 1].Time == SavedMoves[i].Time)
					{
						Percent = 1.f;
						TargetHitbox = SavedMoves[i + 1].Hitbox;
					}
					else
					{
						Percent = (TargetTime - SavedMoves[i].Time) / (SavedMoves[i + 1].Time - SavedMoves[i].Time);
						const FVector Min = FMath::Lerp(SavedMoves[i].Hitbox.Min, SavedMoves[i + 1].Hitbox.Min, Percent);
						const FVector Max = FMath::Lerp(SavedMoves[i].Hitbox.Max, SavedMoves[i + 1].Hitbox.Max, Percent);
						TargetHitbox = FBox(Min, Max);
					}
				}
				else
				{
					bTeleported = SavedMoves[i].bTeleported;
				}
				break;
			}
		}
	}

	DrawDebugBox(GetWorld(), GetOwner()->GetComponentsBoundingBox().GetCenter(), GetOwner()->GetComponentsBoundingBox().GetExtent(), FColor::Red, false, 8.f, 0U, 3.f);
	DrawDebugBox(GetWorld(), TargetHitbox.GetCenter(), TargetHitbox.GetExtent(), FColor::Yellow, false, 8.f, 0U, 3.f);
	DrawDebugBox(GetWorld(), PreHitbox.GetCenter(), PreHitbox.GetExtent(), FColor::Blue, false, 8.f, 0U, 3.f);
	DrawDebugBox(GetWorld(), PostHitbox.GetCenter(), PostHitbox.GetExtent(), FColor::White, false, 8.f, 0U, 3.f);

	return TargetHitbox;
}
{% endhighlight %}

The above rewinding function creates in-between bounding boxes by lerping the closest entries to the requested time, in case the input time doesn't exist in our history `SavedMoves` array. At the same time, it also supports teleports, so we don't lerp between a teleported bounding box and its previous entry. However, this functionality shall be supported by the end user.

## Rewinding our Actors

To use our rewinding component, on **authority** we can rewind any Actor by doing the following:

{% highlight c++ %}
const UShooterRewindableComponent* Rew = Cast<UShooterRewindableComponent>(Victim->GetComponentByClass(UShooterRewindableComponent::StaticClass()));

// Get the component bounding box
const FBox HitBox = Rew ? Rew->GetRewoundHitbox(TimeStamp) : Victim->GetComponentsBoundingBox();
{% endhighlight %}

The main requirement needed for this functionality to work optimally is to implement a [precise synced network clock](https://vorixo.github.io/devtricks/non-destructive-synced-net-clock/). Once this is done we will be able to use it in any game, e.g. [ShooterGame](https://docs.unrealengine.com/4.27/en-US/Resources/SampleGames/ShooterGame/):

{% highlight c++ %}
void AShooterWeapon_Instant::ProcessInstantHit(const FHitResult& Impact, const FVector& Origin, const FVector& ShootDir, int32 RandomSeed, float ReticleSpread)
{
	if (MyPawn && MyPawn->IsLocallyControlled() && GetNetMode() == NM_Client)
	{
		// if we're a client and we've hit something that is being controlled by the server
		if (Impact.GetActor() && Impact.GetActor()->GetRemoteRole() == ROLE_Authority)
		{
			// notify the server of the hit
			ServerNotifyHit(Impact, ShootDir, RandomSeed, ReticleSpread, UMyGameplayStatics::GetServerWorldTime(this));
		}
		else if (Impact.GetActor() == NULL)
		{
			if (Impact.bBlockingHit)
			{
				// notify the server of the hit
				ServerNotifyHit(Impact, ShootDir, RandomSeed, ReticleSpread, UMyGameplayStatics::GetServerWorldTime(this));
			}
			else
			{
				// notify server of the miss
				ServerNotifyMiss(ShootDir, RandomSeed, ReticleSpread);
			}
		}
	}

	// process a confirmed hit
	ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
}

void AShooterWeapon_Instant::ServerNotifyHit_Implementation(const FHitResult& Impact, FVector_NetQuantizeNormal ShootDir, int32 RandomSeed, float ReticleSpread, float TimeStamp)
{
	const float WeaponAngleDot = FMath::Abs(FMath::Sin(ReticleSpread * PI / 180.f));

	// if we have an instigator, calculate dot between the view and the shot
	if (GetInstigator() && (Impact.GetActor() || Impact.bBlockingHit))
	{
		const FVector Origin = GetMuzzleLocation();
		const FVector ViewDir = (Impact.Location - Origin).GetSafeNormal();

		// is the angle between the hit and the view within allowed limits (limit + weapon max angle)
		const float ViewDotHitDir = FVector::DotProduct(GetInstigator()->GetViewRotation().Vector(), ViewDir);
		if (ViewDotHitDir > InstantConfig.AllowedViewDotHitDir - WeaponAngleDot)
		{
			if (CurrentState != EWeaponState::Idle)
			{
				if (Impact.GetActor() == NULL)
				{
					if (Impact.bBlockingHit)
					{
						ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
					}
				}
				// assume it told the truth about static things because the don't move and the hit 
				// usually doesn't have significant gameplay implications
				else if (Impact.GetActor()->IsRootComponentStatic() || Impact.GetActor()->IsRootComponentStationary())
				{
					ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
				}
				else
				{
					const UShooterRewindableComponent* Rew = Cast<UShooterRewindableComponent>(Impact.GetActor()->GetComponentByClass(UShooterRewindableComponent::StaticClass()));

					// Get the component bounding box
					const FBox HitBox = Rew ? Rew->GetRewoundHitbox(TimeStamp) : Impact.GetActor()->GetComponentsBoundingBox();

					// calculate the box extent, and increase by a leeway
					FVector BoxExtent = 0.5 * (HitBox.Max - HitBox.Min);
					BoxExtent *= InstantConfig.ClientSideHitLeeway;

					// avoid precision errors with really thin objects
					BoxExtent.X = FMath::Max(20.0f, BoxExtent.X);
					BoxExtent.Y = FMath::Max(20.0f, BoxExtent.Y);
					BoxExtent.Z = FMath::Max(20.0f, BoxExtent.Z);

					// Get the box center
					const FVector BoxCenter = (HitBox.Min + HitBox.Max) * 0.5;

					// if we are within client tolerance
					if (FMath::Abs(Impact.Location.Z - BoxCenter.Z) < BoxExtent.Z &&
						FMath::Abs(Impact.Location.X - BoxCenter.X) < BoxExtent.X &&
						FMath::Abs(Impact.Location.Y - BoxCenter.Y) < BoxExtent.Y)
					{
						ProcessInstantHit_Confirmed(Impact, Origin, ShootDir, RandomSeed, ReticleSpread);
					}
					else
					{
						UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s (outside bounding box tolerance)"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
					}
				}
			}
		}
		else if (ViewDotHitDir <= InstantConfig.AllowedViewDotHitDir)
		{
			UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s (facing too far from the hit direction)"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
		}
		else
		{
			UE_LOG(LogShooterWeapon, Log, TEXT("%s Rejected client side hit of %s"), *GetNameSafe(this), *GetNameSafe(Impact.GetActor()));
		}
	}
}
{% endhighlight %}

In the above code, `AShooterWeapon_Instant::ProcessInstantHit` is called locally, and forwards all the information about the shot to the server, including our synchronized timestamp so we can rewind our victim by that factor.

With all of this done, you only have to place the rewinding component (`UShooterRewindableComponent`) in all the Actors you wish to rewind (characters, movable platforms, vehicles...) in order to ensure the validity of the predicted shots.

![Component list]({{ '/' | absolute_url }}/assets/images/per-post/simple-rewinding/components.jpg){: .align-center}

# Conclusion: Are we done?

So... Are we done? Not really.

Our rewind method is not sufficient, since we are rewinding only the Actor we are shooting at. Ideally we should rewind all potential Actors we might hit in our scene for latency time, so we make sure the client doesn't hit a character through another moving Actor. 

In addition, as [Jay Mattis](https://x.com/braindx) mentions in [his article about lag compensation](https://snapnet.dev/blog/performing-lag-compensation-in-unreal-engine-5/), the Character Movement Component performs extrapolation and smoothing in the simulated proxies. This means that at times, we will see the sim proxy characters in positions that never existed on the server.

However, I'm going to leave the hands-on part of that exercise to you, the reader. I'd like you to research a solution so we can discuss it in [twitter](https://twitter.com/vorixo) together. I think going through this type of thought process can help you learn more intricate problems about network programming in game development and all that it concerns.

This article linked a couple of resources that can help you in your research.

Remember that all the feedback is welcomed, I deeply appreciate corrections and contributions, so feel free! I don't bite!

Enjoy, vori.