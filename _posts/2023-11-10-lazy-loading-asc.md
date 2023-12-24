---
layout: single
title: "Lazy loading the Ability System Component (GAS) in Multiplayer games"
excerpt: In this post we'll learn how to lazy load the ability system component and why this can be benefitial for our games.
header:
  teaser: /assets/images/per-post/lazy-loading-asc/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - GAS
  - Ability System
  - Networking
---

Hello! In this post we'll learn together how to Lazy Load the [Ability System Component](https://vorixo.github.io/devtricks/gas/) in our multiplayer games to reduce memory usage!

# Introduction

The Ability System Component (ASC) can be a bit resource-intensive, especially when dealing with World Actors that require it, like Damageables. If our world is populated with such Actors, their memory footprint can become noticeable.

Taking a cue from Epic and the insights shared by Tranek in the excellent [GASDocumentation](https://github.com/tranek/GASDocumentation#optimizations-asclazyloading), Fortnite adopts a smart approach. They lazily load their ASCs in their World Actors just when they're needed, dodging the memory hit until it's absolutely necessary. This memory optimization is a significant boost for scalability; since ASC World Actors won't carry that footprint unless explicitly interacted with.

Think of it like this: buildings, trees, and damageable props on standby with a lower cost, only racking up the memory bill when they step into the gameplay spotlight.

# The implementation

So... let's take a look on how we can implement this. Bear with me, as Epic hasn't posted this anywhere online, so if you spot any error, feel free to report it!

The first step is to create an Actor type that will hold this behaviour, in my case I called it `AMyGameplayActor`. It is critical that your actor implements the `IAbilitySystemInterface` interface, as we'll require to override the `GetAbilitySystemComponent` function.

In this Actor, I've defined an Enum `EAbilitySystemCreationPolicy` that determines the ASC loading behaviour:

{% highlight c++ %}
/**
 *	Defines how a the Ability System is loaded (if ever).
 */
UENUM(BlueprintType)
enum class EAbilitySystemCreationPolicy : uint8
{
	Never UMETA(ToolTip = "The Ability System will be always null"),
	Lazy UMETA(ToolTip = "The Ability System will be null in the client until it is used in the server"),
	Always UMETA(ToolTip = "The Ability System is loaded when the Actor loads"),
};
{% endhighlight %}

I've also defined other relevant properties for the implementation, such as the Ability System Component class, the Attribute mutation buffer, or the runtime ASC transient property:

{% highlight c++ %}
protected:
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TSubclassOf<UMyAbilitySystemComponent> AbilitySystemComponentClass;

	UPROPERTY(Transient)
	TObjectPtr<UMyAbilitySystemComponent> AbilitySystemComponent = nullptr;

private:

	UPROPERTY(Transient, ReplicatedUsing=OnRep_ReplicatedAbilitySystemComponent)
	TObjectPtr<UMyAbilitySystemComponent> ReplicatedAbilitySystemComponent = nullptr;

	struct FPendingAttributeReplication
	{
		FPendingAttributeReplication()
		{
		}

		FPendingAttributeReplication(const FGameplayAttribute& InAttribute, const FGameplayAttributeData& InNewValue)
		{
			Attribute = InAttribute;
			NewValue = InNewValue;
		}

		FGameplayAttribute Attribute;
		FGameplayAttributeData NewValue;
	};

	TArray<struct FPendingAttributeReplication> PendingAttributeReplications;
{% endhighlight %}

Gameplay Actors need to replicate by default, so we setup their constructor appropriately:

{% highlight c++ %}
AMyGameplayActor::AMyGameplayActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	AbilitySystemComponentClass = UMyAbilitySystemComponent::StaticClass();

	bReplicates = true;
}
{% endhighlight %}

Then, we create a the relevant functions to create the ASC and to Initialize it:

{% highlight c++ %}
void AMyGameplayActor::CreateAbilitySystem()
{
	AbilitySystemComponent = NewObject<UMyAbilitySystemComponent>(this, AbilitySystemComponentClass);
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	AbilitySystemComponent->RegisterComponent();
}

void AMyGameplayActor::InitializeAbilitySystem()
{
	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	if (HasAuthority())
	{
		// @TODO: Grant Abilities

		// @TODO: Grant and initialize Attributes
	}

	// @TODO: Add static gameplay tags server/client
}
{% endhighlight %}

Following next, we override and implement the functions below to accomodate to the ASC creation policies we defined:

{% highlight c++ %}
void AMyGameplayActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	if (AbilitySystemComponentCreationPolicy == EAbilitySystemCreationPolicy::Always && GetNetMode() != NM_Client)
	{
		check(!AbilitySystemComponent);
		CreateAbilitySystem();
		InitializeAbilitySystem();
		ForceNetUpdate();
	}
}

void AMyGameplayActor::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	if (!ReplicatedAbilitySystemComponent && AbilitySystemComponent && AbilitySystemComponentCreationPolicy != EAbilitySystemCreationPolicy::Never)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, ReplicatedAbilitySystemComponent, this);
		ReplicatedAbilitySystemComponent = AbilitySystemComponent;
	}
}

void AMyGameplayActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, ReplicatedAbilitySystemComponent, Params);
}

UAbilitySystemComponent* AMyGameplayActor::GetAbilitySystemComponent() const
{
	if (!AbilitySystemComponent && HasAuthority() && AbilitySystemComponentCreationPolicy == EAbilitySystemCreationPolicy::Lazy && GetWorld() && !IsUnreachable())
	{
		AMyGameplayActor* MutableActor = const_cast<AMyGameplayActor*>(this);
		MutableActor->CreateAbilitySystem();
		MutableActor->InitializeAbilitySystem();
		MutableActor->ForceNetUpdate();
	}
	return AbilitySystemComponent;
}

void AMyGameplayActor::ApplyPendingAttributesFromReplication()
{
	check(AbilitySystemComponent);

	if (PendingAttributeReplications.Num() > 0)
	{
		for (const FPendingAttributeReplication& Pending : PendingAttributeReplications) 
		{
			AbilitySystemComponent->DeferredSetBaseAttributeValueFromReplication(Pending.Attribute, Pending.NewValue);
		}
		PendingAttributeReplications.Empty();
	}
}

void AMyGameplayActor::OnRep_ReplicatedAbilitySystemComponent()
{
	AbilitySystemComponent = ReplicatedAbilitySystemComponent;
	if (AbilitySystemComponent)
	{
		InitializeAbilitySystem();
		ApplyPendingAttributesFromReplication();
	}
}

void AMyGameplayActor::SetPendingAttributeFromReplication(const FGameplayAttribute& Attribute, const FGameplayAttributeData& NewValue)
{
	PendingAttributeReplications.Emplace(FPendingAttributeReplication(Attribute, NewValue));
}
{% endhighlight %}

As we can see, if the policy is `Lazy`, the first time we call `GetAbilitySystemComponent()` through any of the ASC API functions, the ASC will get created and initialized. The `PreReplication` function and our proxy `ReplicatedAbilitySystemComponent` variable will make sure `AbilitySystemComponent` will get to the client and Server properly.

## Pending Attribute Replication

When the Ability System Component gets created in the server, it takes latency time before it reaches the client, and during that time, Attributes might need to replicate (accounting for Net Priority and Frequency on the owning Actors).

During this time, we need to hold the attribute changes somewhere until the ASC is available in the Client, for that, we use the `PendingAttributeReplications` Array defined in our Gameplay Actor.

To buffer the Attribute mutations, we need to route Attribute replication through our Gameplay Actor. In our base Attribute class we have in our game define the following macro:

{% highlight c++ %}
#define MYGAMEPLAYATTRIBUTE_REPNOTIFY(ClassName, PropertyName, OldValue) \
{ \
	static FProperty* ThisProperty = FindFieldChecked<FProperty>(ClassName::StaticClass(), GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	if (!GetOwningAbilitySystemComponent()) \
	{ \
		if (AMyGameplayActor* GameplayActor = Cast<AMyGameplayActor>(GetOwningActor())) \
		{ \
			GameplayActor->SetPendingAttributeFromReplication(FGameplayAttribute(ThisProperty), PropertyName); \
		} \
		return; \
	} \
	else \
	{ \
		GetOwningAbilitySystemComponent()->SetBaseAttributeValueFromReplication(FGameplayAttribute(ThisProperty), PropertyName, OldValue); \
	} \
}
{% endhighlight %}

Then, in our child Attribute Sets, we have to implement our `OnRep` as follows:

{% highlight c++ %}
void UMyAttributeSet_Health::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	MYGAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet_Health, Health, OldHealth); 
}

void UMyAttributeSet_Health::OnRep_Shield(const FGameplayAttributeData& OldShield)
{
	MYGAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet_Health, Shield, OldShield);
}
{% endhighlight %}

As we can see, the macro we created calls `SetPendingAttributeFromReplication` we defined in our `AMyGameplayActor` from the Attribute Set `OnRep` functions, which fill our buffer Array `PendingAttributeReplications` which gets consumed in `ApplyPendingAttributesFromReplication`, when the ASC reaches the client in `AMyGameplayActor`.

`ApplyPendingAttributesFromReplication` calls `DeferredSetBaseAttributeValueFromReplication` which will ensure we have the most up to date attribute values on the client:

{% highlight c++ %}
void UMyAbilitySystemComponent::DeferredSetBaseAttributeValueFromReplication(const FGameplayAttribute& Attribute, float NewValue)
{
	const float OldValue = ActiveGameplayEffects.GetAttributeBaseValue(Attribute);
	ActiveGameplayEffects.SetAttributeBaseValue(Attribute, NewValue);
	SetBaseAttributeValueFromReplication(Attribute, NewValue, OldValue);
	// TODO: You can process deferred delegates here
}

void UMyAbilitySystemComponent::DeferredSetBaseAttributeValueFromReplication(const FGameplayAttribute& Attribute, const FGameplayAttributeData& NewValue)
{
	const float OldValue = ActiveGameplayEffects.GetAttributeBaseValue(Attribute);
	ActiveGameplayEffects.SetAttributeBaseValue(Attribute, NewValue.GetBaseValue());
	SetBaseAttributeValueFromReplication(Attribute, NewValue.GetBaseValue(), OldValue);
	// TODO: You can process deferred delegates here
}
{% endhighlight %}

# Conclusion

Thanks for reading!

Now you'll have a way to save some memory from your ASC Gameplay Actors! I hope you found this article helpful!

I'm going to leave a remaining challenge to the curious reader: Deferred delegates! These deferred delegates should happen when the ASC reaches the client in our Gameplay Actor, and it would give us the possibility to react to the buffered changes. If you get to implement this, feel free to share it!

Enjoy, [vori](https://twitter.com/vorixo).