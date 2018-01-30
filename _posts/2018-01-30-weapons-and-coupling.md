---
layout: single
title: "Why a good design is crucial"
excerpt: Winding-Up and Winding-Down, the features that make you realize that designing a weapon framework is not as easy to decouple.
author: Meta
category: Videogames Development
tags:
  - Programming
  - Design
  - Coupling
  - Iheritance
  - Interfaces
---

Hi, in today's article I will be writing about weapon systems and [coupling](https://en.wikipedia.org/wiki/Coupling_(computer_programming)).

Weapon systems aren't as easy to design as it could seem, they require the team to have very clear **what they need, and what they don't**. The lack of a needed feature on the design phase can force the team to have to go back to the code and restructure it to suit it up. This could lead us to **rework  completely** some parts of the code based on the interaction of said feature. 

# The design is crucial, no matter what

This statement is pretty straight forward, but **doesn't contradict the fact that our code should be ready to handle [extensibility](https://en.wikipedia.org/wiki/Extensibility)** (depending the style of game). That's why I encourage a design based on a **component and data driven** approach, where each component handles individual parts of the full system. That would also be benefitial for future cases, since **reusability** of components would be a thing. 

Another big point about why to use components is [cohesion](https://en.wikipedia.org/wiki/Cohesion_(computer_science)). An example would be an "Ammo Component". This component will handle all the single aspect of ammunition, leaving that functionality out of the weapon. If programmed correctly, the weapon will survive with or without an "ammo component" (does the class contains said component? If so, use it). 

Working with components is also benefitial when working on big teams, since the work would be more easily splitted on those components versus every programmer trying to merge the same file.

# The problem - Winding Up and Winding Down

In our game we need the shooting events to take over the sprinting ones. This means that as soon as the player wants to shoot, player will stop sprinting and proceed to shoot (the time the weapon takes to go from the sprinting state onto the normal state is named Wind-Up time). However, player's shoot will be frustrated if he is on a wind-up state. 

![Windup mechanic]({{ '/' | absolute_url }}/assets/images/per-post/weapons-and-coupling/windup.gif){: .align-center}

When the wind-up occurs the deferred shoot input that made the player stop sprinting will happen, which could lead us to weird situations if programmed incorrectly. One problem would be - not triggering the stop fire function - which will make our weapon to shoot continuously in an attempt of consuming even our breakfast.

# The solution - Winding Up and Winding Down

We, as a developers have to be aware of these problems when we work on deferred systems based on user input actions. The golden rule is the following: **If the press event is deferred, the release event will need to be deferred aswell.** For example, in our case, the firing functions:

If we delay the start shooting signal with a timer (let's say 1 second), we will need to check **if the player still wants to shoot as soon as this deferred call is completed**. If the player is no longer pressing the action key, we will have to cease our shooting to prevent the greedy weapon situation, this could be an example:

{% highlight c++ %}
/**
* Starts firing a weapon, gets called on "StartFire" function through timers that helps with  winding features.
**/
void UShooterWeapon_FiringComponent::OnStartFire() {
	bWantsToFire = true;
	DetermineWeaponState();
	
	// If the player doesn't want to shoot anymore after the deferred call, we cease the shooting
	if (!OwnerWeapon->GetPawnOwner()->IsFiring()) {
		StopFire();
	}
}
{% endhighlight %}

In a wind-down situation is a bit different. Everytime we shoot, we will have some seconds when the player won't be able to sprint, this is a design choice to avoid sprint shooting spamming. One approach would be controlling that with a variable/interface/function on the character, that will check whether or not the controlled interactible (weapon, accessory…) is busy (doing things). 

# Interaction between elements

As we can observe there are a lot of parts that will need to be controlled back and forward between the player and the interactible, which creates a **strong dependency** between both parts. The mechanic of winding up and winding down needs to keep track of the player sprinting state, and the sprinting state needs to be aware of the "weapon" "firing" (usage) state. Let's draw the following scenario:

  * Player has a responsive sprinting system driven through _Tick_ and a couple booleans, meaning that as soon as the player will be able to sprint, he will do it.
  * The player has equipped a three round burst weapon, meaning that the weapon will take some time to fire the three burst. In this example, clicking once would trigger the complete burst (in a 3 round burst scenario the weapon will shoot three times).
  * Action: Player holds sprint button and without quit pressing it, presses fire once.

Expected result:

  1. Player starts sprinting
  2. Player stops sprinting because he detected a fire input
  3. Player winds-up the weapon
  4. Once wound up, shoots 3 times consecutively (3 round burst)
  5. After the last shoot, the wind down counter starts
  6. Once wound down finishes player starts sprinting 

# Interfaces

Knowing this, if we don't follow a good design scheme **we would be creating a circular dependency with every single interactive "weapon" we have in our game**.

To solve this, make all your interactive "weapons" implement common usage interfaces, by doing that we will get rid of the necessity of doing long inheritation driven systems hard to mantain.Let's see the benefit of using them:

  * Player doesn't need to know what accesory is controlling, weapon or not.
  * Player will just execute functions that will retrieve information (it can retrieve void aswell)

General implementation approaches:

  1. The less granular solution would be the implementation of a "god like" interaction interface. Some of these functions will return vague values, for example, if we carry a map in our inventory and our interface has a "GetMagAmmo" function, we will have to define special values for special situations. In this case, "GetMagAmmo" will return "-2" because it's a map and it doesn't have bullets. This could be useful aswell for a weapon with unlimited ammo; in that case we could use "-1" as the return failsafe ammo. **Have in mind that all these values would need to be well documented**.
  
  2.  If you prefer a more granular solution, another recommendation would be to split your system in various "as generic as possible" interfaces, so a map won't have to implement a function called "GetMagAmmo". The only expense of this approach would be that you would need to check if the interactible item implements or not said interface oposed to failsafe some values.

In my case I really **recommend the second approach**, as **there isn't explicit necessity of documenting what those values mean**. This second approach is more cohesive as we don't need to implement non-sensical functions for some classes requiring these interaction interfaces. In this case, the map won't implement the Ammo interface.

Enjoy, vorixo.
