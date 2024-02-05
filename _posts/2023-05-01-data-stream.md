---
layout: single
title: "Multiplayer data streaming in Unreal Engine"
excerpt: In this post you'll learn how to stream big quantities of data in Unreal Engine using the RPC system.
header:
  teaser: /assets/images/per-post/data-stream/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Data streaming
  - Networking
---

In this article we'll learn how to stream big quantities of data realiably from and to your clients in Unreal Engine.

# Introduction

As we saw in a [previous article](https://vorixo.github.io/devtricks/stateful-events-multiplayer/), Unreal Engine diposes of different tools to send data, such as RPCs and replicated variables. As we've studied in the past, each of them have a different purpose:
- **Replicated variables**: These variables should be set in the server and they will replicate down to the relevant clients following the defined replication condition. Incoming relevant connections will also receive the **state** hold by the replicated variables (if applicable).
- **RPCs**: There are three types of RPCs depending on the [type of operation](https://docs.unrealengine.com/4.26/en-US/InteractiveExperiences/Networking/Actors/RPCs/). They are used for **transient** data as they don't hold state.

However, there are times in which we just want to send a stream of data to a Client or "upload" something to the Server. For example, downloading or uploading a Save Game file. Save Game files can sometimes get quite heavy and contain great quantities of data, which makes it a perfect candidate for today's article. 

# The problem

Continuing with the example of the Save Game file, we can use a **reliable Client RPC** (since we want to guarantee the arrival) to send to the client the contents of such file when requested:

{% highlight c++ %}
void AMyGameMode::SendDataTo(AMyPlayerController* Controller)
{
	// Sending our big Data array to our client controller
	Controller->ClientReceiveData(Data);
}

// Controller Header
UPROPERTY(Client, reliable)
void ClientReceiveData(const TArray<FMyData>& MyData) const;

// Controller cpp
void AMyPlayerController::ClientReceiveData_Implementation(const TArray<FMyData>& MyData) const
{
	for (const FMyData& Data : MyData)
	{
		...
	}
}
{% endhighlight %}

Except... **this won't work** in the majority of the cases when we are dealing with big quantities of data. But why?

## Large bunches

Large data transfers can be a concern when using Unreal Engine's Remote Procedure Call (RPC) system. Unreal Engine employs a mechanism to limit the amount of data that can be sent in a single RPC. The data passed in an RPC conforms a "bunch", and Unreal Engine will not send the bunch if it exceeds the maximum limit set in `NetMaxConstructedPartialBunchSizeBytes`.

This limit is enforced by the `IsBunchTooLarge` function, which checks if the bunch size exceeds `NetMaxConstructedPartialBunchSizeBytes` before sending it. If the check fails, the bunch is not sent and an error message is logged: 

{% highlight c++ %}
FPacketIdRange UChannel::SendBunch( FOutBunch* Bunch, bool Merge )
{
	...
	if (!ensureMsgf(!IsBunchTooLarge(Connection, Bunch), TEXT("Attempted to send bunch exceeding max allowed size. BunchSize=%d, MaximumSize=%d Channel: %s"), Bunch->GetNumBytes(), NetMaxConstructedPartialBunchSizeBytes, *Describe()))
	{
		UE_LOG(LogNetPartialBunch, Error, TEXT("Attempted to send bunch exceeding max allowed size. BunchSize=%d, MaximumSize=%d Channel: %s"), Bunch->GetNumBytes(), NetMaxConstructedPartialBunchSizeBytes, *Describe());
		Bunch->SetError();
		return FPacketIdRange(INDEX_NONE);
	}
	...
}

template<typename T>
static const bool IsBunchTooLarge(UNetConnection* Connection, T* Bunch)
{
	return !Connection->IsUnlimitedBunchSizeAllowed() && Bunch != nullptr && Bunch->GetNumBytes() > NetMaxConstructedPartialBunchSizeBytes;
}
{% endhighlight %}

It may be tempting to simply increase the value of `NetMaxConstructedPartialBunchSizeBytes` to allow larger bunches to be sent (by changing the CVar `net.MaxConstructedPartialBunchSizeBytes`). However, this is not recommended, as the variable is initialized to a reasonable value of 64KB to ensure that the bandwidth usage is controlled and within budget.

But then, how do we solve the issue if we are not supposed to modify the max bunch size allowed?

# Chunking data

Instead of writing a complicated and involved solution, we want to find one that will work in tandem with Unreal's RPC system. 

By splitting the data into `n` subarrays and sending `n` RPCs, you can ensure that each subarray is small enough to be sent within the maximum limit set by `NetMaxConstructedPartialBunchSizeBytes`.

![Data chunking]({{ '/' | absolute_url }}/assets/images/per-post/data-stream/ChunkingDiagram.jpg){: .align-center}

In order to chunk the data and determine the number of RPCs and subarrays needed, we must first determine the size of each data element in our data structure. Once we have this information, we can calculate how many data elements can be sent in each RPC, and then divide the data into appropriately sized subarrays. 

However, if your data structure contains another array inside, you need to take into account the **maximum pessimistic size** of the inner array as well (or send it separately). For example, if you have an array of structs, where each member contains another array of floats, you can determine the size of each element in the following way:

{% highlight c++ %}
int32 InnerArrayMaxSize = 100; // set the maximum size of the inner array here
int32 ElementSize = sizeof(FMyStruct) + (InnerArrayMaxSize * sizeof(float));
{% endhighlight %}

Since the maximum element size can vary significantly depending on the use case, it is recommended that you develop your own pessimistic heuristic to estimate the element size.

## The algorithm

In the example below we will chunk a very simple array (without inner complex data structures) and slice it in various subarrays based on the element size and the `NetMaxConstructedPartialBunchSizeBytes` size:

{% highlight c++ %}
void AMyGameMode::SendDataTo(AMyPlayerController* Controller)
{
	const int32 ElementSize = sizeof(FMyData);
	// Here you can set whatever number, the closer to NetMaxConstructedPartialBunchSizeBytes, the tighter, which I don't recommend
	const int32 MaxBytesPerRPC = 32 * 1024; // 32KB
	const int32 MaxElementsPerRPC = MaxBytesPerRPC / ElementSize;
	const int32 ChunksToSend = FMath::CeilToInt(Data.Num() / (float)MaxElementsPerRPC);
	
	TArray<FMyData> ChunkBuffer;

	for (int32 ChunksSent = 0; i < ChunksToSend; i++)
	{
		ChunkBuffer.Reset();

		const int32 StartIndex = ChunksSent * MaxElementsPerRPC;
		const int32 NumElements = FMath::Min(MaxElementsPerRPC, Data.Num() - StartIndex);
		
		ChunkBuffer.Append(Data.GetData() + StartIndex, NumElements);

		// Send a reliable Client RPC with the subarray data here
		Controller->ClientReceiveData(ChunkBuffer);
	}
}
{% endhighlight %}

In the above code, each RPC will send at most 32KB. However, using a loop to send many reliable RPCs is **not** a feasible option as **it may fill up the reliable buffer**, and Unreal will end up closing the connection, which is not what we want. We can see this in `UChannel::SendBunch`:

{% highlight c++ %}
const bool bOverflowsReliable = (NumOutRec + OutgoingBunches.Num() >= RELIABLE_BUFFER + Bunch->bClose);
...
if (Bunch->bReliable && bOverflowsReliable)
{
	...
	Connection->SendCloseReason(ENetCloseResult::ReliableBufferOverflow);
	...
	Connection->Close(ENetCloseResult::ReliableBufferOverflow);
	...
}
...
{% endhighlight %}

In the above code `NumOutRec` holds the number of outgoing **reliable unacked packets**. And as we can see, if the sum of that, plus the number of **current outgoing bunches** exceed the limit defined in `RELIABLE_BUFFER` (256 by default), it will drop the connection.

So... we need to do something!

## Chunking over time
To avoid overloading the reliable buffer, we need to spread out the data streaming **over time**. This can be achieved by sending smaller subarrays of data and retrying to send them based on the current state of the reliable buffer. Although the code may become a bit more spread out, the solution is simple: iterate over each subarray and check the reliable buffer's state before sending it. To simplify the code below I've precalculated **offline** my chunk size and defined it in a constant variable `MAXCHUNKSIZE`.

First, we send the whole data to our controller (server side) and we signal the controller that the streaming can start (by setting `ChunksToSend` to other than 0):

{% highlight c++ %}
void AMyGameMode::SendDataTo(AMyPlayerController* Controller)
{
	Controller->ReceiveDataInServer(Data);
}

void AMyPlayerController::ReceiveDataInServer(const TArray<FMyData>& MyData)
{
	DataToStream = MyData;
	ChunksToSend = FMath::CeilToInt(DataToStream.Num() / (float)MAXCHUNKSIZE);
	ChunksSent = 0;
}
{% endhighlight %}

Then on `TickActor` server side in our Controller we can start the streaming:

{% highlight c++ %}
void AMyPlayerController::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
	if (HasAuthority())
	{
		TArray<FMyData> ChunkBuffer;
		auto* Channel = NetConnection->FindActorChannelRef(this);

		while (ChunksSent < ChunksToSend && Channel->NumOutRec < (RELIABLE_BUFFER / 2))
		{
			ChunkBuffer.Reset();
			const int32 StartIndex = ChunksSent * MAXCHUNKSIZE;
			const int32 NumElements = FMath::Min(MAXCHUNKSIZE, Data.Num() - StartIndex);

			check(NumElements > 0 && (StartIndex + NumElements - 1) < DataToStream.Num());
			ChunkBuffer.Append(DataToStream.GetData() + StartIndex, NumElements);

			// Send a reliable Client RPC with the subarray data here
			ClientReceiveData(ChunkBuffer);
			ChunksSent++;
		}

		if (ChunksSent >= ChunksToSend)
		{
			ClientNotifyAllDataReceived();
			DataToStream.Empty();
			ChunksToSend = 0;
		}
	}
}
{% endhighlight %}

The principle behind the solution is the same as what we've seen before. However, this time we are distributing the data streaming over time by sending **as many reliable RPCs per tick** as our **budget** allows within the while loop. To reduce the likelihood of saturating the reliable buffer, we have defined our budget to be half the size of the reliable buffer (`RELIABLE_BUFFER / 2`). Once we have finished streaming data, we let the client know by sending an extra reliable Client RPC called `ClientNotifyAllDataReceived`.

**Note:** Distributing the data streaming across multiple ticks, will help us to avoid overflowing the reliable buffer and causing Unreal to close the connection.
{: .notice--info}

# Conclusion

Data streaming goes **BRRRR**!!

Hehe, I hope this article helped you to figure out how you can send around great quantities of data. We did it server to client, but you can use the same techniques to do client to server streaming! There's absolute no need to open a custom Actor Channel, or do extense black magic to achieve this in Unreal, we simply have to use RPCs smartly! 

As a hands-on exercise for the reader I propose you to find a more generic solution using byte arrays (uint8 TArray), my [Network Manager](https://vorixo.github.io/devtricks/network-managers/) article explains how to do this. I'd be happy to know if someone manages to make such implementation!

Also, if you want to learn more about this topic, be sure to check [this wonderful article](https://gafferongames.com/post/sending_large_blocks_of_data/) by [Glenn Fiedler](https://twitter.com/gafferongames), in which a more generic solution using Unreliable RPCs is presented!

As always, if you find anything wrong or have any question, don't doubt to follow and contact me on [twitter](https://twitter.com/vorixo)! ;D

Enjoy, vori.