---
layout: single
title: "Unreal Engine - A non-destructive and better synced network clock"
excerpt: Do you want to have a better synced clock in Unreal Engine without removing the default "synced" clock? This is your post!
header:
  teaser: /assets/images/per-post/netclock/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - Synced clock
  - UE5
  - UE4
  - Network clock
  - NTP clock
---

In this brief but necessary post we explore a non-destructive approach to a better network synced clock for Unreal Engine.

# Introduction

Time is very relevant in multiplayer, as it can provide a clear insight of when an event happened. It not only helps to provide a more accurate view of our [stateful systems](https://vorixo.github.io/devtricks/stateful-events-multiplayer/) across the network (when was this object interacted with?), but it is also very significant when it comes to action validation (ie: [rewinding](https://youtu.be/zrIY0eIyqmI?t=2146)).

The main difficulty in this topic is to achieve a Client clock that matches the Server clock as much as possible. With such clock we can calculate the time it takes for an event to reach the server and vice-versa (also known as round-trip time (RTT)).

Unreal tries to solve this problem providing the [`GetServerWorldTimeSeconds()`](https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/GameFramework/AGameStateBase/GetServerWorldTimeSeconds/) function, however, many users have found this clock to be [quite inaccurate for their use cases](https://github.com/EpicGames/UnrealEngine/pull/4418), while others find the old functionality fine.

# The new clock

[Replacing the old network clock](https://medium.com/@invicticide/accurately-syncing-unreals-network-clock-87a3f9262594) has already been explored, but, as mentioned above, it is not ideal for everyone. 

For that reason, this post provides a non-destructive implementation you can add to your projects while leaving the old clock intact.

### How does it work?

The clock employs [`PostNetInit`](https://docs.unrealengine.com/5.0/en-US/API/Runtime/Engine/GameFramework/AActor/PostNetInit/) in order to create a constant timer that updates the client clock every 10 seconds calling `RequestWorldTime_Internal`. This function sends a server RPC forwarding the client time, which then pongs back to the client alongside the server time to calculate the `ServerWorldTimeDelta` in `ClientUpdateWorldTime` employing a [NTP formula](https://en.wikipedia.org/wiki/Network_Time_Protocol) to adjust the client's local clock appropriately.

## Implementation

First, we inherit `APlayerController` and add the following in the header file:

{% highlight c++ %}
UCLASS()
class GAME_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

	...

#pragma region NetworkClockSync

private:

	float ServerWorldTimeDelta = 0.f;
	float ShortestRoundTripTime = BIG_NUMBER;

public:

	UFUNCTION(BlueprintPure)
	float GetServerWorldTimeDelta() const;

	UFUNCTION(BlueprintPure)
	float GetServerWorldTime() const;

	void PostNetInit() override;

private:

	void RequestWorldTime_Internal();
	
	UFUNCTION(Server, Reliable)
	void ServerRequestWorldTime(float ClientTimestamp);
	
	UFUNCTION(Client, Reliable)
	void ClientUpdateWorldTime(float ClientTimestamp, float ServerTimestamp);

#pragma endregion NetworkClockSync

};
{% endhighlight %}

Then, we implement our clock in the `cpp` of our `APlayerController`:

{% highlight c++ %}
float AMyPlayerController::GetServerWorldTimeDelta() const
{
	return ServerWorldTimeDelta;
}

float AMyPlayerController::GetServerWorldTime() const
{
	return GetWorld()->GetTimeSeconds() + ServerWorldTimeDelta;
}

void AMyPlayerController::PostNetInit()
{
	Super::PostNetInit();
	if (GetLocalRole() != ROLE_Authority)
	{
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::RequestWorldTime_Internal, 10.f, true);
		RequestWorldTime_Internal();
	}
}

void AMyPlayerController::RequestWorldTime_Internal()
{
	ServerRequestWorldTime(GetWorld()->GetTimeSeconds());
}

void AMyPlayerController::ClientUpdateWorldTime_Implementation(float ClientTimestamp, float ServerTimestamp)
{
	const float RoundTripTime = GetWorld()->GetTimeSeconds() - ClientTimestamp;
	if (RoundTripTime < ShortestRoundTripTime)
	{
		ShortestRoundTripTime = RoundTripTime;
		ServerWorldTimeDelta = ServerTimestamp - ClientTimestamp - ShortestRoundTripTime / 2.f;
	}
}

void AMyPlayerController::ServerRequestWorldTime_Implementation(float ClientTimestamp)
{
	const float Timestamp = GetWorld()->GetTimeSeconds();
	ClientUpdateWorldTime(ClientTimestamp, Timestamp);
}
{% endhighlight %}

And that's it, with this now you have a more accurate and non-destructive synced network clock.

### QOL functions

As an extra, I recommend adding these two functions to your Blueprint static function library:

{% highlight c++ %}
float UMyGameplayStatics::GetServerWorldTime(UObject* WorldContextObject)
{
	if (!WorldContextObject) return 0.f;
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return 0.f;
	UGameInstance* const GameInstance = World->GetGameInstance();
	if (!GameInstance) return 0.f;
	AMyPlayerController* const PlayerController = Cast<AMyPlayerController>(GameInstance->GetFirstLocalPlayerController(World));
	if (!PlayerController) return World->GetTimeSeconds();
	return PlayerController->GetServerWorldTime();
}

float UMyGameplayStatics::GetServerWorldTimeDelta(UObject* WorldContextObject)
{
	if (!WorldContextObject) return 0.f;
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return 0.f;
	UGameInstance* const GameInstance = World->GetGameInstance();
	if (!GameInstance) return 0.f;
	AMyPlayerController* const PlayerController = Cast<AMyPlayerController>(GameInstance->GetFirstLocalPlayerController(World));
	if (!PlayerController) return 0.f;
	return PlayerController->GetServerWorldTimeDelta();
}
{% endhighlight %}

Using the new synced clock static functions is as easy as follows:

![OnRep approach]({{ '/' | absolute_url }}/assets/images/per-post/netclock/netclock-nodes.jpg){: .align-center}

## Results

After the first sync-up, the new network clock provides a more accurate view of the server time than the native one, reducing drastically the deviation on high ping scenarios. The following example log was recorded on the [Third Person Template](https://docs.unrealengine.com/5.0/en-US/third-person-template-in-unreal-engine/) with the following Network Emulation settings:

- Emulation Target: Everyone
- Network Emulation Profile: Bad

Results:

| Experiment # | Server time | Old network clock | New network clock |
|-------|--------|---------|---------|
| 1 | 8.607961 | 8.257961 | 8.569201 |
| 2 | 14.769416 | 14.371296 | 14.774051 |
| 3 | 6.552305 | 6.215758 | 6.513541 |
| 4 | 4.971255 | 4.673519 | 4.93249 |
| 5 | 5.075128 | 4.772511 | 5.036363 |

# The problem with synced network clocks (in general)

No matter which clock you end up using, remember that you cannot trust the clock value in a late joining situation. This is because **we cannot ensure that the clock has synced right when we joined**. However, we can paliate this situation with a [`NetworkEventSubsystem`](https://cdn.discordapp.com/attachments/221799385611239424/992481326093574155/NetworkEventSubsystem.zip), in which we would halt our OnRep actions until the clock syncs.

# Conclusion

Finally, a non-intrusive synced network clock! 

As I always say, be sure to make your own experiments, and don't blindly trust randoms on the internet (me)! 

I would like to thank `Zlo#1654` from [Slackers](https://unrealslackers.org/) again for all the help he gave me to make this post. As well  all the people who support my articles, you are amazing. 

By the way, the best way to track when I post articles is on Twitter, so [don't be afraid to follow me](https://twitter.com/vorixo)! DM's are open!

Enjoy, vori.
