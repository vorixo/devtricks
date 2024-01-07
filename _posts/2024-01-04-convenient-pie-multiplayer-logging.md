---
layout: single
title: "Convenient Multiplayer Logging in UE5"
excerpt: In this post we'll learn a very cool trick to enhance your multiplayer logs. It consists on accessing the server instance from the client in the editor. But how? Find out inside!
header:
  teaser: /assets/images/per-post/convenient-pie-multiplayer-logging/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Networking
  - Debugging
  - Logging
---


Hello and happy new year! Many of you asked me how I measured the accuracy in my [network clock article](https://vorixo.github.io/devtricks/non-destructive-synced-net-clock/).

This technique can be generalized to infinite use cases, so I decided to generalize it and write about it in this small article. So I hope you like it and find it useful.

# Overview

This article explains how to access the server instance from a client in PIE (Play in Editor), thus, easing logging of value discrepancy between server and client. This technique has served me in the past to debug synced variables. In essence, expect something as follows:

```
LogEntry: Client Stamina: 29.25 | Server Stamina: 29.23
```

As you can see, the same log entry contains the value of the stamina in the client and in the server captured in the same frame. The trick here is accessing the server instance from the client instance, this trick will only work in the editor, so its strictly useful for editor debugging/logging.

# The implementation

To obtain a Server Player Controller from a client, first we have to find the server world, only existing within the editor context:

{% highlight c++ %}
UWorld* UMyDevelopmentStatics::FindPlayInEditorAuthorityWorld()
{
	check(GEngine);

	// Find the server world (any PIE world will do, in case they are running without a dedicated server, but the ded. server world is ideal)
	UWorld* ServerWorld = nullptr;
#if WITH_EDITOR
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			if (UWorld* TestWorld = WorldContext.World())
			{
				if (WorldContext.RunAsDedicated)
				{
					// Ideal case
					ServerWorld = TestWorld;
					break;
				}
				else if (ServerWorld == nullptr)
				{
					ServerWorld = TestWorld;
				}
				else
				{
					// We already have a candidate, see if this one is 'better'
					if (TestWorld->GetNetMode() < ServerWorld->GetNetMode())
					{
						return ServerWorld;
					}
				}
			}
		}
	}
#endif
	return ServerWorld;
}
{% endhighlight %}

The function above was conveniently implemented in Lyra, so we can make use of it. The next function will use the previous function to get the Server Player Controller:

{% highlight c++ %}
APlayerController* ULyraDevelopmentStatics::FindPlayInEditorAuthorityPlayerController(APlayerController* ClientController)
{
#if WITH_EDITOR
	UWorld* ServerWorld = ULyraDevelopmentStatics::FindPlayInEditorAuthorityWorld();
	if (ServerWorld != nullptr)
	{
		for (FConstPlayerControllerIterator Iterator = ServerWorld->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* ServerController = Iterator->Get();

			const FUniqueNetIdRepl& ServerPlayerUniqueId = ServerController->PlayerState->GetUniqueId();
			const FUniqueNetIdRepl& ClientPlayerUniqueId = ClientController->PlayerState->GetUniqueId();

			if (ServerPlayerUniqueId == ClientPlayerUniqueId)
			{
				return ServerController;
			}
		}
	}
#endif
	return nullptr;
}
{% endhighlight %}

As we can see, we simply iterate through the player controllers of the server world until we find the one matching (so that the Player UniqueId matches). Thus, if we call the function from our client, the function returns the server Player Controller related to our client Player Controller.

# A simple example

In the example below, we want to print the server value alongside the client value of Stamina from our CMC.

{% highlight c++ %}
// From a client...

float ServerStamina = 0.f;
if (APlayerController* ServerPlayerController = UMyDevelopmentStatics::FindPlayInEditorAuthorityPlayerController(GetPlayerController()))
{
	if( AMyCharacter* ServerPawn = Cast<AMyCharacter>(ServerPlayerController->GetPawn()))
	{
		ServerStamina = ServerPawn->GetMyCharacterMovementComponent()->Stamina;
	}
	Print("LogEntry: Client Stamina: %f | Server Stamina: %f", Stamina, ServerStamina);
}
{% endhighlight %}

# Conclusion

Thanks for reading!

I hope this clarifies the magic I made back then when I measured the clock. Now! Feel free to use it wherever you like! Trust me, you'll find usecases :D

Enjoy, [vori](https://twitter.com/vorixo).