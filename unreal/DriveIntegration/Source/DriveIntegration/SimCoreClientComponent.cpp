#include "SimCoreClientComponent.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "SimCoreMapPackage.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreClient, Log, All);

namespace
{
template <typename CallbackType>
void ApplyOnGameThread(CallbackType&& Callback)
{
	if (IsInGameThread())
	{
		Callback();
		return;
	}

	AsyncTask(ENamedThreads::GameThread, Forward<CallbackType>(Callback));
}

const TCHAR* VehicleClassText(
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	switch (VehicleClass)
	{
	case SimCoreProtocol::ERuntimeVehicleClass::Sedan: return TEXT("SEDAN");
	case SimCoreProtocol::ERuntimeVehicleClass::Compact: return TEXT("COMPACT");
	case SimCoreProtocol::ERuntimeVehicleClass::Truck: return TEXT("TRUCK");
	case SimCoreProtocol::ERuntimeVehicleClass::Motorcycle: return TEXT("MOTORCYCLE");
	default: return TEXT("WAIT");
	}
}
}

USimCoreClientComponent::USimCoreClientComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CapsuleMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	RuntimePedestrianMesh = CapsuleMesh.Object;
}

void USimCoreClientComponent::BeginPlay()
{
	Super::BeginPlay();
	PlaySessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Connect();
	PerformanceCapture.StartFromCommandLine(GetWorld(), MapPackageChecksum);
}

void USimCoreClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PerformanceCapture.Stop();
	if (GEngine != nullptr)
	{
		GEngine->RemoveOnScreenDebugMessage(DebugHudMessageKey());
	}
	Disconnect();
	Super::EndPlay(EndPlayReason);
}

void USimCoreClientComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickConnection();
	TickControlTransmission(DeltaTime);
	TickRuntimeProxyActors(DeltaTime);
	TickTrafficSignals();
	TickStructureDamage(DeltaTime);
	TickTelemetry(DeltaTime);
	TickDebugHud(DeltaTime);
}

void USimCoreClientComponent::Connect()
{
	if (ConnectionState == ESimCoreConnectionState::Connecting
		|| ConnectionState == ESimCoreConnectionState::Handshaking
		|| ConnectionState == ESimCoreConnectionState::Connected)
	{
		return;
	}
	if (!PrepareMapPackageIdentity())
	{
		bAutoReconnectEnabled = false;
		SetConnectionState(ESimCoreConnectionState::Incompatible);
		return;
	}

	bAutoReconnectEnabled = true;
	StartConnectionAttempt();
}

void USimCoreClientComponent::Disconnect()
{
	bAutoReconnectEnabled = false;
	SetConnectionState(ESimCoreConnectionState::Stopping);
	++SocketGeneration;
	ReleaseSocket(true, TEXT("Unreal client shutdown"));
	IncomingMessage.Reset();
	bDiscardIncomingMessage = false;
	ResetReceivedState();
	ResetTelemetry();
	SetConnectionState(ESimCoreConnectionState::Disconnected);
}

bool USimCoreClientComponent::IsConnected() const
{
	return ConnectionState == ESimCoreConnectionState::Connected
		&& Socket.IsValid()
		&& Socket->IsConnected();
}

void USimCoreClientComponent::SetControl(
	float Throttle,
	float Brake,
	float Steering,
	bool bSideBrake,
	SimCoreProtocol::EVehicleGear Gear)
{
	PendingControl.Throttle = ApplyInputDeadzone(FMath::Clamp(Throttle, 0.0f, 1.0f));
	PendingControl.Brake = ApplyInputDeadzone(FMath::Clamp(Brake, 0.0f, 1.0f));
	PendingControl.Steering = ApplyInputDeadzone(FMath::Clamp(Steering, -1.0f, 1.0f));
	PendingControl.bHandbrake = bSideBrake;
	PendingControl.Gear = Gear;
	bControlDirty = HasMeaningfulControlChange();
}

bool USimCoreClientComponent::GetLatestState(
	SimCoreProtocol::FVehicleState& OutState,
	float& OutStateAgeSeconds) const
{
	if (!bHasState) return false;
	OutState = LatestState;
	OutStateAgeSeconds = static_cast<float>(FMath::Max(
		0.0,
		FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds));
	return true;
}

void USimCoreClientComponent::StartConnectionAttempt()
{
	// A server-side MapPackage hot reload closes the old checksum lifecycle.
	// Re-read the editor's manifest before every reconnect so the next Reset and
	// control frames use the newly committed collision identity automatically.
	const FString PreviousMapPackageChecksum = MapPackageChecksum;
	if (!PrepareMapPackageIdentity())
	{
		// Preserve the last verified identity across a transient manifest-last
		// commit window. The eventual successful retry must still detect A -> B
		// and mint a fresh PlaySessionId.
		MapPackageChecksum = PreviousMapPackageChecksum;
		UE_LOG(
			LogSimCoreClient,
			Warning,
			TEXT("MapPackage is not ready for reconnect; validation will retry"));
		SetConnectionState(ESimCoreConnectionState::Disconnected);
		ScheduleReconnect();
		return;
	}
	if (!PreviousMapPackageChecksum.IsEmpty()
		&& PreviousMapPackageChecksum != MapPackageChecksum)
	{
		PlaySessionId = FGuid::NewGuid().ToString(
			EGuidFormats::DigitsWithHyphensLower);
		UE_LOG(
			LogSimCoreClient,
			Log,
			TEXT("MapPackage identity changed %s -> %s; starting fresh play session %s"),
			*PreviousMapPackageChecksum,
			*MapPackageChecksum,
			*PlaySessionId);
	}

	++SocketGeneration;
	const uint64 Generation = SocketGeneration;
	ReleaseSocket(true, TEXT("Replacing SimCore connection"));
	ResetConnectionSession();
	SetConnectionState(ESimCoreConnectionState::Connecting);

	FWebSocketsModule& Module =
		FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Socket = Module.CreateWebSocket(ServerUrl);
	if (!Socket.IsValid())
	{
		UE_LOG(LogSimCoreClient, Error, TEXT("Failed to create WebSocket for %s"), *ServerUrl);
		ScheduleReconnect();
		return;
	}

	BindSocketDelegates(Generation);
	Socket->Connect();
}

bool USimCoreClientComponent::SelectVehicleClass(
	const SimCoreProtocol::ERuntimeVehicleClass VehicleClass)
{
	if (VehicleClass < SimCoreProtocol::ERuntimeVehicleClass::Sedan
		|| VehicleClass > SimCoreProtocol::ERuntimeVehicleClass::Motorcycle
		|| VehicleClass == SelectedVehicleClass)
	{
		return false;
	}

	SelectedVehicleClass = VehicleClass;
	PlaySessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	PendingControl = {};
	bAutoReconnectEnabled = true;
	StartConnectionAttempt();
	return true;
}

FString USimCoreClientComponent::GetConnectionStatusText() const
{
	return BuildDebugStatusText();
}

bool USimCoreClientComponent::PrepareMapPackageIdentity()
{
	SimCoreMapPackage::FManifest Manifest;
	FString Error;
	if (!SimCoreMapPackage::LoadAndVerifyManifest(
		MapPackageDirectory,
		Manifest,
		Error))
	{
		MapPackageChecksum.Reset();
		UE_LOG(
			LogSimCoreClient,
			Error,
			TEXT("MapPackage validation failed for %s: %s"),
			*MapPackageDirectory,
			*Error);
		return false;
	}

	MapPackageDirectory = Manifest.PackageDirectory;
	MapPackageChecksum = Manifest.CollisionChecksum;
	UE_LOG(
		LogSimCoreClient,
		Log,
		TEXT("Verified MapPackage id=%s checksum=%s directory=%s"),
		*Manifest.MapId,
		*MapPackageChecksum,
		*MapPackageDirectory);
	return true;
}

void USimCoreClientComponent::TickConnection()
{
	if (bAutoReconnectEnabled
		&& ConnectionState == ESimCoreConnectionState::WaitingToReconnect
		&& FPlatformTime::Seconds() >= NextReconnectTimeSeconds)
	{
		StartConnectionAttempt();
	}
}

void USimCoreClientComponent::TickControlTransmission(float DeltaTime)
{
	SendAccumulator += DeltaTime;
	if (ShouldSendControl())
	{
		SendControl();
	}
}

void USimCoreClientComponent::TickTelemetry(float DeltaTime)
{
	TelemetryAccumulator += DeltaTime;
	if (TelemetryAccumulator < 1.0f) return;

	const double LocalStateAgeMs = bHasState
		? FMath::Max(0.0, FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds) * 1000.0
		: -1.0;
	const double StateRateHz = ReceivedStateCount
		/ FMath::Max(static_cast<double>(TelemetryAccumulator), 0.001);
	LastTelemetryStateRateHz = static_cast<float>(StateRateHz);
	LastTelemetryMaxGapMs = MaxStateIntervalSeconds * 1000.0;
	LastTelemetryMissingStateSequenceCount = MissingStateSequenceCount;
	LastTelemetryDroppedStateCount = DroppedOutOfOrderStateCount;
	UE_LOG(
		LogSimCoreClient,
		Log,
		TEXT("State telemetry: rate=%.1fHz sequence=%llu server_age=%.1fms local_age=%.1fms max_gap=%.1fms missing=%llu dropped_old=%u"),
		StateRateHz,
		static_cast<unsigned long long>(LatestState.Sequence),
		LatestStateWallAgeMs,
		LocalStateAgeMs,
		MaxStateIntervalSeconds * 1000.0,
		static_cast<unsigned long long>(MissingStateSequenceCount),
		DroppedOutOfOrderStateCount);

	TelemetryAccumulator = FMath::Fmod(TelemetryAccumulator, 1.0f);
	ReceivedStateCount = 0;
	MissingStateSequenceCount = 0;
	DroppedOutOfOrderStateCount = 0;
	MaxStateIntervalSeconds = 0.0;
}

void USimCoreClientComponent::TickDebugHud(float DeltaTime)
{
	if (!bShowDebugHud || GEngine == nullptr)
	{
		return;
	}
	DebugHudAccumulator += DeltaTime;
	const float RefreshInterval = 1.0f / FMath::Max(DebugHudRefreshHz, 1.0f);
	const SimCoreClientDiagnostics::FHealthDisplay HealthDisplay = EvaluateHealthDisplay();
	// Safety transitions, including a 100ms stale deadline, must not wait for
	// the lower-frequency numeric telemetry refresh.
	if (DebugHudAccumulator < RefreshInterval
		&& LastDebugHudHealthStatus == HealthDisplay.Status)
	{
		return;
	}
	DebugHudAccumulator = FMath::Fmod(DebugHudAccumulator, RefreshInterval);
	LastDebugHudHealthStatus = HealthDisplay.Status;

	const FColor Color = ConnectionState == ESimCoreConnectionState::Incompatible
		? FColor(255, 80, 80) : HealthDisplay.Color;
	GEngine->AddOnScreenDebugMessage(
		DebugHudMessageKey(),
		RefreshInterval * 1.5f,
		Color,
		BuildDebugStatusText(&HealthDisplay),
		false);
}

SimCoreClientDiagnostics::FHealthDisplay USimCoreClientComponent::EvaluateHealthDisplay() const
{
	return SimCoreClientDiagnostics::EvaluateServerHealth(
		GetHealthSnapshotForDisplay(),
		IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete,
		HasHealthSnapshotForDisplay(),
		GetHealthSnapshotAgeSeconds(),
		HealthStateStaleTimeoutSeconds);
}

const SimCoreProtocol::FServerHealth& USimCoreClientComponent::GetHealthSnapshotForDisplay() const
{
	return GlobalEstopHealthCache.HasEstopHealth()
		? GlobalEstopHealthCache.GetHealth() : LatestState.ServerHealth;
}

bool USimCoreClientComponent::HasHealthSnapshotForDisplay() const
{
	return GlobalEstopHealthCache.HasEstopHealth()
		|| (bHasState && GlobalEstopHealthCache.CanUsePlaySnapshot(LatestState.Sequence));
}

double USimCoreClientComponent::GetHealthSnapshotAgeSeconds() const
{
	if (GlobalEstopHealthCache.HasEstopHealth())
	{
		return FMath::Max(0.0,
			FPlatformTime::Seconds() - GlobalEstopHealthCache.GetReceiveTimeSeconds());
	}
	return bHasState && GlobalEstopHealthCache.CanUsePlaySnapshot(LatestState.Sequence)
		? FMath::Max(0.0, FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds) : -1.0;
}

FString USimCoreClientComponent::BuildDebugStatusText(
	const SimCoreClientDiagnostics::FHealthDisplay* DisplayOverride) const
{
	const TCHAR* StateText = TEXT("Disconnected");
	switch (ConnectionState)
	{
	case ESimCoreConnectionState::Connecting: StateText = TEXT("Connecting"); break;
	case ESimCoreConnectionState::Handshaking: StateText = TEXT("Handshaking"); break;
	case ESimCoreConnectionState::Connected: StateText = TEXT("Connected"); break;
	case ESimCoreConnectionState::Incompatible: StateText = TEXT("Incompatible"); break;
	case ESimCoreConnectionState::WaitingToReconnect: StateText = TEXT("Reconnecting"); break;
	case ESimCoreConnectionState::Stopping: StateText = TEXT("Stopping"); break;
	case ESimCoreConnectionState::Disconnected:
	default: break;
	}
	const double LocalAgeMs = bHasState
		? FMath::Max(0.0, FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds) * 1000.0
		: -1.0;
	const FString ShortChecksum = MapPackageChecksum.Len() > 12
		? MapPackageChecksum.Right(12)
		: MapPackageChecksum;
	const SimCoreClientDiagnostics::FHealthDisplay HealthDisplay = DisplayOverride
		? *DisplayOverride : EvaluateHealthDisplay();
	const SimCoreProtocol::FServerHealth& Health = GetHealthSnapshotForDisplay();
	const double HealthLocalAgeMs = HasHealthSnapshotForDisplay()
		? GetHealthSnapshotAgeSeconds() * 1000.0 : -1.0;
	const FString CommandAgeText = HealthDisplay.bAuthoritative && Health.bHasControlCommand
		? FString::Printf(TEXT("%.1f ms"), static_cast<double>(Health.LastCommandAgeNs) / 1.0e6)
		: TEXT("n/a");
	const FString OverrunText = HealthDisplay.bAuthoritative
		? FString::Printf(TEXT("%u"), Health.TickOverrunCount) : TEXT("n/a");
	const double VehicleStateAgeSeconds = bHasState
		? FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds : -1.0;
	const bool bVehicleStateDisplayable = HealthDisplay.bAuthoritative
		&& IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete
		&& bHasState && GlobalEstopHealthCache.CanUsePlaySnapshot(LatestState.Sequence)
		&& FMath::IsFinite(VehicleStateAgeSeconds) && VehicleStateAgeSeconds >= 0.0
		&& FMath::IsFinite(HealthStateStaleTimeoutSeconds) && HealthStateStaleTimeoutSeconds > 0.0f
		&& VehicleStateAgeSeconds <= HealthStateStaleTimeoutSeconds
		&& FMath::IsFinite(LatestState.SpeedMps)
		&& FMath::IsFinite(LatestState.SteeringAngleRad)
		&& FMath::IsFinite(LatestState.YawRateRad);
	const TCHAR* GearText = TEXT("--");
	if (bVehicleStateDisplayable)
	{
		switch (LatestState.Gear)
		{
		case SimCoreProtocol::EVehicleGear::Neutral: GearText = TEXT("N"); break;
		case SimCoreProtocol::EVehicleGear::Drive: GearText = TEXT("D"); break;
		case SimCoreProtocol::EVehicleGear::Reverse: GearText = TEXT("R"); break;
		default: break;
		}
	}
	const FString VehicleStateText = bVehicleStateDisplayable && FCString::Strcmp(GearText, TEXT("--")) != 0
		? FString::Printf(TEXT("speed=%.1f km/h (%.1f m/s) gear=%s steering=%.1f deg yaw=%.1f deg/s"),
			LatestState.SpeedMps * 3.6f, LatestState.SpeedMps, GearText,
			FMath::RadiansToDegrees(LatestState.SteeringAngleRad),
			FMath::RadiansToDegrees(LatestState.YawRateRad))
		: TEXT("speed=-- km/h (-- m/s) gear=-- steering=-- deg yaw=-- deg/s");
	const SimCoreProtocol::ERuntimeVehicleClass DisplayClass =
		bVehicleStateDisplayable
			&& LatestState.RuntimeVehicleClass != SimCoreProtocol::ERuntimeVehicleClass::Unspecified
		? LatestState.RuntimeVehicleClass : SelectedVehicleClass;
	return FString::Printf(
		TEXT("SimCore %s | hello=%s map=%s controlTx=%s\n")
		TEXT("vehicle %s | class=%s [1 sedan / 2 compact / 3 truck / 4 motorcycle]\n")
		TEXT("serverHealth=%s%s | healthLocal=%.1f ms commandAge@server=%s overruns=%s\n")
		TEXT("reason: %s\n")
		TEXT("network state rate=%.1f Hz seq=%llu local=%.1f ms server=%.1f ms gap=%.1f ms missing=%llu old=%u\n")
		TEXT("map ...%s entity=%d\n%s"),
		StateText,
		bProtocolHandshakeComplete ? TEXT("ok") : TEXT("wait"),
		bMapHandshakeComplete ? TEXT("ok") : TEXT("wait"),
		IsConnected() && bProtocolHandshakeComplete && bMapHandshakeComplete ? TEXT("ready") : TEXT("blocked"),
		*VehicleStateText,
		VehicleClassText(DisplayClass),
		*HealthDisplay.Status,
		GlobalEstopHealthCache.HasEstopHealth() ? TEXT(" (global)") : TEXT(""),
		HealthLocalAgeMs,
		*CommandAgeText,
		*OverrunText,
		*HealthDisplay.Reason,
		LastTelemetryStateRateHz,
		static_cast<unsigned long long>(LatestState.Sequence),
		LocalAgeMs,
		LatestStateWallAgeMs,
		LastTelemetryMaxGapMs,
		static_cast<unsigned long long>(LastTelemetryMissingStateSequenceCount),
		LastTelemetryDroppedStateCount,
		*ShortChecksum,
		ControlledEntityId,
		*BuildTrafficSignalStatusText());
}

uint64 USimCoreClientComponent::DebugHudMessageKey() const
{
	return 0x53494D4300000000ULL | static_cast<uint64>(GetUniqueID());
}

void USimCoreClientComponent::ResetConnectionSession()
{
	IncomingMessage.Reset();
	bDiscardIncomingMessage = false;
	ResetReceivedState();
	ResetTelemetry();
	ResetControlSession();
	bMapHandshakeComplete = false;
	bProtocolHandshakeComplete = false;
}

void USimCoreClientComponent::ResetReceivedState()
{
	DestroyRuntimeProxyActors();
	DestroyTrafficSignalActors();
	DestroyStructureDamageActors();
	bTrafficSnapshotAccepted = false;
	GlobalEstopHealthCache.Reset();
	LatestState = {};
	LatestStateReceiveTimeSeconds = 0.0;
	bHasState = false;
}

void USimCoreClientComponent::ResetControlSession()
{
	OutgoingSequence = 1;
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	LastSentControl = {};
	bHasSentControl = false;
	bControlDirty = true;
	SendAccumulator = 0.0f;
}

void USimCoreClientComponent::ResetTelemetry()
{
	LastStateArrivalTimeSeconds = 0.0;
	MaxStateIntervalSeconds = 0.0;
	LatestStateWallAgeMs = 0.0;
	TelemetryAccumulator = 0.0f;
	ReceivedStateCount = 0;
	MissingStateSequenceCount = 0;
	DroppedOutOfOrderStateCount = 0;
	LastTelemetryStateRateHz = 0.0f;
	LastTelemetryMaxGapMs = 0.0;
	LastTelemetryMissingStateSequenceCount = 0;
	LastTelemetryDroppedStateCount = 0;
	DebugHudAccumulator = 0.0f;
}

void USimCoreClientComponent::ScheduleReconnect()
{
	IncomingMessage.Reset();
	bDiscardIncomingMessage = false;
	DestroyRuntimeProxyActors();
	GlobalEstopHealthCache.Reset();
	if (!bAutoReconnectEnabled)
	{
		SetConnectionState(ESimCoreConnectionState::Disconnected);
		return;
	}

	if (ConnectionState != ESimCoreConnectionState::WaitingToReconnect)
	{
		NextReconnectTimeSeconds = FPlatformTime::Seconds() + ReconnectDelaySeconds;
	}
	SetConnectionState(ESimCoreConnectionState::WaitingToReconnect);
}

void USimCoreClientComponent::SetConnectionState(ESimCoreConnectionState NewState)
{
	ConnectionState = NewState;
}

bool USimCoreClientComponent::IsCurrentSocketGeneration(uint64 Generation) const
{
	return Generation == SocketGeneration;
}

void USimCoreClientComponent::BindSocketDelegates(uint64 Generation)
{
	if (!Socket.IsValid()) return;

	const TWeakObjectPtr<USimCoreClientComponent> WeakThis(this);
	ConnectedDelegateHandle = Socket->OnConnected().AddLambda([WeakThis, Generation]()
	{
		auto Apply = [WeakThis, Generation]()
		{
			if (USimCoreClientComponent* Self = WeakThis.Get())
			{
				Self->ApplyConnected(Generation);
			}
		};
		ApplyOnGameThread(MoveTemp(Apply));
	});
	ConnectionErrorDelegateHandle = Socket->OnConnectionError().AddLambda(
		[WeakThis, Generation](const FString& Error)
		{
			FString ErrorCopy = Error;
			auto Apply = [WeakThis, Generation, ErrorCopy = MoveTemp(ErrorCopy)]()
			{
				if (USimCoreClientComponent* Self = WeakThis.Get())
				{
					Self->ApplyConnectionError(Generation, ErrorCopy);
				}
			};
			ApplyOnGameThread(MoveTemp(Apply));
		});
	ClosedDelegateHandle = Socket->OnClosed().AddLambda(
		[WeakThis, Generation](int32 StatusCode, const FString& Reason, bool bWasClean)
		{
			FString ReasonCopy = Reason;
			auto Apply = [
				WeakThis,
				Generation,
				StatusCode,
				ReasonCopy = MoveTemp(ReasonCopy),
				bWasClean]()
			{
				if (USimCoreClientComponent* Self = WeakThis.Get())
				{
					Self->ApplyClosed(Generation, StatusCode, ReasonCopy, bWasClean);
				}
			};
			ApplyOnGameThread(MoveTemp(Apply));
		});
	// OnRawMessage's BytesRemaining counts bytes in the current WS frame, not
	// the complete message. Only the binary event carries the true message FIN.
	BinaryMessageDelegateHandle = Socket->OnBinaryMessage().AddLambda(
		[WeakThis, Generation](const void* Data, SIZE_T Size, bool bIsLastFragment)
		{
			const bool bFragmentTooLarge = Size > static_cast<SIZE_T>(MAX_int32)
				|| Size > static_cast<SIZE_T>(MaxIncomingMessageBytes)
				|| (Size > 0 && Data == nullptr);
			TArray<uint8> Fragment;
			if (!bFragmentTooLarge && Size > 0)
			{
				Fragment.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
			}

			auto Apply = [
				WeakThis,
				Generation,
				Fragment = MoveTemp(Fragment),
				bIsLastFragment,
				bFragmentTooLarge]() mutable
			{
				if (USimCoreClientComponent* Self = WeakThis.Get())
				{
					Self->ApplyBinaryMessage(
						Generation,
						MoveTemp(Fragment),
						bIsLastFragment,
						bFragmentTooLarge);
				}
			};
			ApplyOnGameThread(MoveTemp(Apply));
		});
}

void USimCoreClientComponent::UnbindSocketDelegates()
{
	if (Socket.IsValid())
	{
		if (ConnectedDelegateHandle.IsValid())
		{
			Socket->OnConnected().Remove(ConnectedDelegateHandle);
		}
		if (ConnectionErrorDelegateHandle.IsValid())
		{
			Socket->OnConnectionError().Remove(ConnectionErrorDelegateHandle);
		}
		if (ClosedDelegateHandle.IsValid())
		{
			Socket->OnClosed().Remove(ClosedDelegateHandle);
		}
		if (BinaryMessageDelegateHandle.IsValid())
		{
			Socket->OnBinaryMessage().Remove(BinaryMessageDelegateHandle);
		}
	}

	ConnectedDelegateHandle = FDelegateHandle();
	ConnectionErrorDelegateHandle = FDelegateHandle();
	ClosedDelegateHandle = FDelegateHandle();
	BinaryMessageDelegateHandle = FDelegateHandle();
}

void USimCoreClientComponent::ReleaseSocket(
	bool bCloseConnectedSocket,
	const FString& CloseReason)
{
	if (!Socket.IsValid())
	{
		UnbindSocketDelegates();
		return;
	}

	TSharedPtr<IWebSocket> SocketToRelease = Socket;
	UnbindSocketDelegates();
	Socket.Reset();
	if (bCloseConnectedSocket && SocketToRelease->IsConnected())
	{
		SocketToRelease->Close(1000, CloseReason);
	}
}

float USimCoreClientComponent::ApplyInputDeadzone(float Value) const
{
	return FMath::Abs(Value) < InputDeadzone ? 0.0f : Value;
}

bool USimCoreClientComponent::HasMeaningfulControlChange() const
{
	return !bHasSentControl
		|| !FMath::IsNearlyEqual(
			LastSentControl.Throttle,
			PendingControl.Throttle,
			ControlChangeEpsilon)
		|| !FMath::IsNearlyEqual(
			LastSentControl.Brake,
			PendingControl.Brake,
			ControlChangeEpsilon)
		|| !FMath::IsNearlyEqual(
			LastSentControl.Steering,
			PendingControl.Steering,
			ControlChangeEpsilon)
		|| LastSentControl.bHandbrake != PendingControl.bHandbrake
		|| LastSentControl.Gear != PendingControl.Gear;
}

bool USimCoreClientComponent::ShouldSendControl() const
{
	if (!IsConnected() || !bMapHandshakeComplete) return false;
	const float HeartbeatInterval = 1.0f / FMath::Max(CommandRateHz, 1.0f);
	const float ChangedInputInterval = 1.0f / FMath::Max(MaxChangedCommandRateHz, 1.0f);
	return (bControlDirty && SendAccumulator >= ChangedInputInterval)
		|| SendAccumulator >= HeartbeatInterval;
}

void USimCoreClientComponent::SendControl()
{
	if (!IsConnected() || !bMapHandshakeComplete) return;
	PendingControl.ClientTimeNs = static_cast<uint64>(
		FPlatformTime::Seconds() * 1'000'000'000.0);
	const TArray<uint8> Message = SimCoreProtocol::SerializeControlEnvelope(
		PendingControl,
		OutgoingSequence++,
		SourceId,
		SessionId,
		MapPackageChecksum);
	Socket->Send(Message.GetData(), Message.Num(), true);
	LastSentControl = PendingControl;
	bHasSentControl = true;
	bControlDirty = false;
	SendAccumulator = 0.0f;
}

void USimCoreClientComponent::SendSimulationReset()
{
	if (!IsConnected() || !bMapHandshakeComplete || PlaySessionId.IsEmpty()) return;
	const uint64 ClientTimeNs = static_cast<uint64>(
		FPlatformTime::Seconds() * 1'000'000'000.0);
	const TArray<uint8> Message = SimCoreProtocol::SerializeSimulationResetEnvelope(
		PlaySessionId,
		ClientTimeNs,
		SelectedVehicleClass,
		OutgoingSequence++,
		SourceId,
		SessionId,
		MapPackageChecksum);
	Socket->Send(Message.GetData(), Message.Num(), true);
	UE_LOG(
		LogSimCoreClient,
		Log,
		TEXT("Requested simulation reset for play_session=%s connection_session=%s vehicle=%d"),
		*PlaySessionId,
		*SessionId,
		static_cast<int32>(SelectedVehicleClass));
}

void USimCoreClientComponent::SendHello()
{
	if (!Socket.IsValid() || !Socket->IsConnected()) return;
	const TArray<FString> Capabilities{
		TEXT("world-state.v2"),
		TEXT("control.v2"),
		TEXT("simulation-reset.v1"),
		TEXT("player-vehicle-selection.v1"),
		TEXT("map-package-checksum.v1"),
		TEXT("ground-heightfield.v1"),
		TEXT("runtime-entities.v1"),
		TEXT("traffic-signals.v1"),
		TEXT("pedestrian-signals.v1"),
	};
	const TArray<uint8> Message = SimCoreProtocol::SerializeHelloEnvelope(
		OutgoingSequence++,
		SourceId,
		SessionId,
		MapPackageChecksum,
		TEXT("drive-integration-ue56-r1"),
		Capabilities);
	Socket->Send(Message.GetData(), Message.Num(), true);
}

bool USimCoreClientComponent::ValidateServerHello(
	const SimCoreProtocol::FHelloInfo& Hello,
	FString& OutError) const
{
	OutError.Reset();
	if (Hello.Sequence == 0
		|| Hello.Build.IsEmpty()
		|| Hello.Build.Len() > 128
		|| Hello.SourceId.IsEmpty()
		|| Hello.MapPackageChecksum != MapPackageChecksum
		|| Hello.Schema != SimCoreProtocol::SchemaName)
	{
		OutError = TEXT("server Hello identity, schema, or map checksum is invalid");
		return false;
	}

	TSet<FString> UniqueCapabilities;
	for (const FString& Capability : Hello.Capabilities)
	{
		if (Capability.IsEmpty()
			|| Capability.Len() > 128
			|| UniqueCapabilities.Contains(Capability))
		{
			OutError = TEXT("server Hello contains an invalid or duplicate capability");
			return false;
		}
		UniqueCapabilities.Add(Capability);
	}
	const TCHAR* RequiredCapabilities[] = {
		TEXT("world-state.v2"),
		TEXT("control.v2"),
		TEXT("simulation-reset.v1"),
		TEXT("player-vehicle-selection.v1"),
		TEXT("map-package-checksum.v1"),
	};
	for (const TCHAR* Required : RequiredCapabilities)
	{
		if (!UniqueCapabilities.Contains(Required))
		{
			OutError = FString::Printf(
				TEXT("server Hello is missing required capability %s"),
				Required);
			return false;
		}
	}
	return true;
}

void USimCoreClientComponent::ApplyConnected(uint64 Generation)
{
	if (!IsCurrentSocketGeneration(Generation)
		|| ConnectionState != ESimCoreConnectionState::Connecting)
	{
		return;
	}

	SetConnectionState(ESimCoreConnectionState::Handshaking);
	SendHello();
	UE_LOG(
		LogSimCoreClient,
		Log,
		TEXT("Transport connected to %s session=%s; Hello sent, waiting for server capabilities"),
		*ServerUrl,
		*SessionId);
}

void USimCoreClientComponent::ApplyConnectionError(
	uint64 Generation,
	const FString& Error)
{
	if (!IsCurrentSocketGeneration(Generation)) return;
	UE_LOG(LogSimCoreClient, Error, TEXT("Connection failed: %s"), *Error);
	ScheduleReconnect();
}

void USimCoreClientComponent::ApplyClosed(
	uint64 Generation,
	int32 StatusCode,
	const FString& Reason,
	bool bWasClean)
{
	if (!IsCurrentSocketGeneration(Generation)) return;
	UE_LOG(
		LogSimCoreClient,
		Warning,
		TEXT("Connection closed (%d, clean=%s): %s"),
		StatusCode,
		bWasClean ? TEXT("true") : TEXT("false"),
		*Reason);
	ScheduleReconnect();
}

void USimCoreClientComponent::ApplyBinaryMessage(
	uint64 Generation,
	TArray<uint8> Fragment,
	bool bIsLastFragment,
	bool bFragmentTooLarge)
{
	if (!IsCurrentSocketGeneration(Generation)
		|| (ConnectionState != ESimCoreConnectionState::Handshaking
			&& ConnectionState != ESimCoreConnectionState::Connected))
	{
		return;
	}

	if (bDiscardIncomingMessage)
	{
		if (bIsLastFragment)
		{
			bDiscardIncomingMessage = false;
		}
		return;
	}

	const bool bMessageTooLarge = bFragmentTooLarge
		|| IncomingMessage.Num() > MaxIncomingMessageBytes
		|| Fragment.Num() > MaxIncomingMessageBytes - IncomingMessage.Num();
	if (bMessageTooLarge)
	{
		InvalidateTrafficSignals();
		UE_LOG(LogSimCoreClient, Error, TEXT("Rejected oversized SimCore message"));
		IncomingMessage.Reset();
		bDiscardIncomingMessage = !bIsLastFragment;
		return;
	}

	IncomingMessage.Append(Fragment);
	if (!bIsLastFragment) return;

	TArray<uint8> CompleteMessage = MoveTemp(IncomingMessage);
	IncomingMessage.Reset();
	SimCoreProtocol::FHelloInfo Hello;
	bool bIsHello = false;
	FString Error;
	if (!SimCoreProtocol::TryParseHelloEnvelope(
		CompleteMessage,
		Hello,
		bIsHello,
		Error))
	{
		InvalidateTrafficSignals();
		UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored SimCore packet: %s"), *Error);
		if (Error.StartsWith(TEXT("Schema version")))
		{
			bAutoReconnectEnabled = false;
			SetConnectionState(ESimCoreConnectionState::Incompatible);
			++SocketGeneration;
			ReleaseSocket(true, TEXT("Incompatible SimCore protocol identity"));
			ResetReceivedState();
		}
		return;
	}
	if (bIsHello)
	{
		if (!ValidateServerHello(Hello, Error))
		{
			UE_LOG(LogSimCoreClient, Error, TEXT("Server Hello rejected: %s"), *Error);
			bAutoReconnectEnabled = false;
			SetConnectionState(ESimCoreConnectionState::Incompatible);
			++SocketGeneration;
			ReleaseSocket(true, TEXT("Incompatible SimCore capabilities"));
			ResetReceivedState();
			return;
		}
		bProtocolHandshakeComplete = true;
		UE_LOG(
			LogSimCoreClient,
			Log,
			TEXT("Server Hello accepted build=%s capabilities=%d"),
			*Hello.Build,
			Hello.Capabilities.Num());
		return;
	}
	if (!bProtocolHandshakeComplete)
	{
		UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored state before server Hello"));
		return;
	}

	SimCoreProtocol::FVehicleState Parsed;
	TArray<SimCoreProtocol::FVehicleState> ParsedEntities;
	if (!SimCoreProtocol::ParseWorldStateEnvelope(
		CompleteMessage,
		static_cast<uint32>(FMath::Max(ControlledEntityId, 1)),
		Parsed,
		ParsedEntities,
		Error))
	{
		InvalidateTrafficSignals();
		UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored SimCore packet: %s"), *Error);
		if (Error.StartsWith(TEXT("Schema version"))
			|| Error.StartsWith(TEXT("WorldState is missing a valid map package checksum")))
		{
			bAutoReconnectEnabled = false;
			SetConnectionState(ESimCoreConnectionState::Incompatible);
			++SocketGeneration;
			ReleaseSocket(true, TEXT("Incompatible SimCore protocol identity"));
			ResetReceivedState();
		}
		return;
	}

	if (Parsed.MapPackageChecksum != MapPackageChecksum)
	{
		UE_LOG(
			LogSimCoreClient,
			Error,
			TEXT("MapPackage mismatch; control is disabled. local=%s server=%s directory=%s"),
			*MapPackageChecksum,
			*Parsed.MapPackageChecksum,
			*MapPackageDirectory);
		bAutoReconnectEnabled = false;
		SetConnectionState(ESimCoreConnectionState::Incompatible);
		++SocketGeneration;
		ReleaseSocket(true, TEXT("MapPackage checksum mismatch"));
		ResetReceivedState();
		return;
	}

	if (!bMapHandshakeComplete)
	{
		bMapHandshakeComplete = true;
		SetConnectionState(ESimCoreConnectionState::Connected);
		UE_LOG(
			LogSimCoreClient,
			Log,
			TEXT("MapPackage handshake complete checksum=%s"),
			*MapPackageChecksum);
		// A reconnect repeats the same play-session reset. The host deduplicates it
		// by source/play-session, while ordinary control remains reset-gated.
		SendSimulationReset();
		SendControl();
	}

	// An EStop can predate this PIE and reject its SimulationReset. Cache only
	// that host-wide status before the play fence, using the verified socket/map
	// and monotonic sequence. No cross-play pose or Active status is accepted.
	const double ArrivalTimeSeconds = FPlatformTime::Seconds();
	if (!GlobalEstopHealthCache.Observe(
		Parsed, Generation, SocketGeneration, bProtocolHandshakeComplete,
		MapPackageChecksum, ArrivalTimeSeconds))
	{
		++DroppedOutOfOrderStateCount;
		return;
	}

	// The host can publish its previous authoritative snapshot immediately after
	// WebSocket accept, before this PIE's SimulationReset is processed. Only show
	// snapshots echoed for the current play lifetime. A reconnect within the same
	// PIE keeps PlaySessionId, so its matching snapshots remain valid.
	if (Parsed.PlaySessionId.IsEmpty() || Parsed.PlaySessionId != PlaySessionId)
	{
		InvalidateTrafficSignals();
		UE_LOG(
			LogSimCoreClient,
			Verbose,
			TEXT("Ignored WorldState for play_session=%s; expected=%s"),
			*Parsed.PlaySessionId,
			*PlaySessionId);
		return;
	}

	if (bHasState && Parsed.Sequence <= LatestState.Sequence)
	{
		++DroppedOutOfOrderStateCount;
		return;
	}
	if (bHasState)
	{
		const uint64 SequenceDelta = Parsed.Sequence - LatestState.Sequence;
		if (SequenceDelta > 1)
		{
			MissingStateSequenceCount += SequenceDelta - 1;
		}
	}

	if (LastStateArrivalTimeSeconds > 0.0)
	{
		MaxStateIntervalSeconds = FMath::Max(
			MaxStateIntervalSeconds,
			ArrivalTimeSeconds - LastStateArrivalTimeSeconds);
	}
	const FDateTime UnixEpoch(1970, 1, 1);
	const double UnixNowSeconds = (FDateTime::UtcNow() - UnixEpoch).GetTotalSeconds();
	LatestStateWallAgeMs = (UnixNowSeconds - Parsed.Timestamp) * 1000.0;
	LastStateArrivalTimeSeconds = ArrivalTimeSeconds;
	PerformanceCapture.ObserveAcceptedState(ArrivalTimeSeconds, Parsed.Sequence);
	++ReceivedStateCount;
	LatestState = MoveTemp(Parsed);
	LatestStateReceiveTimeSeconds = ArrivalTimeSeconds;
	bHasState = true;
	bTrafficSnapshotAccepted = true;
	SyncRuntimeProxyActors(ParsedEntities, ArrivalTimeSeconds);
	TickTrafficSignals();
	TickStructureDamage(0.0f);
}
