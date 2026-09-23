---
layout: single
title: "Networked Physics and UNetworkPhysicsComponent in Unreal Engine 5.8"
excerpt: In this post we'll build a rolling physics boulder with UNetworkPhysicsComponent, physics prediction and resimulation in Unreal Engine 5.8.
header:
  teaser: /assets/images/per-post/phys-prediction-use/thumb.jpg
last_modified_at: 2026-09-23
author: Meta
category: Videogames Development
tags:
  - Multiplayer
  - UE5
  - Physics Prediction
  - Networking
---

In our [introduction to physics prediction](https://vorixo.github.io/devtricks/phys-prediction-show/) we explored the physics prediction system bundled with Unreal Engine. Now, let's build a rolling boulder in **Unreal Engine 5.8** and do a hands-on together!

# Introduction

Predicting and correcting physics is one of the most complicated topics when it comes to network prediction. The nature of the complexity comes by the fact that if we hit a chain of physics objects on the client, we'd expect all the physics to react instantaneously, without having to wait for the server. But not only that, these physics objects should smoothly correct their position on the clients if there are incongruences with what the server expects. So you can imagine how difficult this can get with complex physics chain reactions.

Fortunately, Epic has been working on a solution for these problems bundled in their Networked Physics system. Before digging into the matter, I strongly recommend everyone to read [Epic's Networked Physics Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networked-physics-overview), which explains the different physics replication modes.

This article follows **[Networked Physics - Pawn Tutorial by MBobbo (Markus Boberg)](https://dev.epicgames.com/community/learning/tutorials/MoBq/unreal-engine-networked-physics-pawn-tutorial)**, the creator of Unreal Engine's networked physics prediction system. His tutorial is the implementation reference for this article. We follow its input flow, physics callback, history registration, input merging and decay, and physics-object handling. Credit where it's due!

**UE 5.8:** The code below adapts that implementation to our rolling boulder and adds **networked actions for jumping**, a feature introduced in 5.8. Follow the written settings below when configuring your project.
{: .notice--info}

# Making a rolling boulder

The 'rolling boulder' we are going to create may serve as a "hello world" for those that would like to get introduced to the matter, as this system can be used to make vehicles or physics controllable objects. Because... what's easier than a rolling sphere?

This is still a toy example. We'll keep the movement simple and spend our time on getting inputs into the physics simulation, recording the state we need for rewind, and replaying those inputs when the server corrects us.

So... as I always say, don't trust random articles from the internet and please, do your own research! If you think that any of the information shared in here is off, you can always [contact me on twitter](https://twitter.com/vorixo) and we can fix it together as a community effort! Now... let's get into it :D!

## The setup

In `Project Settings -> Physics`, turn on **Enable Physics Prediction**. In UE 5.8 this synchronizes the client and server physics frames, and enables the client's physics history when an actor uses Resimulation.

![Network Physics Prediction]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-show/enableit.jpg){: .align-center}

Under `Physics -> Framerate`, enable **Tick Physics Async** and choose an **Async Fixed Time Step Size**, for example `0.016666667` for 60 physics steps per second. This is the physics rate, not a requirement to render at 60 FPS!

![Physics Settings]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/physics-settings.jpg){: .align-center}

Set **Max Supported Latency Prediction** to cover the round-trip latency you intend to support, with some room for jitter. A larger history costs more memory. You do **not** need to also enable **Enable Physics History Capture** for this setup: the 5.8 settings explicitly reserve that switch for manually enabling history, including on the server.

Our pawn needs **Replicates**, **Replicate Movement**, and **Physics Replication Mode = Resimulation** so we can predict its movement locally and resimulate it after a server correction. Set these on a Blueprint child of `ARollingBallPawn` then, add a sphere as the root component, a spring arm and camera for your controls.

Other replicated physics actors can use **Predictive Interpolation**. Resimulation isn't restricted to controlled pawns, though: the UE 5.8 source still recommends it for physics objects that interact with a resimulated pawn (`EPhysicsReplicationMode` in `EngineTypes.h`). Choose their mode with those interactions in mind, rather than setting every replicated physics object in the level to Resimulation.

![Boulder Actor]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/boulderactor.jpg){: .align-center}

And to finish up the setup, let's configure the root static mesh:

![Root mesh]({{ '/' | absolute_url }}/assets/images/per-post/phys-prediction-use/rootmeshsettings.jpg){: .align-center}

The simulating sphere must be the **root component**, with collision enabled. Keep **Component Replicates** off for the mesh, but keep **Replicate Movement** on for the actor and **Replicate Physics to Autonomous Proxy** on for the mesh. The actor's movement replication carries the authoritative rigid body state; the Network Physics component carries our inputs and custom state. We need both!

**Note:** Following MBobbo's setup, raise the replicated movement quantization levels under `Replication -> Advanced`, so the physics replication system has more precise position, velocity and rotation data to work with. Set the pawn's spawn collision handling to **Always Spawn, Ignore Collisions**. Keep **Enable Async Physics Tick** off on the pawn: our physics-thread work runs through the Chaos simulation callback.
{: .notice--info}

For this example, follow MBobbo's **Iris** setup. Enable the **Iris** plugin in the editor (and restart when prompted), then add these settings to `Config/DefaultEngine.ini`:

{% highlight ini %}
[SystemSettings]
net.IsPushModelEnabled=1
net.Iris.PushModelMode=1
net.Iris.UseIrisReplication=1
net.SubObjects.DefaultUseSubObjectReplicationList=1
{% endhighlight %}

In your module's `.Build.cs`, add the dependencies used by the headers below, and enable its Iris build support. Keep any other dependencies your project already needs:

{% highlight csharp %}
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine", "PhysicsCore", "Chaos", "NetCore"
});
SetupIrisSupport(Target);
{% endhighlight %}

`SetupIrisSupport` configures compilation; the plugin and ini settings complete this example's Iris setup. Our payloads derive from `FNetworkPhysicsPayload` and describe their replicated data with `UPROPERTY`.

## Coding the physics boulder

The `UNetworkPhysicsComponent` handles networking and history for our inputs and custom states. As MBobbo explains, rigid body movement is replicated through the actor, while our custom state must contain the gameplay data needed to reproduce the simulation. Here that includes which jump request has already produced an action. The jump effect itself has its own `FNetworkPhysicsActionPayload`.

Let's put the following in `RollingBallPawn.h`. Replace `MYPROJECT_API` with your module's API macro. The generated header must remain the last include.

{% highlight c++ %}
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Chaos/SimCallbackObject.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "RollingBallPawn.generated.h"

class FRollingBallAsync;

UCLASS()
class MYPROJECT_API ARollingBallPawn : public APawn
{
	GENERATED_BODY()

public:
	ARollingBallPawn();
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void SetThrottleInput(float Value);

	UFUNCTION(BlueprintCallable)
	void SetSteeringInput(float Value);

	UFUNCTION(BlueprintCallable)
	void SetTravelDirectionInput(FRotator Value);

	UFUNCTION(BlueprintCallable)
	void Jump();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FRollingBallAsync* RollingBallAsync = nullptr;

	UPROPERTY()
	TObjectPtr<UNetworkPhysicsComponent> NetworkPhysicsComponent = nullptr;

	float ThrottleInput_External = 0.f;
	float SteeringInput_External = 0.f;
	FRotator TravelDirection_External = FRotator::ZeroRotator;
	uint32 JumpRequest_External = 0;
};

USTRUCT()
struct FNetworkBallInputs : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	UPROPERTY()
	float ThrottleInput = 0.f;

	UPROPERTY()
	float SteeringInput = 0.f;

	UPROPERTY()
	FRotator TravelDirection = FRotator::ZeroRotator;

	// Persistent request, so a press isn't lost between game and physics ticks.
	UPROPERTY()
	uint32 JumpRequest = 0;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData,
		const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override;
	virtual void MergeData(const FNetworkPhysicsPayload& FromData) override;
	virtual void DecayData(float DecayAmount) override;
	virtual bool CompareData(const FNetworkPhysicsPayload& PredictedData) const override;
};

USTRUCT()
struct FNetworkBallStates : public FNetworkPhysicsPayload
{
	GENERATED_BODY()

	// Records which request has already produced an action.
	UPROPERTY()
	uint32 LastJumpRequest = 0;

	virtual void InterpolateData(const FNetworkPhysicsPayload& MinData,
		const FNetworkPhysicsPayload& MaxData, float LerpAlpha) override;
	virtual bool CompareData(const FNetworkPhysicsPayload& PredictedData) const override;
};

// UE 5.8 addition: the jump effect is a networked physics action.
USTRUCT()
struct FBallJumpAction : public FNetworkPhysicsActionPayload
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 JumpRequest = 0;

	virtual const EActionAuthorStyle GetAuthorStyle() const override
	{
		return EActionAuthorStyle::Predicted;
	}

	virtual bool IsNearlyEqual(const FNetworkPhysicsActionPayload& Other) const override
	{
		return JumpRequest == static_cast<const FBallJumpAction&>(Other).JumpRequest;
	}
};

struct FAsyncInputRollingBall : public Chaos::FSimCallbackInput
{
	float ThrottleInput = 0.f;
	float SteeringInput = 0.f;
	FRotator TravelDirection = FRotator::ZeroRotator;
	uint32 JumpRequest = 0;

	void Reset()
	{
		ThrottleInput = 0.f;
		SteeringInput = 0.f;
		TravelDirection = FRotator::ZeroRotator;
		JumpRequest = 0;
	}
};

struct FAsyncOutputRollingBall : public Chaos::FSimCallbackOutput
{
	void Reset() {}
};

class FRollingBallAsync : public Chaos::TSimCallbackObject<FAsyncInputRollingBall,
	FAsyncOutputRollingBall, Chaos::ESimCallbackOptions::Presimulate
	| Chaos::ESimCallbackOptions::PhysicsObjectUnregister | Chaos::ESimCallbackOptions::Rewind>
	, public TNetworkPhysicsInputState_Internal<FNetworkBallInputs, FNetworkBallStates>
	, public INetworkPhysicsActionHandler_Internal
{
	friend ARollingBallPawn;

	virtual void OnPostInitialize_Internal() override;
	virtual void ProcessInputs_Internal(int32 PhysicsStep) override;
	virtual void OnPreSimulate_Internal() override;
	virtual void OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle Object) override;

	virtual void BuildInput_Internal(FNetworkBallInputs& Input) const override;
	virtual void ValidateInput_Internal(FNetworkBallInputs& Input) const override;
	virtual void ApplyInput_Internal(const FNetworkBallInputs& Input) override;
	virtual void BuildState_Internal(FNetworkBallStates& State) const override;
	virtual void ApplyState_Internal(const FNetworkBallStates& State) override;
	virtual void ApplyAction_Internal(const TInstancedStruct<FNetworkPhysicsActionPayload>& Action) override;

	Chaos::FConstPhysicsObjectHandle PhysicsObject = nullptr;
	FAsyncNetworkPhysicsComponent* NetworkPhysics_Internal = nullptr;
	float ThrottleInput_Internal = 0.f;
	float SteeringInput_Internal = 0.f;
	FRotator TravelDirection_Internal = FRotator::ZeroRotator;
	uint32 JumpRequest_Internal = 0;
	uint32 LastJumpRequest_Internal = 0;
};
{% endhighlight %}

The layout follows the reference: the pawn holds game-thread input, the payloads describe networked data, the async input/output structs carry data between threads, and `FRollingBallAsync` owns the physics-thread logic. The addition for 5.8 is `FBallJumpAction` and the `INetworkPhysicsActionHandler_Internal` interface. There are no custom serializers here, the payloads describe their networked fields with `UPROPERTY`.

Now, `RollingBallPawn.cpp`. Each part has a job: the pawn gathers input on the game thread, a marshaled snapshot crosses to the physics thread, and the simulation callback applies forces there.

{% highlight c++ %}
#include "RollingBallPawn.h"

#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Components/PrimitiveComponent.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

ARollingBallPawn::ARollingBallPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction)
	{
		NetworkPhysicsComponent = CreateDefaultSubobject<UNetworkPhysicsComponent>(TEXT("NetworkPhysicsComponent"));
		NetworkPhysicsComponent->SetNetAddressable();
		NetworkPhysicsComponent->SetIsReplicated(true);
	}
}

void ARollingBallPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!GetWorld()->IsGameWorld())
	{
		return;
	}

	UPrimitiveComponent* RootSimulatedComponent = Cast<UPrimitiveComponent>(GetRootComponent());
	FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
	if (!RootSimulatedComponent || !PhysScene || !PhysScene->GetSolver() || !NetworkPhysicsComponent)
	{
		return;
	}

	RollingBallAsync = PhysScene->GetSolver()->CreateAndRegisterSimCallbackObject_External<FRollingBallAsync>();
	// Initialize these pointers before the callback is handed to the physics thread.
	RollingBallAsync->PhysicsObject = RootSimulatedComponent->GetPhysicsObjectByName(NAME_None);
	RollingBallAsync->NetworkPhysics_Internal = NetworkPhysicsComponent->GetNetworkPhysicsComponent_Internal();
	NetworkPhysicsComponent->CreateDataHistory<FNetworkBallInputs, FNetworkBallStates>(RollingBallAsync);
	NetworkPhysicsComponent->SetActionHandler(RollingBallAsync);
	NetworkPhysicsComponent->SetCompareInputToTriggerRewind(true);
	NetworkPhysicsComponent->SetCompareStateToTriggerRewind(true);
}

void ARollingBallPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Check local control BEFORE requesting a producer buffer, as in MBobbo's tutorial.
	if (RollingBallAsync && IsLocallyControlled())
	{
		if (FAsyncInputRollingBall* AsyncInput = RollingBallAsync->GetProducerInputData_External())
		{
			AsyncInput->ThrottleInput = ThrottleInput_External;
			AsyncInput->SteeringInput = SteeringInput_External;
			AsyncInput->TravelDirection = TravelDirection_External;
			AsyncInput->JumpRequest = JumpRequest_External;
		}
	}
}

void ARollingBallPawn::SetThrottleInput(float Value)
{
	ThrottleInput_External = FMath::Clamp(Value, -1.f, 1.f);
}

void ARollingBallPawn::SetSteeringInput(float Value)
{
	SteeringInput_External = FMath::Clamp(Value, -1.f, 1.f);
}

void ARollingBallPawn::SetTravelDirectionInput(FRotator Value)
{
	TravelDirection_External = FRotator(0.0, FRotator::NormalizeAxis(Value.Yaw), 0.0);
}

void ARollingBallPawn::Jump()
{
	++JumpRequest_External;
}

void ARollingBallPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RollingBallAsync)
	{
		// Stop the network callback before releasing its input/state/action handler.
		if (NetworkPhysicsComponent)
		{
			NetworkPhysicsComponent->SetActionHandler(nullptr);
			NetworkPhysicsComponent->RemoveDataHistory();
			NetworkPhysicsComponent->UnregisterComponent();
		}
		if (FPhysScene* PhysScene = GetWorld()->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{
				Solver->UnregisterAndFreeSimCallbackObject_External(RollingBallAsync);
			}
		}
		RollingBallAsync = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void FNetworkBallInputs::InterpolateData(const FNetworkPhysicsPayload& MinData,
	const FNetworkPhysicsPayload& MaxData, float LerpAlpha)
{
	const FNetworkBallInputs& Min = static_cast<const FNetworkBallInputs&>(MinData);
	const FNetworkBallInputs& Max = static_cast<const FNetworkBallInputs&>(MaxData);
	ThrottleInput = FMath::Lerp(Min.ThrottleInput, Max.ThrottleInput, LerpAlpha);
	SteeringInput = FMath::Lerp(Min.SteeringInput, Max.SteeringInput, LerpAlpha);
	TravelDirection = FMath::Lerp(Min.TravelDirection, Max.TravelDirection, LerpAlpha);
	JumpRequest = LerpAlpha < 1.f ? Min.JumpRequest : Max.JumpRequest;
}

void FNetworkBallInputs::MergeData(const FNetworkPhysicsPayload& FromData)
{
	const FNetworkBallInputs& From = static_cast<const FNetworkBallInputs&>(FromData);
	ThrottleInput = FMath::Max(ThrottleInput, From.ThrottleInput);
	SteeringInput = FMath::Max(SteeringInput, From.SteeringInput);
	// Retain the newer frame's direction and discrete request identifier.
}

void FNetworkBallInputs::DecayData(float DecayAmount)
{
	ThrottleInput *= 1.f - DecayAmount;
	SteeringInput *= 1.f - DecayAmount;
}

bool FNetworkBallInputs::CompareData(const FNetworkPhysicsPayload& PredictedData) const
{
	const FNetworkBallInputs& Predicted = static_cast<const FNetworkBallInputs&>(PredictedData);
	return ThrottleInput == Predicted.ThrottleInput
		&& SteeringInput == Predicted.SteeringInput
		&& TravelDirection == Predicted.TravelDirection
		&& JumpRequest == Predicted.JumpRequest;
}

void FNetworkBallStates::InterpolateData(const FNetworkPhysicsPayload& MinData,
	const FNetworkPhysicsPayload& MaxData, float LerpAlpha)
{
	const FNetworkBallStates& Min = static_cast<const FNetworkBallStates&>(MinData);
	const FNetworkBallStates& Max = static_cast<const FNetworkBallStates&>(MaxData);
	LastJumpRequest = LerpAlpha < 1.f ? Min.LastJumpRequest : Max.LastJumpRequest;
}

bool FNetworkBallStates::CompareData(const FNetworkPhysicsPayload& PredictedData) const
{
	return LastJumpRequest == static_cast<const FNetworkBallStates&>(PredictedData).LastJumpRequest;
}

void FRollingBallAsync::OnPostInitialize_Internal()
{
	if (PhysicsObject)
	{
		Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
		{
			ParticleHandle->SetSleepType(Chaos::ESleepType::NeverSleep);
		}
	}
}

void FRollingBallAsync::ProcessInputs_Internal(int32 PhysicsStep)
{
	const FAsyncInputRollingBall* AsyncInput = GetConsumerInput_Internal();
	if (!AsyncInput)
	{
		return;
	}
	Chaos::FPhysicsSolverBase* BaseSolver = GetSolver();
	if (!BaseSolver || BaseSolver->IsResimming())
	{
		// Historical/networked controls are supplied by ApplyInput_Internal.
		return;
	}
	ThrottleInput_Internal = AsyncInput->ThrottleInput;
	SteeringInput_Internal = AsyncInput->SteeringInput;
	TravelDirection_Internal = AsyncInput->TravelDirection;
	JumpRequest_Internal = AsyncInput->JumpRequest;
}

void FRollingBallAsync::OnPreSimulate_Internal()
{
	if (!PhysicsObject)
	{
		return;
	}
	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject);
	if (!ParticleHandle)
	{
		return;
	}

	const FVector Forward = TravelDirection_Internal.Vector();
	const FVector Right = FRotationMatrix(TravelDirection_Internal).GetUnitAxis(EAxis::Y);
	const FVector Force = (ThrottleInput_Internal * Forward + SteeringInput_Internal * Right) * 80000.f;
	if (Force.SizeSquared() > UE_SMALL_NUMBER)
	{
		ParticleHandle->AddForce(Force, true);
	}

	if (NetworkPhysics_Internal && JumpRequest_Internal != LastJumpRequest_Internal)
	{
		FBallJumpAction Action;
		Action.JumpRequest = JumpRequest_Internal;
		// Author on both client and server, including during resimulation.
		const int32 NextFrame = static_cast<Chaos::FPhysicsSolver*>(GetSolver())->GetCurrentFrame() + 1;
		NetworkPhysics_Internal->EnqueueScheduledActionAtFrame_Internal(Action, uint32(0), NextFrame, true);
		LastJumpRequest_Internal = JumpRequest_Internal;
	}
}

void FRollingBallAsync::ApplyAction_Internal(const TInstancedStruct<FNetworkPhysicsActionPayload>& Action)
{
	if (!PhysicsObject || !Action.GetPtr<FBallJumpAction>())
	{
		return;
	}
	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	if (Chaos::FPBDRigidParticleHandle* ParticleHandle = Interface.GetRigidParticle(PhysicsObject))
	{
		// The action, rather than the request, applies a 500 cm/s upward velocity change.
		ParticleHandle->SetLinearImpulseVelocity(
			ParticleHandle->LinearImpulseVelocity() + FVector(0, 0, 500), true);
	}
}

void FRollingBallAsync::OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle Object)
{
	if (PhysicsObject == Object)
	{
		PhysicsObject = nullptr;
	}
}

void FRollingBallAsync::BuildInput_Internal(FNetworkBallInputs& Input) const
{
	Input.ThrottleInput = ThrottleInput_Internal;
	Input.SteeringInput = SteeringInput_Internal;
	Input.TravelDirection = TravelDirection_Internal;
	Input.JumpRequest = JumpRequest_Internal;
}

void FRollingBallAsync::ValidateInput_Internal(FNetworkBallInputs& Input) const
{
	Input.ThrottleInput = FMath::Clamp(Input.ThrottleInput, -1.f, 1.f);
	Input.SteeringInput = FMath::Clamp(Input.SteeringInput, -1.f, 1.f);
	Input.TravelDirection = FRotator(0.0, FRotator::NormalizeAxis(Input.TravelDirection.Yaw), 0.0);
}

void FRollingBallAsync::ApplyInput_Internal(const FNetworkBallInputs& Input)
{
	ThrottleInput_Internal = Input.ThrottleInput;
	SteeringInput_Internal = Input.SteeringInput;
	TravelDirection_Internal = Input.TravelDirection;
	JumpRequest_Internal = Input.JumpRequest;
}

void FRollingBallAsync::BuildState_Internal(FNetworkBallStates& State) const
{
	State.LastJumpRequest = LastJumpRequest_Internal;
}

void FRollingBallAsync::ApplyState_Internal(const FNetworkBallStates& State)
{
	LastJumpRequest_Internal = State.LastJumpRequest;
}
{% endhighlight %}

One detail that's easy to miss: `Tick` checks `IsLocallyControlled()` **before** requesting a producer buffer, just like MBobbo's implementation. Remote pawns get their input through `ApplyInput_Internal`. During resimulation, the autonomous pawn also uses that recorded input, so `ProcessInputs_Internal` leaves its async input alone.

**Note:** This is still a "hello world". It clamps continuous input, but doesn't implement grounded checks, jump cooldowns or gameplay validation of jump requests.
{: .notice--info}

### Connecting the controls

Set your Blueprint child as the GameMode's Default Pawn Class, so the server spawns and possesses one pawn for each player. The owning client needs possession for its input RPCs to reach the server.

From your locally controlled pawn's input events:

- Call `SetThrottleInput` and `SetSteeringInput` with values in `[-1, 1]`. With Enhanced Input, also send zero on `Completed` / `Canceled`, so releasing a key doesn't leave the last value held down.
- Call `SetTravelDirectionInput` with the controller or camera rotation. This example uses yaw only, so looking up doesn't make the ball fly.
- Call `Jump` once on the jump action's `Started` event, not every frame while it is held.

The camera and input mapping are yours to choose. Keep the camera on a spring arm or another setup that doesn't inherit the rolling mesh's rotation unless that's the ride you want, hehe.

If everything is connected, we're aiming for the behaviour shown in this video:

<iframe width="480" height="270" src="https://www.youtube.com/embed/kFtZqNlcg3U" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

The video shows the boulder with latency applied to the server and client. At the end I display the difference between jump inputs being sent through the `NetworkPhysicsComponent`, and jumps performed only on the client; you can see how the latter get corrected, making the local sphere snap back to the ground.

### Implementation details

Now that we are all on the same page, let's connect this back to MBobbo's tutorial.

We create the physics callback and register `CreateDataHistory<FNetworkBallInputs, FNetworkBallStates>` in `PostInitializeComponents`, as he does. The `_External` variables belong to the game thread; the `_Internal` variables belong to the physics thread. The async input struct carries the local player's controls between them.

We also use his callback flags: `Presimulate`, `PhysicsObjectUnregister` and `Rewind`, with `ProcessInputs_Internal` processing the marshaled input. UE 5.8.2 supports this flow; its native `ProcessInputsInternal` flag is optional for this setup.

The flow remains: apply networked or historical input, process fresh local input when appropriate, build the input history, then simulate. `OnPreSimulate_Internal` applies forces through the rigid particle handle.

Our added travel direction and jump request have their own handling: retain the newer direction when merging, and never interpolate or decay a request identifier as if it were a movement axis.

UE 5.8 records custom state after the solve by default and stamps it for the beginning of the next physics frame. We let the engine manage that timing.

### Jumping with UE 5.8 networked actions

Here's the new part! UE 5.8 adds actions alongside inputs and states. We use `FBallJumpAction` for the jump effect and register our callback with `SetActionHandler`. The [UE 5.8 release notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes) describe actions being predicted, confirmed by the server and applied during resimulation.

The jump follows three steps:

1. `Jump()` advances a request identifier on the game thread. That request travels in the normal input payload, so both the client and server can produce the action from the same input stream.
2. In `OnPreSimulate_Internal`, a new request produces `FBallJumpAction` through `EnqueueScheduledActionAtFrame_Internal`. Its author style is **Predicted**: clients predict it, and the server independently authors the authoritative action. `LastJumpRequest` is custom rewind state so replay can regenerate the action from historical input.
3. `ApplyAction_Internal` applies the upward velocity change. The movement callback doesn't also apply a jump impulse; doing both would jump twice!

**Timing matters:** in 5.8.2, action dispatch happens before our `OnPreSimulate_Internal` callback. We explicitly schedule the action for **the next physics frame**. This also covers the first action, before the component has an initialized action history. The scheduling API converts the local frame to the server timeline; we don't invoke the handler ourselves or manually overwrite `ServerFrame`.

We must also keep authoring actions during resimulation. Only fresh game-thread input is skipped during replay; the simulation still needs to regenerate its predictions. Server-confirmed actions replace matching predictions and are replayed from action history.

While testing, `np2.Resim.NetworkedActions.EnableDebugLogs=1` lets you follow action prediction, server authoring and confirmation in the logs.
{: .notice--info}

With **Predicted** actions, enqueuing only from the client's Blueprint jump event would leave the server without its own action. The server must independently produce that action from the request. **Proposed** actions can send a client's candidate to the server, but even then the server must author a matching action and compare it before accepting the proposal. Neither style automatically grants a client permission to jump. Our normal input path supplies the request, and the physics simulation authors the action on both machines.

This toy example allows an air jump for each observed new request. Multiple presses folded into one input still produce one action. Grounded checks, cooldowns and other rules belong in the simulation before authoring the action, with any persistent gameplay state included in the rewind state.

The 5.8 additions were checked in `Engine/Source/Runtime/Engine/Public/Physics/NetworkPhysicsComponent.h` and its implementation under `Private/PhysicsEngine`. For the base implementation and its settings, keep [MBobbo's original tutorial](https://dev.epicgames.com/community/learning/tutorials/MoBq/unreal-engine-networked-physics-pawn-tutorial) as the reference.

# Additional resources

Besides this post and MBobbo's tutorial, you can also take a look at systems from the engine that use this technology:

- `UChaosVehicleMovementComponent`: Chaos simple vehicle system. It demonstrates traits-based registration, another approach you'll encounter in engine code.
- `UChaosMoverBackendComponent`: the networked physics backend in the **ChaosMover** plugin. For more background, see the physics-based [Mover](https://vorixo.github.io/devtricks/mover-show/) article.

Finally, I strongly recommend everyone to take a look at Epic's GDC talk about Chaos, where they talk a bit about their physics system:

<iframe width="480" height="270" src="https://www.youtube.com/embed/WPsRfZ8rxOg?start=2319" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

Thanks Epic!

# Conclusion

Today we explored together the `UNetworkPhysicsComponent` and built our "hello world" physics boulder in UE 5.8, following MBobbo's implementation and adding networked actions for jumping. The ball is still simple; the important part is understanding which input drives each physics frame, and which gameplay state needs to come back when we rewind.

The [experimental arcade vehicle sample](https://github.com/vorixo/ExperimentalArcadeVehicleSampleProject) repo is an additional resource. Check its engine version before copying code. Feel free to open issues or pull requests there, the main intention is to create a "hello world" example the community can learn from.

As always, feel free to [contact me](https://twitter.com/vorixo) (and follow me! hehe) if you find any issues with the article or have any questions about the whole topic.

Enjoy, vori.
