---
layout: single
title: "Gameplay Ability System - Advanced Network Optimizations"
excerpt: How to save some bandwidth
author: Meta
category: Videogames Development
tags:
  - Programming
  - Design
  - Multiplayer
  - Gameplay Ability System
  - UE5
  - UE4
---

Hi! It's me again, I promised a follow-up in my [previous post](https://vorixo.github.io/devtricks/gas/), and here I am, with another Unreal Engine blog entry. Today we are going to cover a pretty obscure topic that to my knowledge hasn't been covered in a practical manner anywhere. That is, network optimizations in GAS.

This post will revise some of the concepts explained in [Q&A With Epic Game's Dave Ratti](https://epicgames.ent.box.com/s/m1egifkxv3he3u3xezb9hzbgroxyhx89) and will expand further on them. Let's get into it.

# Introduction

If we take a look at [question #5](https://epicgames.ent.box.com/s/m1egifkxv3he3u3xezb9hzbgroxyhx89), we find the whole rationale about who should own the Ability System Component (ASC):

```
In general I would say anything that does not need to respawn 
should have the Owner and Avatar actor be the same thing. Anything 
like AI enemies, buildings, world props, etc.

Anything that does respawn should have the Owner and Avatar be 
different so that the Ability System Component does not need to 
be saved off / recreated / restored after a respawn.. PlayerState 
is the logical choice it is replicated to all clients (where as 
PlayerController is not). The downside is PlayerStates are always 
relevant so you can run into problems in 100 player games.

- Dave Ratti
```

Let's assume our game requires to preserve state between respawns for our controllable hero characters. We have two options:
  * **Implement Actor Pooling and make the hero the ASC owner:** If we never destroy our pawns there is no point on making the PlayerState the owner of the ASC. However implementing Actor Pooling can become complicated depending on the scope of the project.
  * **Make the PlayerState the ASC owner:** With this method it is not required to implement Actor Pooling to preserve state, since the PlayerState persists during the whole game. 

Actor Pooling is a whole topic by itself, so we are going to leave it out of the formula for this post, but you can learn more about it in [this video](https://youtu.be/BkQAOJHjmkQ?t=1601) by YAGER.

# Using the PlayerState as the owner

However, when using the PlayerState as the owner of the ASC there is a number of questions and constraints we must consider:
  * How do I initialize the ASC on the Pawn?
  * PlayerStates run at a reduced network frequency, that's a no-no for a competitive game!
  * PlayerStates are always relevant! Poor network bandwidth! :(

In this article we'll demonstrate how we can palliate these issues using multiple optimization techniques that we can find in our precious engine. But before we start, this article assumes the reader is using a [`Mixed` Replication Mode](https://github.com/tranek/GASDocumentation#concepts-asc-rm) in the PlayerState's ASC.
  
## How do I initialize the ASC on the Pawn?

To initialize the ASC on our Pawn we need to override three functions:

{% highlight c++ %}
// ~ Server - PC and PS Valid
void PossessedBy(AController* NewController) override;	
// ~ Client - PS Valid  
void OnRep_PlayerState() override;	
 // ~ Client - PC Valid					            
void OnRep_Controller() override;						             
{% endhighlight %}

Within the scope of the functions we ensure the validity of the different components relevant for the initialization of the ASC. First let's start with `PossessedBy`:

{% highlight c++ %}
void ARBPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (AbilitySystemComponent == nullptr)
	{
		if (ARBPlayerState* PS = GetPlayerState<ARBPlayerState>())
		{
      		// Cache the ASC in the Server (TWeakObjectPtr preferrable)
			AbilitySystemComponent = Cast<URBAbilitySystemComponent>(PS->GetAbilitySystemComponent());
			
      		// Init the Server side part of the ASC
      		AbilitySystemComponent->InitAbilityActorInfo(PS, this);

			// Some games grant attributes and abilities here

      		// Some games server initialize another components of the character that use the ASC here
		}
	}
}	             
{% endhighlight %}

`PossessedBy(AController* NewController)` gets called on the server, and in this function we are setting the PlayerState as the owner of the ASC, and passing `ARBPlayerCharacter` as the Avatar.

Next, `OnRep_PlayerState()`:

{% highlight c++ %}
void ARBPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (AbilitySystemComponent == nullptr)
	{
		if (ARBPlayerState* PS = GetPlayerState<ARBPlayerState>())
		{
      		// Cache the ASC in the Client (TWeakObjectPtr preferrable)
			AbilitySystemComponent = Cast<URBAbilitySystemComponent>(PS->GetAbilitySystemComponent());
			
      		// Init the Client side part of the ASC
      		AbilitySystemComponent->InitAbilityActorInfo(PS, this);
		
      		// Some games grant attributes here

     		// Some games client initialize another components of the character that use the ASC here
    	}
	}
}
{% endhighlight %}

The implementation logic is equal to the one found in `PossessedBy`, except with some minor differences - ie. Abilities cannot be added in the client, Attributes can be added but require custom initialization logic, [Gameplay Effects won't work](https://github.com/tranek/GASDocumentation/pull/68) (I hope I can cover this in a future article).

And finally, `OnRep_Controller()`:

{% highlight c++ %}
void ARBPlayerCharacter::OnRep_Controller()
{
	// Needed in case the PC wasn't valid when we Init-ed the ASC.
	if (ARBPlayerState* PS = GetPlayerState<ARBPlayerState>())
	{
		PS->GetAbilitySystemComponent()->RefreshAbilityActorInfo();
	}
	Super::OnRep_Controller();
}
{% endhighlight %}

This function is a fail-guard to the PlayerController data race we can encounter while initializing GAS. It might happen that the PlayerController wasn't valid when `OnRep_PlayerState()` was called, therefore we need this extra override.

The setup isn't very complicated, it is simply very decentralized. I did a [PR](https://github.com/EpicGames/UnrealEngine/pull/8400) recently trying to improve this by overriding `SetPlayerState` instead of `OnRep_PlayerState` and `PossessedBy`. (An upvote in the PR would be dope!!!)


## PlayerStates run at a reduced network frequency

Quoting [Dan's marvelous GAS guide](https://github.com/tranek/GASDocumentation#41-ability-system-component):

```
If your ASC is on your PlayerState, then you will need to increase 
the NetUpdateFrequency of your PlayerState. It defaults to a very 
low value on the PlayerState and can cause delays or perceived lag 
before changes to things like Attributes and GameplayTags happen on 
the clients. Be sure to enable Adaptive Network Update Frequency, 
Fortnite uses it.

- Dan
```

Increasing the `NetUpdateFrequency` of the PlayerState isn't a very good idea, since the goal of this article revolves around optimizing the network usage while using GAS. So let's take a look on how to activate the [Adaptive Network Update Frequency](https://docs.unrealengine.com/4.27/en-US/InteractiveExperiences/Networking/Actors/Properties/#adaptivenetworkupdatefrequency).

The theory is simple, we should set `net.UseAdaptiveNetUpdateFrequency` to 1. For that, in the `Config` folder of your project, open 
`DefaultEngine.ini` and add the following section:

```
[SystemSettings]
net.UseAdaptiveNetUpdateFrequency=1
```

However, in UE4 and UE5 Early Access, this won't work. To make it work in those engine versions you have the following alternatives: 
  * A) Remove `net.UseAdaptiveNetUpdateFrequency=0` from `Engine/Config/ConsoleVariables.ini` in the Engine's directory (like [this](https://github.com/EpicGames/UnrealEngine/commit/002a1bebb69660c16b4f2f0f8b0f43520e818cd1#diff-7f69b5e66f7365484672aeb4e44691f46a89674b05df4587a02ad49369360ec6)).
  * B) Switch the CVAR value in the Server when the game starts (right after ConsoleVariables.ini gets called).

## PlayerStates are always relevant

Yes indeed they are. And by design, there isn't much we can do about this, but if we follow Dave Ratti's advice, we could implement some optimizations ([from the Q&A, question #3](https://epicgames.ent.box.com/s/m1egifkxv3he3u3xezb9hzbgroxyhx89)):

```
Fortnite goes a few steps further with its optimizations. It actually 
does not replicate the UAbilitySystemComponent at all for simulated 
proxies. The component and attribute subobjects are skipped inside 
::ReplicateSubobjects() on the owning fortnite player state class. We 
do push the bare minimum replicated data from the ability system 
component to a structure on the pawn itself (basically, a subset of 
attribute values and a white list subset of tags that we replicate down 
in a bitmask). We call this a “proxy”. On the receiving side we take the 
proxy data, replicated on the pawn, and push it back into ability system 
component on the player state. So you do have an ASC for each player in 
FNBR, it just doesn’t directly replicate: instead it replicates data via
a minimal proxy struct on the pawn and then routes back to the ASC on 
receiving side. This is advantage since its A) a more minimal set of data 
B) takes advantage of pawn relevancy.

- Dave Ratti
```

Let's take a look on how to implement this.

### 1. The component and attribute subobjects are skipped inside ::ReplicateSubobjects() on the owning fortnite player state class

{% highlight c++ %}
bool ARBPlayerState::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
  check(Channel);
  check(Bunch);
  check(RepFlags);

  bool WroteSomething = false;

  for (UActorComponent* ActorComp : ReplicatedComponents)
  {
    if (ActorComp && ActorComp->GetIsReplicated())
    {
      if (!ActorComp->IsA(AbilitySystemComponent->GetClass()) || RepFlags->bNetOwner || !AbilitySystemComponent->ReplicationProxyEnabled)
      {
        WroteSomething |= ActorComp->ReplicateSubobjects(Channel, Bunch, RepFlags);
        WroteSomething |= Channel->ReplicateSubobject(ActorComp, *Bunch, *RepFlags);
      }
    }
  }
  return WroteSomething;
}
{% endhighlight %}

### 2. We do push the bare minimum replicated data from the ability system component to a structure on the pawn itself

By doing this we are considerably reducing the network bandwith used as we can decide which clients get our updates. Following next we present a plausible implementation:

#### 2.1. Define the replication proxy struct

{% highlight c++ %}
USTRUCT(BlueprintType)
struct GAME_API FReplicationProxyVarList
{
	GENERATED_BODY()

public:

	FReplicationProxyVarList() :
		GameplayTagsBitMask(0),
		AttributeOne(0.f),
		AttributeTwo(0.f)
	{

	}

	void Copy(uint8 inGameplayTagsBitMask, float inAttributeOne, float inAttributeTwo)
	{
		GameplayTagsBitMask = inGameplayTagsBitMask;
		AttributeOne = inAttributeOne;
		AttributeTwo = inAttributeTwo;
	}

public:

	UPROPERTY()
	uint8 GameplayTagsBitMask;
	
	UPROPERTY()
	float AttributeOne;
	
	UPROPERTY()
	float AttributeTwo;
};
{% endhighlight %}

In this struct we are using an 8 bits BitMask (`uint8`) to identify 8 different gameplay tags. We can achieve the same effect using `NetDeltaSerialize` the same way it's done in the `FHitResult` definition. This BitMask works like a set of booleans to represent whether certain tag exists or not, is up to the game-code interpret what each bit means.

Once the variable replicates, we can do the following to read its value on the client:

{% highlight c++ %}
bFirstTagExists = ((GameplayTagsBitMask & 0b00000001) != 0);
bFourthTagExists = ((GameplayTagsBitMask & 0b00001000) != 0);
bEighthTagExists = ((GameplayTagsBitMask & 0b10000000) != 0);
{% endhighlight %}

**Note:** To ease the explanation I've used binary notation to identify the flag position, but I recommend using hexadecimal for briefness.

#### 2.2. Define getters

This is not needed if you don't use Push Based replication model.

{% highlight c++ %}
FReplicationProxyVarList& ARBPlayerCharacter::Call_GetReplicationVarList_Mutable()
{
	MARK_PROPERTY_DIRTY_FROM_NAME(ARBPlayerCharacter, ReplicationVarList, this);
	return ReplicationVarList;
}
{% endhighlight %}

#### 2.3. Pass-in the data

{% highlight c++ %}
bool ARBPlayerState::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
  check(Channel);
  check(Bunch);
  check(RepFlags);

  bool WroteSomething = false;

  for (UActorComponent* ActorComp : ReplicatedComponents)
  {
    if (ActorComp && ActorComp->GetIsReplicated())
    {
      // We replicate replicate everything but simulated proxies in ASC
      if (!ActorComp->IsA(AbilitySystemComponent->GetClass()) || RepFlags->bNetOwner || !AbilitySystemComponent->ReplicationProxyEnabled)
      {
        WroteSomething |= ActorComp->ReplicateSubobjects(Channel, Bunch, RepFlags);
        WroteSomething |= Channel->ReplicateSubobject(ActorComp, *Bunch, *RepFlags);
      }
      else
      {
        ARBPlayerCharacter* MyCharacter = GetPawn<ARBPlayerCharacter>();
        MyCharacter->Call_GetReplicationVarList_Mutable().Copy(ServerFlags, 
          AbilitySystemComponent->GetNumericAttribute(URBAttributeSet_Dummy::GetOneAttribute()),
          AbilitySystemComponent->GetNumericAttribute(URBAttributeSet_Dummy::GetTwoAttribute()));
      }
    }
  }
  return WroteSomething;
}
{% endhighlight %}

### 3. ...and then routes back to the ASC on receiving side.
For that we'll use an OnRep in the receiving end struct:

{% highlight c++ %}
UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicationVarList)
FReplicationProxyVarList ReplicationVarList;
{% endhighlight %}

With the following implementation:

{% highlight c++ %}
void ARBPlayerCharacter::OnRep_ReplicationVarList()
{
	URBAbilitySystemComponent* ASC = GetGameAbilitySystemComponent();
	if (ASC)
	{
		// Update ASC client version of RepAnimMontageInfo
		ASC->SetNumericAttributeBase(URBAttributeSet_Movement::GetWalkingSpeedAttribute(), ReplicationVarList.AttributeOne);
		ASC->SetNumericAttributeBase(URBAttributeSet_Movement::GetSprintingSpeedAttribute(), ReplicationVarList.AttributeTwo);

    	// Here you should add the tags to the simulated proxies ie.:
    	bool bFirstTagExists = ((ReplicationVarList.GameplayTagsBitMask & 0x01) != 0);
   		if(bFirstTagExists)
    	{
      		ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag("Data.Sample"));
    	}
    	else
    	{
      		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag("Data.Sample"));
    	}
	}
}
{% endhighlight %}

By doing this, the simulated proxies will gather the desired replicated data marked down in the ReplicationProxyVarList Struct.

### 4. Handling gameplay cues

Since we are skipping simulated proxies in `ReplicateSubobjects`, we must handle the GameplayCues manually, since they no longer replicate to everyone. 

And this is a great change since we'll be using the Pawn's relevancy instead of the **boring** always relevant PlayerState.

1) The very first step is setting `ReplicationProxyEnabled` to `true` in the AbilitySystemComponent class.

2) Then, inherit `UAbilitySystemReplicationProxyInterface` and `IAbilitySystemReplicationProxyInterface`:

{% highlight c++ %}
UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class GAME_API URBAbilitySystemReplicationProxyInterface : public UAbilitySystemReplicationProxyInterface
{
	GENERATED_BODY()
};

struct FGameplayAbilityRepAnimMontage;

class GAME_API IRBAbilitySystemReplicationProxyInterface : public IAbilitySystemReplicationProxyInterface
{
	GENERATED_BODY()

public:

	virtual FGameplayAbilityRepAnimMontage& Call_GetRepAnimMontageInfo_Mutable() = 0;

	virtual void Call_OnRep_ReplicatedAnimMontage() = 0;
};
{% endhighlight %}

3) Following next, implement the full interface in our replication proxy class, in my case the `ARBPlayerCharacter`:

{% highlight c++ %}
UCLASS()
class GAME_API ARBPlayerCharacter : public ARBCharacter,
									public IAbilitySystemInterface,
									public IRBAbilitySystemReplicationProxyInterface,
{
{% endhighlight %}

4) Now, be prepared to write boilerplate code. Starting with the header:

{% highlight c++ %}
// Begin: IAbilitySystemReplicationProxyInterface ~~ 
UPROPERTY(ReplicatedUsing = Call_OnRep_ReplicatedAnimMontage)
FGameplayAbilityRepAnimMontage RepAnimMontageInfo;

void ForceReplication() override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueExecuted_FromSpec(const FGameplayEffectSpecForRPC Spec, FPredictionKey PredictionKey) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueExecuted(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCuesExecuted(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueExecuted_WithParams(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCuesExecuted_WithParams(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueAdded(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueAdded_WithParams(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters Parameters) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueAddedAndWhileActive_FromSpec(const FGameplayEffectSpecForRPC& Spec, FPredictionKey PredictionKey) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCueAddedAndWhileActive_WithParams(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters) override;

UFUNCTION(NetMulticast, unreliable)
void NetMulticast_InvokeGameplayCuesAddedAndWhileActive_WithParams(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters) override;
  
FGameplayAbilityRepAnimMontage& Call_GetRepAnimMontageInfo_Mutable() override;

UFUNCTION()
void Call_OnRep_ReplicatedAnimMontage() override;
// End: IAbilitySystemReplicationProxyInterface ~~ 
{% endhighlight %}

And continuing with the implementation:

{% highlight c++ %}
void ARBPlayerCharacter::ForceReplication()
{
	ForceNetUpdate();
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueExecuted_FromSpec_Implementation(const FGameplayEffectSpecForRPC Spec, FPredictionKey PredictionKey)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(Spec, EGameplayCueEvent::Executed);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueExecuted_Implementation(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::Executed, EffectContext);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCuesExecuted_Implementation(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		for (const FGameplayTag& GameplayCueTag : GameplayCueTags)
		{
			GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::Executed, EffectContext);
		}
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueExecuted_WithParams_Implementation(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::Executed, GameplayCueParameters);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCuesExecuted_WithParams_Implementation(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		for (const FGameplayTag& GameplayCueTag : GameplayCueTags)
		{
			GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::Executed, GameplayCueParameters);
		}
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueAdded_Implementation(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayEffectContextHandle EffectContext)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::OnActive, EffectContext);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueAdded_WithParams_Implementation(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters Parameters)
{
	// If server generated prediction key and auto proxy, skip this message. 
	// This is an RPC from mixed replication mode code, we will get the "real" message from our OnRep on the autonomous proxy
	// See UAbilitySystemComponent::AddGameplayCue_Internal for more info.
	
	bool bIsMixedReplicationFromServer = (GetAbilitySystemComponent()->ReplicationMode == EGameplayEffectReplicationMode::Mixed && PredictionKey.IsServerInitiatedKey() && IsLocallyControlled());

	if (HasAuthority() || (PredictionKey.IsLocalClientKey() == false && !bIsMixedReplicationFromServer))
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::OnActive, Parameters);
	}
}


void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueAddedAndWhileActive_FromSpec_Implementation(const FGameplayEffectSpecForRPC& Spec, FPredictionKey PredictionKey)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(Spec, EGameplayCueEvent::OnActive);
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(Spec, EGameplayCueEvent::WhileActive);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCueAddedAndWhileActive_WithParams_Implementation(const FGameplayTag GameplayCueTag, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::OnActive, GameplayCueParameters);
		GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::WhileActive, GameplayCueParameters);
	}
}

void ARBPlayerCharacter::NetMulticast_InvokeGameplayCuesAddedAndWhileActive_WithParams_Implementation(const FGameplayTagContainer GameplayCueTags, FPredictionKey PredictionKey, FGameplayCueParameters GameplayCueParameters)
{
	if (HasAuthority() || PredictionKey.IsLocalClientKey() == false)
	{
		for (const FGameplayTag& GameplayCueTag : GameplayCueTags)
		{
			GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::OnActive, GameplayCueParameters);
			GetAbilitySystemComponent()->InvokeGameplayCueEvent(GameplayCueTag, EGameplayCueEvent::WhileActive, GameplayCueParameters);
		}
	}
}

FGameplayAbilityRepAnimMontage& ARBPlayerCharacter::Call_GetRepAnimMontageInfo_Mutable()
{
	MARK_PROPERTY_DIRTY_FROM_NAME(ARBPlayerCharacter, RepAnimMontageInfo, this);
	return RepAnimMontageInfo;
}

void ARBPlayerCharacter::Call_OnRep_ReplicatedAnimMontage()
{
	URBAbilitySystemComponent* ASC = GetGameAbilitySystemComponent();
	if (ASC)
	{
		// Update ASC client version of RepAnimMontageInfo
		ASC->SetRepAnimMontageInfoAccessor(RepAnimMontageInfo);
		// Call OnRep of AnimMontageInfo
		ASC->ReplicatedAnimMontageOnRepAccesor();
	}
}
{% endhighlight %}

5) And finally, override and add these functions to your Ability System Component:

{% highlight c++ %}
// Replication proxy helpers and accesors - CRITICAL TO KEEP UPDATED ON MAJOR REVISIONS
IRBAbilitySystemReplicationProxyInterface* GetExtendedReplicationInterface();

void ReplicatedAnimMontageOnRepAccesor();

void SetRepAnimMontageInfoAccessor(const FGameplayAbilityRepAnimMontage& NewRepAnimMontageInfo);

float PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage, float InPlayRate, FName StartSectionName = NAME_None, float StartTimeSeconds = 0.0f) override;
{% endhighlight %}

Followed by the implementation:

{% highlight c++ %}
IRBAbilitySystemReplicationProxyInterface* URBAbilitySystemComponent::GetExtendedReplicationInterface()
{
	if (ReplicationProxyEnabled)
	{
		// Note the expectation is that when the avatar actor is null (e.g during a respawn) that we do return null and calling code handles this (by probably not replicating whatever it was going to)
		return Cast<IRBAbilitySystemReplicationProxyInterface>(GetAvatarActor_Direct());
	}

	return nullptr;
}

void URBAbilitySystemComponent::ReplicatedAnimMontageOnRepAccesor()
{
	OnRep_ReplicatedAnimMontage();
}

void URBAbilitySystemComponent::SetRepAnimMontageInfoAccessor(const FGameplayAbilityRepAnimMontage& NewRepAnimMontageInfo)
{
	SetRepAnimMontageInfo(NewRepAnimMontageInfo);
}

float URBAbilitySystemComponent::PlayMontage(UGameplayAbility* InAnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* NewAnimMontage, float InPlayRate, FName StartSectionName, float StartTimeSeconds)
{
	float Duration = -1.f;

	UAnimInstance* AnimInstance = AbilityActorInfo.IsValid() ? AbilityActorInfo->GetAnimInstance() : nullptr;
	if (AnimInstance && NewAnimMontage)
	{
		Duration = AnimInstance->Montage_Play(NewAnimMontage, InPlayRate, EMontagePlayReturnType::MontageLength, StartTimeSeconds);
		if (Duration > 0.f)
		{
			if (LocalAnimMontageInfo.AnimatingAbility && LocalAnimMontageInfo.AnimatingAbility != InAnimatingAbility)
			{
				// The ability that was previously animating will have already gotten the 'interrupted' callback.
				// It may be a good idea to make this a global policy and 'cancel' the ability.
				// 
				// For now, we expect it to end itself when this happens.
			}

			if (NewAnimMontage->HasRootMotion() && AnimInstance->GetOwningActor())
			{
				UE_LOG(LogRootMotion, Log, TEXT("UAbilitySystemComponent::PlayMontage %s, Role: %s")
					, *GetNameSafe(NewAnimMontage)
					, *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), AnimInstance->GetOwningActor()->GetLocalRole())
				);
			}

			LocalAnimMontageInfo.AnimMontage = NewAnimMontage;
			LocalAnimMontageInfo.AnimatingAbility = InAnimatingAbility;
			LocalAnimMontageInfo.PlayBit = !LocalAnimMontageInfo.PlayBit;

			if (InAnimatingAbility)
			{
				InAnimatingAbility->SetCurrentMontage(NewAnimMontage);
			}

			// Start at a given Section.
			if (StartSectionName != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(StartSectionName, NewAnimMontage);
			}

			// Replicate to non owners
			if (IsOwnerActorAuthoritative())
			{
				IRBAbilitySystemReplicationProxyInterface* ReplicationInterface = GetExtendedReplicationInterface();
				FGameplayAbilityRepAnimMontage& MutableRepAnimMontageInfo = ReplicationInterface ? ReplicationInterface->Call_GetRepAnimMontageInfo_Mutable() : GetRepAnimMontageInfo_Mutable();

				// Those are static parameters, they are only set when the montage is played. They are not changed after that.
				MutableRepAnimMontageInfo.AnimMontage = NewAnimMontage;
				MutableRepAnimMontageInfo.ForcePlayBit = !bool(MutableRepAnimMontageInfo.ForcePlayBit);

				MutableRepAnimMontageInfo.SectionIdToPlay = 0;
				if (MutableRepAnimMontageInfo.AnimMontage && StartSectionName != NAME_None)
				{
					// we add one so INDEX_NONE can be used in the on rep
					MutableRepAnimMontageInfo.SectionIdToPlay = MutableRepAnimMontageInfo.AnimMontage->GetSectionIndex(StartSectionName) + 1;
				}

				// Update parameters that change during Montage life time.
				AnimMontage_UpdateReplicatedData(MutableRepAnimMontageInfo);

				// Force net update on our avatar actor
				if (AbilityActorInfo->AvatarActor != nullptr)
				{
					AbilityActorInfo->AvatarActor->ForceNetUpdate();
				}
			}
			else
			{
				// If this prediction key is rejected, we need to end the preview
				FPredictionKey PredictionKey = GetPredictionKeyForNewAction();
				if (PredictionKey.IsValidKey())
				{
					PredictionKey.NewRejectedDelegate().BindUObject(this, &URBAbilitySystemComponent::OnPredictiveMontageRejected, NewAnimMontage);
				}
			}
		}
	}

	return Duration;
}
{% endhighlight %}

And... we are done. With this we will be able to execute Gameplay Cues which will be shown to everyone using the Character relevancy.

# Some tips

What a ride... well, to sum up I'd like provide some tips regarding the matter:
* You probably don't need to replicate that attribute.
* Use the replication graph, it now exists!
* Consider wisely if you really need these optimizations, the amount of boilerplate coded needed is significant.
* The more boilerplate code you add, the greater the chances of introducing bugs into your codebase, be careful!
* [Profile, please](https://docs.unrealengine.com/4.27/en-US/InteractiveExperiences/Networking/NetworkProfiler/).
* Read the engine's source code, and when in doubt, ask! No one will judge you because you've got questions!
* [Follow me](https://twitter.com/vorixo) on twitter, lol. I had to do it!

And... let me know if I forgot something! It's a lot of code and information, I totally might have.

Enjoy, vorixo.
