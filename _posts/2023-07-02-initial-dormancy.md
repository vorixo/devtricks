---
layout: single
title: "Understanding Unreal Engine's DORM_Initial dormancy setting"
excerpt: In this post we'll explain how DORM_Initial works and why you should be careful with it.
header:
  teaser: /assets/images/per-post/initial-dormancy/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Dormancy
  - Networking
---

This article is a collaboration with [WizardCell](https://twitter.com/wizardcells) and on it, you'll learn how `DORM_Initial` works and why you should be careful with it. 

# Introduction

If we read the description of the `DORM_Initial` dormancy setting, we see what follows: 
``` 
This actor is initially dormant for all connection if it was placed in map.
```

Which made me understand that this setting would work like `DORM_DormantAll` just with a couple extra optimizations. But it isn't the case, let's see what are the implications of using this setting.

# `DORM_DormantAll` versus `DORM_Initial`

When an Actor becomes relevant to a connection, all the properties of said Actor are sent to the connection, no matter whether the Actor is `DORM_DormantAll` or `DORM_Awake`. That means that the [state](https://vorixo.github.io/devtricks/stateful-events-multiplayer/) the client receives from the Actor will be consistent of what the server sees about it.

However, that is not the case of `DORM_Initial` Actors, as they will not send any C++ replicated property if a connection becomes relevant:

![Dormancy Initial issue]({{ '/' | absolute_url }}/assets/images/per-post/initial-dormancy/dormancygif.gif){: .align-center}

**Watch out!** Blueprint replicated properties ignore Actor dormancy settings.
{: .notice--danger}

## How to use `DORM_Initial`

So shall I simply not use `DORM_Initial`? No, you should! `DORM_Initial` Actors won't be considered for replication at all even under relevancy, and sometimes that can be a huge optimization.

To ensure incoming connections get the new values over `DORM_Initial` Actors, you should call `FlushNetDormancy` on them after updating their properties. This will make the Actor to become `DORM_DormantAll` but only when strictly needed.

![Flush Dormancy node]({{ '/' | absolute_url }}/assets/images/per-post/initial-dormancy/flushdormancynode.jpg){: .align-center}


### What about calling `ForceNetUpdate` to also flush the dormancy?

Be very careful with this, as `ForceNetUpdate` also calls `FlushNetDormancy` but its guarded with a `NetDriver` check:

{% highlight c++ %}
void AActor::ForceNetUpdate()
{
	...

	if (GetLocalRole() == ROLE_Authority)
	{
		// ForceNetUpdate on the game net driver only if we are the authority...
		if (NetDriver && NetDriver->GetNetMode() < ENetMode::NM_Client) // ... and not a client
		{
			NetDriver->ForceNetUpdate(this);

			if (NetDormancy > DORM_Awake)
			{
				FlushNetDormancy();
			}
		}
	}

	...
}
{% endhighlight %}

So if the Net driver is null at the moment you call `ForceNetUpdate`, which can definitely happen, the Actor will never call `FlushNetDormancy`, causing the Actor to stay in a `DORM_Initial` state. For that reason it is always recommended to **explicitly** call `FlushNetDormancy` when dealing with `DORM_Initial` Actors.

# Conclusion

Thanks for reading! 

Let's hope everything is clear! Feel free to reach us out if there are questions! 
And I encourage every reader to check [WizardCell's articles](https://wizardcell.com/).

Enjoy, [WizardCell](https://twitter.com/wizardcells) and [vori](https://twitter.com/vorixo).