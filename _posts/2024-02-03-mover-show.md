---
layout: single
title: "Mover in Unreal Engine 5.4"
excerpt: In this post we'll see the current state of the Mover Plugin in UE 5.4.
header:
  teaser: /assets/images/per-post/mover-show/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Physics Prediction
  - Networking
---

Unreal Engine introduced Mover 2.0 in 5.4, this post showcases my first (second) impressions and an overview of the system.

# Introduction

Epic Games recently introduced a new and **experimental** Movement Component in 5.4, called Mover, so... let's take a look.

In order to activate the feature, open `Edit` and within `Plugins` windows, turn on the following:

![Mover Plugins]({{ '/' | absolute_url }}/assets/images/per-post/mover-show/mover-enable.jpg){: .align-center}

## How does it work?

The following video I've recorded showcases the state of the system at the release date of the Plugin during UE 5.4: 

<iframe width="480" height="270" src="https://www.youtube.com/embed/1jD4WT6wkjw" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

**Note:** At the time of writing, Mover 2.0 is only obtainable through the git version of the engine, branch 5.4 or ue5-main.
{: .notice--info}

## Additional resources

In the video I mention two resources:
* [Configuration settings for NPP.](https://github.com/ElSnaps/NetworkPredictionPlugin-Q3-PMove/blob/main/Config/DefaultNetworkPrediction.ini)
* [An alternative Movement system implemented with the NPP.](https://github.com/ElSnaps/NetworkPredictionPlugin-Q3-PMove/tree/main)

# Conclusion

Thanks for watching! 

Hopefully, and as I also said previously, I you are as hyped as I am with this new feature! :)

Enjoy, [vori](https://twitter.com/vorixo).