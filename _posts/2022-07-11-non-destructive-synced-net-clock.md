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

Unreal tries to solve this problem providing the [`GetServerWorldTimeSeconds()`](https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/GameFramework/AGameStateBase/GetServerWorldTimeSeconds/) function, however, many users have found this clock to be [inaccurate for their use cases](https://github.com/EpicGames/UnrealEngine/pull/4418), while others find its functionality fine.

# The new clock

[Replacing the old network clock](https://medium.com/@invicticide/accurately-syncing-unreals-network-clock-87a3f9262594) has already been explored, but, as mentioned above, it is not ideal for everyone. 

For that reason, this post provides a non-destructive implementation you can add to your projects while leaving the vanilla clock intact.

## How does it work?

The new clock employs [`PostNetInit`](https://docs.unrealengine.com/5.0/en-US/API/Runtime/Engine/GameFramework/AActor/PostNetInit/) in order to create a constant timer that updates the client clock at a constant rate (ie: every second) calling `RequestWorldTime_Internal`. This function sends a server RPC forwarding the client time, which then pongs back to the client alongside the server time to calculate the `ServerWorldTimeDelta` in `ClientUpdateWorldTime` employing a [NTP formula](https://en.wikipedia.org/wiki/Network_Time_Protocol) to adjust the client's local clock appropriately.

## Implementation
There are many approaches to implement this clock, in this article I provide two:
- [Based on shortest round-trip-time](https://vorixo.github.io/devtricks/simple-rewinding/#based-on-shortest-round-trip-time): It reduces the error by an order of magnitude versus the vanilla clock in hazardous conditions. Since it uses thes shortest round-trip time to adjust clocks, the client clock will be based on statistical outliers. This will make the final result less accurate with the benefit of a very simplistic O(1) implementation.
- [Based on a moving window discarding outliers](https://vorixo.github.io/devtricks/simple-rewinding/#based-on-a-moving-window-discarding-outliers): It reduces the average error versus the shortest round-trip-time approach. To do this, it employs a sliding window to hold the latest _n_ round-trip-times and computes an average over them discarding outliers to compute the "fairest RTT". This algorithm has a higher complexity but provides a more accurate end-result.

### Based on shortest round-trip-time

First, we inherit `APlayerController` and add the following in the header file:

{% highlight c++ %}
UCLASS()
class GAME_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

	...

#pragma region NetworkClockSync

protected:

	/** Frequency that the client requests to adjust it's local clock. Set to zero to disable periodic updates. */
	UPROPERTY(EditDefaultsOnly, Category=GameState)
	float NetworkClockUpdateFrequency = 5.f;

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
	
	UFUNCTION(Server, Unreliable)
	void ServerRequestWorldTime(float ClientTimestamp);
	
	UFUNCTION(Client, Unreliable)
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
		RequestWorldTime_Internal();
		if (NetworkClockUpdateFrequency > 0.f)
		{
			FTimerHandle TimerHandle;
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::RequestWorldTime_Internal, NetworkClockUpdateFrequency, true);
		}
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

### Based on a moving window discarding outliers

First, we inherit `APlayerController` and add the following in the header file:

{% highlight c++ %}
UCLASS()
class GAME_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

	...

#pragma region NetworkClockSync

protected:

	/** Frequency that the client requests to adjust it's local clock. Set to zero to disable periodic updates. */
	UPROPERTY(EditDefaultsOnly, Category=GameState)
	float NetworkClockUpdateFrequency = 1.f;

private:

	float ServerWorldTimeDelta = 0.f;
	TArray<float> RTTSlidingWindow;

public:

	UFUNCTION(BlueprintPure)
	float GetServerWorldTimeDelta() const;

	UFUNCTION(BlueprintPure)
	float GetServerWorldTime() const;

	void PostNetInit() override;

private:

	void RequestWorldTime_Internal();
	
	UFUNCTION(Server, Unreliable)
	void ServerRequestWorldTime(float ClientTimestamp);
	
	UFUNCTION(Client, Unreliable)
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
		RequestWorldTime_Internal();
		if (NetworkClockUpdateFrequency > 0.f)
		{
			FTimerHandle TimerHandle;
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::RequestWorldTime_Internal, NetworkClockUpdateFrequency, true);
		}
	}
}

void AMyPlayerController::RequestWorldTime_Internal()
{
	ServerRequestWorldTime(GetWorld()->GetTimeSeconds());
}

void AMyPlayerController::ClientUpdateWorldTime_Implementation(float ClientTimestamp, float ServerTimestamp)
{
	const float RoundTripTime = GetWorld()->GetTimeSeconds() - ClientTimestamp;
	RTTSlidingWindow.Add(RoundTripTime);
	float AdjustedRTT = 0;
	if (RTTSlidingWindow.Num() == 10)
	{
		RTTSlidingWindow.RemoveAt(0);
		TArray<float> tmp = RTTSlidingWindow;
		tmp.Sort();
		for (int i = 1; i < 9; ++i)
		{
			AdjustedRTT += tmp[i];
		}
		AdjustedRTT /= 8;
	}
	else
	{
		AdjustedRTT = RoundTripTime;
	}
	
	ServerWorldTimeDelta = ServerTimestamp - ClientTimestamp - AdjustedRTT / 2.f;
}

void AMyPlayerController::ServerRequestWorldTime_Implementation(float ClientTimestamp)
{
	const float Timestamp = GetWorld()->GetTimeSeconds();
	ClientUpdateWorldTime(ClientTimestamp, Timestamp);
}
{% endhighlight %}

In this method there are a couple of [magic numbers](https://en.wikipedia.org/wiki/Magic_number_(programming)) that have been selected empirically considering the trade-off between performance and accuracy. In this case, our sliding window has 10 slots and we discard the biggest and smallest numbers (20%) from it  as if they were outliers to compute the RTT average. I encourage the reader to tweak these magic numbers until you get your desired results! 

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

![Synced clock static functions]({{ '/' | absolute_url }}/assets/images/per-post/netclock/netclock-nodes.jpg){: .align-center}

## Results

After the first complete sync-up, the new network clocks provide a more accurate view of the server time than the native one, reducing drastically the deviation on high ping scenarios. The following example log was recorded on the [Third Person Template](https://docs.unrealengine.com/5.0/en-US/third-person-template-in-unreal-engine/) with the following Network Emulation settings:

- Emulation Target: Everyone
- Network Emulation Profile: Bad

Results:

| Experiment # | Server time | Vanilla network clock | Vanilla network clock error | Shortest round-trip-time clock | Shortest round-trip-time clock error |
|-------|--------|---------|---------|---------|---------|
| 1 | 8.607961 | 8.257961 | 0.3500 | 8.569201 | 0.0388 |
| 2 | 14.769416 | 14.371296 | 0.3981 | 14.774051 | 0.0046 |
| 3 | 6.552305 | 6.215758 | 0.3365 | 6.513541 | 0.0388 |
| 4 | 4.971255 | 4.673519 | 0.2977 | 4.93249 | 0.0388 |
| 5 | 5.075128 | 4.772511 | 0.3026 | 5.036363 | 0.0388 |

| Experiment # | Server time | Vanilla network clock | Vanilla network clock error | Moving window clock | Moving window clock error |
|-------|--------|---------|---------|---------|---------|
| 1 | 19.079962 | 18.424429 | 0.6555 | 19.077688 | 0.0023 |
| 2 | 32.913425 | 32.289417 | 0.6240 | 32.925301 | 0.0119 |
| 3 | 10.613771 | 10.11378 | 0.5000 | 10.637333 | 0.0236 |
| 4 | 14.840554 | 14.153416 | 0.6871 | 14.856544 | 0.0160 |
| 5 | 18.089819 | 17.513674 | 0.5761 | 18.087681 | 0.0021 |

As seen above, both new network clocks reports values closer to the expected value (_Server time_).

# The problem with synced network clocks (in general)

No matter which clock you end up using, remember that you cannot trust the clock value in a late joining situation. This is because **we cannot ensure that the clock has synced right when we joined**. However, we can paliate this situation with a [`NetworkEventSubsystem`](https://cdn.discordapp.com/attachments/221799385611239424/992481326093574155/NetworkEventSubsystem.zip) (by [Jambax](https://jambax.co.uk/)), in which we would halt our OnRep actions until the clock syncs.

# Conclusion

Finally, a non-intrusive synced network clock! 

As I always say, be sure to make your own experiments, and don't blindly trust randoms on the internet (me)! 

I would like to thank `Zlo#1654`, `Adriel#5737` and `Laura#4664` from [Slackers](https://unrealslackers.org/) again for all the help they gave me to make this post. As well  all the people who support my articles, you are amazing. 

By the way, the best way to track when I post articles is on Twitter, so [don't be afraid to follow me](https://twitter.com/vorixo)! DM's are open!

Enjoy, vori.