---
layout: single
title: "Unreal Engine - Correct stateful replication"
excerpt: RPCs or OnReps, what shall I use?
header:
  teaser: /assets/images/per-post/stateful-rep/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - Replication
  - UE5
  - UE4
  - Late joiners
  - Relevancy
---

In this post we explore a very typical conceptual problem I've seen a lot lately: Non resilient stateful multiplayer code.

# Introduction

One of the problems I've seen repeated lately in Unreal Engine is multiplayer code non resilient to late joiners and relevancy. Most of the time this is because the lack of understanding on how RPCs and replicated variables work at a high level. 

This post won't get into low level details, but will provide insights on how Multicast/Client RPCs and OnRep variables work, for both Blueprints and C++, to provide late-joiners/relevancy "resilient" Multiplayer code.

# OnReps VS Multicast/Client RPCs

## OnReps

OnReps are replicated variables that trigger a behaviour on clients and server whenever the stored value changes. However, they don't behave exactly the same in Blueprints and C++:


- **C++:** When changing an OnRep variable in the Server, the OnRep behavior will only trigger on the clients when the value of the variable changes. The Server won't trigger the OnRep, meaning that we have to call the OnRep behavior explicitly from the Server if we wish to execute it in the Server.

- **Blueprints:** When we set an OnRep variable in the server, the OnRep behaviour will be triggered always on the server (even if the value of the variable didn't change). However, the OnRep behavior will only be triggered on the clients if the variable of the OnRep has changed. In addition, it is not possible to call explicitly the OnRep functions created by OnRep variables.

The values of our replicated variables are sent by the server to the incoming connections, so if a new player becomes [relevant to us](https://docs.unrealengine.com/4.26/en-US/InteractiveExperiences/Networking/Actors/Relevancy/), they will set our replicated variables to the incoming values. This concept is key, as OnReps execute behaviour on clients when the value of such variable changes. 


## Multicast/Client RPCs

Multicast and Client RPCs are one-off events that should be only executed from the server that trigger behaviour on all the [relevant](https://docs.unrealengine.com/4.26/en-US/InteractiveExperiences/Networking/Actors/Relevancy/) targets once called.

- **Multicast RPC:** Multicasts execute in all the clients, they are useful for one-off events, like one-shot SFX and VFX.

- **Client RPC:** Client RPCs execute only in the client to which the RPC is targeted to. It can be useful for one-off events targetted to one specific client (ie: A team-mate sending you a [private message](https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/GameFramework/APlayerController/ClientTeamMessage/)).

Essentially, Multicast and Client RPCs are for one-off events, meaning that they don't persist any state. New connections have no clue which RPCs were sent recently.

# Towards relevancy resilient code

This Section provides an example of a relevancy non-resilient code. We are going to showcase the problems and convert it to a relevancy resilient code using the tools in our toolset.

## The problem

In this example, we want to change the mesh of our character to `SK_Mannequin`. For that, we execute a Multicast RPC from the server.

![RPC approach]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/non-resilient.jpg){: .align-center}

Now, once we press E in one of our clients, this happens:

![Changing the mesh]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/changemesh.gif){: .align-center}

Which is great, we see the new mesh in all the players. However, when we late-join a new player, this happens:

![New connections don't see it]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/latejoin-wrong.gif){: .align-center}

The newly spawned client won't see the new mesh, since, as I mentioned in the above Section, new connections have no clue which RPCs were sent recently.

**Late joiners:** For earlier versions of UE5 this feature can be activated under `Editor Preferences > Experimental > PIE > Allow late joining`
{: .notice--info}

## The solution

In order to solve this situation, we can use the knowledge we learned in this post, and apply it.

Since we want new connections to see the new mesh of our Pawn, we are going to employ a state for that. In this case I decided to use an OnRep boolean, which defines whether a player has the new Mesh set or not:

![OnRep approach]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/resilient.jpg){: .align-center}

For that, we remove the RPC Multicast and replace it for an OnRep variable.

Now, once we press E in one of our clients, this happens:

![Changing the mesh]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/changemesh.gif){: .align-center}

Which is great, we see the new mesh in all the players. New connections will also see the new mesh, since they receive the updated properties from the server:

![Everything good]({{ '/' | absolute_url }}/assets/images/per-post/stateful-rep/latejoin-good.gif){: .align-center}

That's great!!

This is because we are using a replicated variable, and the values of the replicated variables are sent to new connections, thus, the OnRep triggers.


## Other use cases

The example I provided is a very simplistic use case, but this can be applied to lots of situations:
- Opening and closing networked doors
- Team-based stencil customization (coloured outlines based on the team of the players)
- Chests 
- ...

Basically everything stateful!

# Conclusion

## So... Shall I use always OnReps?

No. Replicated variables have a cost, and sometimes there is no need to preserve state. This is the case for one-off events, like some VFX and SFX (ie: explosions).

## To sum up...

OnReps, as Multicast and Client RPCs, are just that, tools in our toolset. We need to know how they work and when its convenient to use them.

My hope is that with this post everything will be much clearer as to when its convenient to use each of them.

Feel free to ask any question or provide feedback for these posts, you can find me (and follow me) on [twitter](https://twitter.com/vorixo), DM's are open!

Enjoy, vori.
