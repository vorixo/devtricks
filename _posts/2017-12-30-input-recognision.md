---
layout: single
title: "Input recognision and responsitivity on UE4"
excerpt: In this article we'll have a look about how to solve the input responsive problem on UE4
author: Meta
category: Videogames Development
tags:
  - Programming
  - UE4
  - Input
  - Videogames Development
---

_**NOTE:** This article doesn’t involve blueprints at all! It is highly recommended to follow this tutorial with the **ShooterGame** (UE4) at hand._.

One of the main concerns when talking about shooters is input recognition. When a player presses a key, no matter what (which menu is in front of him, where he is at, he is dead or not), the game should recognize which key is being pressed at any given moment or... at least make it look like it does!

The way the input works in **Unreal** is by refreshing an **input stack** on the [controller](https://docs.unrealengine.com/latest/INT/API/Runtime/Engine/GameFramework/APlayerController/){:target="_blank"} “delegating” those inputs to the actions each input is bound to, let’s call this method "deferred input catching". 

# What should I use for binding, the controller or the pawn?

In terms of responsivity we want the fastest and… of course reliable, why not the player controller? It’s an excellent choice in any given case if you pursue the topic of this article. 

There is a common missconception about why using the controller or the [pawn](https://docs.unrealengine.com/latest/INT/API/Runtime/Engine/GameFramework/APawn/){:target="_blank"} for bindings, most of the people, even _internals_, say that general inputs common for all the pawns - such as open a midgame menu or show the scoreboard - should be handled in the controller **in every scenario**. Why? Well, simply because no matter what pawn we are possessing, we want to execute those actions whenever we press those keys!

On the other side, ~~using the pawn as the input binder would be good in a scenario where you have custom controls for each pawn~~. Or at least, that’s what I’ve been saying all this time till I realised about certain problems:

  * On pre-match forget about pressing the sprint key to start sprinting as soon as you respawn
  * Every time you die, on your respawn you will have to repress every key
  * Menu navigation and key tracking for pawn – Tedious! 

To summarize, if we use the pawn, we will start registering keys on the input stack as soon as the pawn is possessed, this means that **all the keys we hold before possessing that pawn will need to be repressed**, and… we don’t want that, don’t we?

![Keyboard smashing]({{ '/' | absolute_url }}/assets/images/per-post/input-recognision/carrey.gif){: .align-center}

Does this mean that the controller is enough to accomplish our task? The answer is simple, no. 

# Persistency

Let’s talk about persistency. Persistency is so important for us since we need to keep track of what is happening on our keyboard most of the time (aiming for all the time if possible!), we already defined the controller to be more persistent than the pawn, but… is there something that is more persistent than the controller itself and can process input all the time?

The answer is yes, the game viewport! Concretely the class [UGameViewportClient](https://docs.unrealengine.com/latest/INT/API/Runtime/Engine/Engine/UGameViewportClient/){:target="_blank"}, if we check the documentation carefully we can see its **main responsibility**: propagating input events to the global interactions list. 

Once we know where to look, let’s get into it.

# The break down

First of all, we want to check which functions would be relevant for us; in this case, we are going to work with: 

{% highlight c++ %}
virtual bool InputKey(FViewport* Viewport, int32 ControllerId, FKey Key, EInputEvent EventType, float AmountDepressed = 1.f, bool bGamepad = false) override;
{% endhighlight %}

So we’ll go ahead and declare that function in our header file, in my case, I’m working with the [ShooterGame](https://docs.unrealengine.com/latest/INT/Resources/SampleGames/ShooterGame/){:target="_blank"} template provided by [EpicGames](https://www.epicgames.com/es-ES){:target="_blank"}, so I’ll go ahead and define it on ``ShooterGameViewportClient.h`` – As we can see, this function gets executed **every time we press a key**, which is ideal! All we have to do is to store said keys somewhere in our class or in an outer class (it could be actually the controller). In my case, I continued using the same header file, so I'll go ahead and declare a cute lil array:

{% highlight c++ %}
TArray<FKey> keys;
{% endhighlight %}

This array is already the solution to our problem, but first, let me explain why. The array will store the pressed keys all the time, this means it will need to be updated live, storing the keys we press and removing the ones we release, if we look at the definition of [EInputEvent](https://docs.unrealengine.com/latest/INT/API/Runtime/Engine/Engine/EInputEvent/){:target="_blank"}, we can determine that **0** is “Press” and **1** is “Release”:

{% highlight c++ %}
bool UShooterGameViewportClient::InputKey(FViewport* v, int32 ControllerId, FKey Key, EInputEvent EventType, float AmountDepressed, bool bGamepad) {
    if (EventType == 0) {
		keys.AddUnique(Key);
	}
	else if (EventType == 1) {
		keys.RemoveSingle(Key);
	}
	return Super::InputKey(v, ControllerId, Key, EventType, AmountDepressed, bGamepad);
}
{% endhighlight %}

In order to see if it worked, we can print the array at the end of the tick function, in my case, again, I’m still working on ``ShooterGameViewportClient.cpp``:

{% highlight c++ %}
for (auto& key : keys)
{
    GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Red, key.GetDisplayName().ToString());
}
{% endhighlight %}

We are almost done! 

# The final details

If you execute the game at this point you will notice **everything works correctly**, however, we are missing a big point! What if the player decides to “Alt tab” to browse something on the internet? Well, no problem since we have:

{% highlight c++ %}
virtual void LostFocus(FViewport* Viewport) override;
{% endhighlight %}

If the player loses focus on the game, we should empty the array! The player is not really playing the game, why should we say she is pressing certain inputs when she is not?

{% highlight c++ %}
void UShooterGameViewportClient::LostFocus(FViewport* v) {
	Super::LostFocus(v);
	keys.Empty();
}
{% endhighlight %}

This solves the only problem we had left. Now we know which keys are being pressed at any given moment. Now is up to you using this for your game appropriately!

# Some tips for a more friendly implementation

Let’s drop some tips for a friendly implementation:

  * As we said at the beginning of the article, the input binding will happen on the controller since it will provide us with a faster response, but the **GameViewportClient** implementation is compatible with both ways of binding.
  * The controller (or the pawn) will read these keys and process them whenever it is required. One example of key processing would be the method **AShooterPlayerController** has:
{% highlight c++ %}
void AShooterPlayerController::SimulateInputKey(FKey Key, bool bPressed)
{
	InputKey(Key, bPressed ? IE_Pressed : IE_Released, 1, false);
}
{% endhighlight %}
  * It is recommended to NOT use this array when it is not needed (ie: mid-game while playing), same for “recording the keys”. 
  * If done correctly you will have a responsive input system!

Enjoy, vorixo.