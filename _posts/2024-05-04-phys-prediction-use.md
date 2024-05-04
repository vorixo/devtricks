---
layout: single
title: "Networked Physics and UNetworkPhysicsComponent in Unreal Engine 5.4"
excerpt: In this post we'll learn how to use the new UNetworkPhysicsComponent component used for networked physics introduced in UE5.4.
header:
  teaser: /assets/images/per-post/phys-prediction-use/thumb.jpg
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Physics Prediction
  - Networking
---

In a [previous article](https://vorixo.github.io/devtricks/phys-prediction-show/) we saw a very premature version of the physics prediction system that now bundles with Unreal Engine. Now, in Unreal Engine 5.4 the system has matured a lot and in todays article we will do a hands-on together.

# Introduction

Predicting and correcting physics is one of the most complicated topics when it comes to network prediction. The nature of the complexity comes by the fact that if we hit a chain of physics objects predictively, we'd expect all the physics to react instantaneously, without having to wait for the server. But not only that, these physics objects should smoothly correct their position on the clients if there are incongruences with what the server expects. So you can imagine how difficult this can get with complex physics chain reactions.

Fortunately, Epic has been working lately in a solution for all these problems bundled in their Networked Physics solution. Before digging onto the matter, I strongly recommend everyone to take a look to [this article](https://dev.epicgames.com/documentation/en-us/unreal-engine/networked-physics-overview), where Epic digs further on the different physics replication modes they brought to the engine with this new system.

# Making a rolling boulder

I decided to make this post as I couldnt find any simple example around the engine on how to use properly the new Networked Physics system. The 'rolling boulder' we are going to create may serve as a "hello world" for those that would like to get introduced in the matter, as this new system can be used to make any type of vehicle or physics controllable object. Because... what's easier than a rolling sphere?

However, this tutorial comes with a big disclaimer, as we are in a very early r&d phase, and we dont have yet any official technical documentation on the matter, other than the engine itself, and... my own assumptions! 

So... as I always say, don't trust random articles from the internet and please, do your own research! If you think that any of the information shared in here is off, you can always [contact me on twitter](https://twitter.com/vorixo) and we can fix it together as a community effort! Now... lets get into it :D! 

## The setup

In order to activate the feature, open `Project Settings` and within the `Physics` tab, turn on `Enable Physics Prediction`.

![Network Physics Prediction]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-show/enableit.jpg){: .align-center}

There are other settings you might want to explore! 

Then, lets not forget to enable the following settings in the same tab:

![Physics Settings]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/physics-settings.jpg){: .align-center}

One of the core components of the new physics system is the `UNetworkPhysicsComponent`, and to date, this component enforces us to wrap our movement code in another component, so we'll have a physics movement component and a controllable pawn that holds said component, let's create the pawn:

![Boulder Actor]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/boulderactor.jpg){: .align-center}

Pay close attention to the replication settings, as they will play a relevant role in our implementation. Worth noting that the physics replication mode we are choosing for our boulder is **Resimulation**; in addition im leveraging a really low [Net Update Frequency](https://dev.epicgames.com/documentation/en-us/unreal-engine/detailed-actor-replication-flow-in-unreal-engine) (not using Iris yet!), simply to show how great this system is.

And to finish up the setup, let me share with you my root static mesh settings that we use for the physics simulation:

![Root mesh]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/rootmeshsettings.jpg){: .align-center}

Main important points from the image above is that, we need to turn on physics simulation, set the collision channel to something sensitive, and finally, ensure that the `Component Replicates` flag is off, we don't need it, we are replicating the transform of our boulder by other means.

## Coding the physics boulder movement

The way I've found other systems in the engine forward their physics input to the server is by means of the `UNetworkPhysicsComponent`. This component not only takes care of that, it also mantains historical records of both inputs and states, enabling the rewind and resimulation of physics simulations, crucial for maintaining consistency and accuracy in multiplayer environments. So, let's make use of these features!

The `UNetworkPhysicsComponent` requires us to create two structs, which hold the input and the state of our physics simulation:

{% highlight c++ %}
/** Ball inputs from the player controller */
USTRUCT()
struct FBallInputs
{
	GENERATED_BODY()

	FBallInputs()
		: SteeringInput(0.f)
		, ThrottleInput(0.f)
		, TravelDirection(FRotator::ZeroRotator)
		, JumpCount(0)
	{}

	// Steering output to physics system. Range -1...1
	UPROPERTY()
	float SteeringInput;

	// Accelerator output to physics system. Range -1...1
	UPROPERTY()
	float ThrottleInput;

	// Desired direction
	UPROPERTY()
	FRotator TravelDirection;

	/** Counter for user jumps */
	UPROPERTY()
	int32 JumpCount;
};

/** Ball state data that will be used in the state history to rewind the simulation at some point in time */
USTRUCT()
struct FNetworkBallStates : public FNetworkPhysicsData
{
	GENERATED_BODY()

	/**  Serialize data function that will be used to transfer the struct across the network */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNetworkBallStates> : public TStructOpsTypeTraitsBase2<FNetworkBallStates>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/** Ball Inputs data that will be used in the inputs history to be applied while simulating */
USTRUCT()
struct FNetworkBallInputs : public FNetworkPhysicsData
{
	GENERATED_BODY()

	/** List of incoming control inputs coming from the local client */
	UPROPERTY()
	FBallInputs BallInputs;

	/**  Apply the data onto the network physics component */
	virtual void ApplyData(UActorComponent* NetworkComponent) const override;

	/**  Build the data from the network physics component */
	virtual void BuildData(const UActorComponent* NetworkComponent) override;

	/**  Serialize data function that will be used to transfer the struct across the network */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Interpolate the data in between two inputs data */
	virtual void InterpolateData(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData) override;

	/** Merge data into this input */
	virtual void MergeData(const FNetworkPhysicsData& FromData) override;
};

template<>
struct TStructOpsTypeTraits<FNetworkBallInputs> : public TStructOpsTypeTraitsBase2<FNetworkBallInputs>
{
	enum
	{
		WithNetSerializer = true,
	};
};

struct FPhysicsBallTraits
{
	using InputsType = FNetworkBallInputs;
	using StatesType = FNetworkBallStates;
};
{% endhighlight %}

Note that, in my case I don't require to keep track of any state of my boulder, but I still need to create the struct since the system requires it so.

Following next, the declaration of my movement class, I decided to use a `UPawnMovementComponent` in this case:

{% highlight c++ %}
CLASS(meta = (BlueprintSpawnableComponent))
class MYPROJECT_API URollingBallMovementComponent : public UPawnMovementComponent
{
	GENERATED_UCLASS_BODY()
	
public:

	/** Overridden to allow registration with components NOT owned by a Pawn. */
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/** Return true if it's suitable to create a physics representation of the Ball at this time */
	virtual bool ShouldCreatePhysicsState() const override;

	/** Used to create any physics engine information for this component */
	virtual void OnCreatePhysicsState() override;

	/** Used to shut down and physics engine structure for this component */
	virtual void OnDestroyPhysicsState() override;

	void InitializeBall();

	virtual void AsyncPhysicsTickComponent(float DeltaTime, float SimTime);

	UFUNCTION(BlueprintCallable)
	void SetThrottleInput(float InThrottle);

	UFUNCTION(BlueprintCallable)
	void SetSteeringInput(float InSteering);

	UFUNCTION(BlueprintCallable)
	void SetTravelDirectionInput(FRotator InTravelDirection);

	UFUNCTION(BlueprintCallable)
	void Jump();

public:

	// Ball inputs
	FBallInputs BallInputs;

	// Ball state
	FTransform BallWorldTransform;
	FVector BallForwardAxis;
	FVector BallRightAxis;

private:

	UPROPERTY()
	TObjectPtr<UNetworkPhysicsComponent> NetworkPhysicsComponent = nullptr;

	Chaos::FRigidBodyHandle_Internal* RigidHandle;
	FBodyInstance* BodyInstance;
	int32 PreviousJumpCount = 0;
};
{% endhighlight %}

And finally, the implementation:

{% highlight c++ %}
URollingBallMovementComponent::URollingBallMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);

	static const FName NetworkPhysicsComponentName(TEXT("PC_NetworkPhysicsComponent"));
	NetworkPhysicsComponent = CreateDefaultSubobject<UNetworkPhysicsComponent, UNetworkPhysicsComponent>(NetworkPhysicsComponentName);
	NetworkPhysicsComponent->SetNetAddressable(); // Make DSO components net addressable
	NetworkPhysicsComponent->SetIsReplicated(true);
}

void URollingBallMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	UNavMovementComponent::SetUpdatedComponent(NewUpdatedComponent);
	PawnOwner = NewUpdatedComponent ? Cast<APawn>(NewUpdatedComponent->GetOwner()) : nullptr;
}

bool URollingBallMovementComponent::ShouldCreatePhysicsState() const
{
	if (!IsRegistered() || IsBeingDestroyed())
	{
		return false;
	}

	// only create 'Physics' Ball in game
	UWorld* World = GetWorld();
	if (World->IsGameWorld())
	{
		FPhysScene* PhysScene = World->GetPhysicsScene();

		if (PhysScene && UpdatedComponent && UpdatedPrimitive)
		{
			return true;	
		}
	}

	return false;
}

void URollingBallMovementComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	// only create Physics Ball in game
	UWorld* World = GetWorld();
	if (World->IsGameWorld())
	{
		InitializeBall();
			
		if (NetworkPhysicsComponent)
		{
			NetworkPhysicsComponent->CreateDataHistory<FPhysicsBallTraits>(this);
		}
	}

	// Initializing phys handle
	if (UPrimitiveComponent* Mesh = Cast<UPrimitiveComponent>(UpdatedComponent))
	{
		BodyInstance = Mesh->GetBodyInstance();
	}	
}

void URollingBallMovementComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();

	if (UpdatedComponent)
	{
		UpdatedComponent->RecreatePhysicsState();
	}
	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->RemoveDataHistory();
	}
}

void URollingBallMovementComponent::InitializeBall()
{
	if (UpdatedComponent && UpdatedPrimitive)
	{
		SetAsyncPhysicsTickEnabled(true);
	}
}

void URollingBallMovementComponent::AsyncPhysicsTickComponent(float DeltaTime, float SimTime)
{
	Super::AsyncPhysicsTickComponent(DeltaTime, SimTime);

	if (!BodyInstance)
	{
		return;
	}

	if (const auto Handle = BodyInstance->ActorHandle)
	{
		RigidHandle = Handle->GetPhysicsThreadAPI();
	}
	
	UWorld* World = GetWorld();
	if (World && RigidHandle)
	{
		const FTransform WorldTM(RigidHandle->R(), RigidHandle->X());
		BallWorldTransform = WorldTM;
		BallForwardAxis = BallWorldTransform.GetUnitAxis(EAxis::X);
		BallRightAxis = BallWorldTransform.GetUnitAxis(EAxis::Y);

		const FVector DesiredForwardVector = BallInputs.TravelDirection.Vector();
		const FVector DesiredRightVector = FRotationMatrix(BallInputs.TravelDirection).GetScaledAxis(EAxis::Y);

		// Update the simulation forces/impulses...
		if (BallInputs.JumpCount != PreviousJumpCount)
		{
			RigidHandle->SetLinearImpulse(FVector(0,0,500.f), true);
		}

		RigidHandle->AddForce(BallInputs.ThrottleInput * DesiredForwardVector * 80000.f, true);
		RigidHandle->AddForce(BallInputs.SteeringInput * DesiredRightVector * 80000.f, false);

		// Set prev frame vars
		PreviousJumpCount = BallInputs.JumpCount;
	}
}

void URollingBallMovementComponent::SetThrottleInput(float InThrottle)
{
	const float FinalThrottle = FMath::Clamp(InThrottle, -1.f, 1.f);
	BallInputs.ThrottleInput = InThrottle;
}

void URollingBallMovementComponent::SetSteeringInput(float InSteering)
{
	const float FinalSteering = FMath::Clamp(InSteering, -1.f, 1.f);
	BallInputs.SteeringInput = InSteering;
}

void URollingBallMovementComponent::SetTravelDirectionInput(FRotator InTravelDirection)
{
	BallInputs.TravelDirection = InTravelDirection;
}

void URollingBallMovementComponent::Jump()
{
	BallInputs.JumpCount++;
}

bool FNetworkBallInputs::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FNetworkPhysicsData::SerializeFrames(Ar);

	Ar << BallInputs.SteeringInput;
	Ar << BallInputs.ThrottleInput;
	Ar << BallInputs.TravelDirection;
	Ar << BallInputs.JumpCount;

	bOutSuccess = true;
	return bOutSuccess;
}

void FNetworkBallInputs::ApplyData(UActorComponent* NetworkComponent) const
{
	if (URollingBallMovementComponent* BallMover = Cast<URollingBallMovementComponent>(NetworkComponent))
	{
		BallMover->BallInputs = BallInputs;
	}
}

void FNetworkBallInputs::BuildData(const UActorComponent* NetworkComponent)
{
	if (NetworkComponent)
	{
		if (const URollingBallMovementComponent* BallMover = Cast<const URollingBallMovementComponent>(NetworkComponent))
		{
			BallInputs = BallMover->BallInputs;
		}
	}
}

void FNetworkBallInputs::InterpolateData(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData)
{
	const FNetworkBallInputs& MinInput = static_cast<const FNetworkBallInputs&>(MinData);
	const FNetworkBallInputs& MaxInput = static_cast<const FNetworkBallInputs&>(MaxData);

	const float LerpFactor = MaxInput.LocalFrame == LocalFrame
		? 1.0f / (MaxInput.LocalFrame - MinInput.LocalFrame + 1) // Merge from min into max
		: (LocalFrame - MinInput.LocalFrame) / (MaxInput.LocalFrame - MinInput.LocalFrame); // Interpolate from min to max


	BallInputs.ThrottleInput = FMath::Lerp(MinInput.BallInputs.ThrottleInput, MaxInput.BallInputs.ThrottleInput, LerpFactor);
	BallInputs.SteeringInput = FMath::Lerp(MinInput.BallInputs.SteeringInput, MaxInput.BallInputs.SteeringInput, LerpFactor);
	BallInputs.TravelDirection = FMath::Lerp(MinInput.BallInputs.TravelDirection, MaxInput.BallInputs.TravelDirection, LerpFactor);
	BallInputs.JumpCount = LerpFactor < 0.5 ? MinInput.BallInputs.JumpCount : MaxInput.BallInputs.JumpCount;
}

void FNetworkBallInputs::MergeData(const FNetworkPhysicsData& FromData)
{
	// Perform merge through InterpolateData
	InterpolateData(FromData, *this);
}

bool FNetworkBallStates::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	FNetworkPhysicsData::SerializeFrames(Ar);
	return true;
}
{% endhighlight %}

Note that the implementation above is just a "hello world", it doesnt do any input sanitization in the server for simplicity.
{: .notice--info}

If you did everything correctly, now you should have something as follows:

<iframe width="480" height="270" src="https://www.youtube.com/embed/kFtZqNlcg3U" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

The above video was recorded with average latency everywhere (server and client).

### Implementation details

Now that we all are in the same page, I think its worth noting different details from the implementation from above.

At the start of our physics simulation (`OnCreatePhysicsState`), we let the `NetworkPhysicsComponent` know which data it should handle for our physics simulations. It's like magic – super simple to use! We just need to specify the data's type, the component holding it, and then implement overrides for input and state handling. `ApplyData` and `BuildData` are key here; they smoothly transfer data between our movement component and the `NetworkPhysicsComponent`, which takes care of all the nitty-gritty details of state and input management for us.

The input processing goes like this: during `AsyncPhysicsTickComponent`, inputs are first applied locally, then the server catches up. This allows for predictive autonomous proxy simulation followed by server validation

Now, we're mostly dealing with continuous input streams, like movement directions or camera orientation (`TravelDirection`), which are straightforward. However, sending one-frame events, like a jump action, is still a puzzle. I've experimented with boolean input variables, such as `bJump`, but encountered issues – the server sometimes missed the update because the variable reset before the `NetworkPhysicsComponent` could transmit it. This led me to try alternative approaches, like using `JumpCount` to increment with each jump action. However, I'm not entirely certain if this method aligns with Epic's recommended practices.

Another thing worth to note is that, if you look at the other physics examples around in the engine, they explicitly process the input in `OnPhysScenePreTick`, part of the Physics Scene. I'm however not sure of the extra-implications of this and I would love to know; but at least, in this minimal example we got something working in less than 400 lines of code.

# Additional resources

Besides this post, you can also take a look at different systems from the engine that also make use of this technology:
- `UChaosVehicleMovementComponent`: Chaos simple vehicle system.
- `UMoverNetworkPhysicsLiaisonComponent`: Physics-based [Mover](https://vorixo.github.io/devtricks/mover-show/).

Finally, I strongly recommend everyone to take a look at Epic's GDC talk about Chaos, where they talk a bit about their new physics system:

<iframe width="480" height="270" src="https://www.youtube.com/embed/WPsRfZ8rxOg?start=2319" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

Thanks Epic!

# Conclusion

Today we explored together the `UNetworkPhysicsComponent` and coded together a "hello world" physics boulder.

Note that this toy example uses a very simple setup for preview purposes. But I'd like to support it following the best practices, for that reason I'll be mantaining it in my [experimental arcade vehicle sample](https://github.com/vorixo/ExperimentalArcadeVehicleSampleProject) repo. Feel free to open issues or pull request to the sample, I'll be vigilante, the main intention is to create a "hello world" example the community could learn from. And of course, along with that I'll try to keep this article updated!

As always, feel free to [contact me](https://twitter.com/vorixo) (and follow me! hehe) if you find any issues with the article or have any questions about the whole topic.

Enjoy, vori.