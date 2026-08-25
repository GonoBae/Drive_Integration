#include "SimCoreClientComponent.h"

#include "Async/Async.h"
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
}

USimCoreClientComponent::USimCoreClientComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USimCoreClientComponent::BeginPlay()
{
	Super::BeginPlay();
	Connect();
}

void USimCoreClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	TickTelemetry(DeltaTime);
}

void USimCoreClientComponent::Connect()
{
	if (ConnectionState == ESimCoreConnectionState::Connecting
		|| ConnectionState == ESimCoreConnectionState::Connected)
	{
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
	bool bHandbrake)
{
	PendingControl.Throttle = ApplyInputDeadzone(FMath::Clamp(Throttle, 0.0f, 1.0f));
	PendingControl.Brake = ApplyInputDeadzone(FMath::Clamp(Brake, 0.0f, 1.0f));
	PendingControl.Steering = ApplyInputDeadzone(FMath::Clamp(Steering, -1.0f, 1.0f));
	PendingControl.bHandbrake = bHandbrake;
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
	UE_LOG(
		LogSimCoreClient,
		Log,
		TEXT("State telemetry: rate=%.1fHz sequence=%llu server_age=%.1fms local_age=%.1fms max_gap=%.1fms dropped_old=%u"),
		StateRateHz,
		static_cast<unsigned long long>(LatestState.Sequence),
		LatestStateWallAgeMs,
		LocalStateAgeMs,
		MaxStateIntervalSeconds * 1000.0,
		DroppedOutOfOrderStateCount);

	TelemetryAccumulator = FMath::Fmod(TelemetryAccumulator, 1.0f);
	ReceivedStateCount = 0;
	DroppedOutOfOrderStateCount = 0;
	MaxStateIntervalSeconds = 0.0;
}

void USimCoreClientComponent::ResetConnectionSession()
{
	IncomingMessage.Reset();
	bDiscardIncomingMessage = false;
	ResetReceivedState();
	ResetTelemetry();
	ResetControlSession();
}

void USimCoreClientComponent::ResetReceivedState()
{
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
	DroppedOutOfOrderStateCount = 0;
}

void USimCoreClientComponent::ScheduleReconnect()
{
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
	RawMessageDelegateHandle = Socket->OnRawMessage().AddLambda(
		[WeakThis, Generation](const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
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
				BytesRemaining,
				bFragmentTooLarge]() mutable
			{
				if (USimCoreClientComponent* Self = WeakThis.Get())
				{
					Self->ApplyRawMessage(
						Generation,
						MoveTemp(Fragment),
						BytesRemaining,
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
		if (RawMessageDelegateHandle.IsValid())
		{
			Socket->OnRawMessage().Remove(RawMessageDelegateHandle);
		}
	}

	ConnectedDelegateHandle = FDelegateHandle();
	ConnectionErrorDelegateHandle = FDelegateHandle();
	ClosedDelegateHandle = FDelegateHandle();
	RawMessageDelegateHandle = FDelegateHandle();
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
		|| LastSentControl.bHandbrake != PendingControl.bHandbrake;
}

bool USimCoreClientComponent::ShouldSendControl() const
{
	if (!IsConnected()) return false;
	const float HeartbeatInterval = 1.0f / FMath::Max(CommandRateHz, 1.0f);
	const float ChangedInputInterval = 1.0f / FMath::Max(MaxChangedCommandRateHz, 1.0f);
	return (bControlDirty && SendAccumulator >= ChangedInputInterval)
		|| SendAccumulator >= HeartbeatInterval;
}

void USimCoreClientComponent::SendControl()
{
	if (!IsConnected()) return;
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

void USimCoreClientComponent::ApplyConnected(uint64 Generation)
{
	if (!IsCurrentSocketGeneration(Generation)
		|| ConnectionState != ESimCoreConnectionState::Connecting)
	{
		return;
	}

	SetConnectionState(ESimCoreConnectionState::Connected);
	UE_LOG(LogSimCoreClient, Log, TEXT("Connected to %s session=%s"), *ServerUrl, *SessionId);
	SendControl();
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

void USimCoreClientComponent::ApplyRawMessage(
	uint64 Generation,
	TArray<uint8> Fragment,
	SIZE_T BytesRemaining,
	bool bFragmentTooLarge)
{
	if (!IsCurrentSocketGeneration(Generation)
		|| ConnectionState != ESimCoreConnectionState::Connected)
	{
		return;
	}

	if (bDiscardIncomingMessage)
	{
		if (BytesRemaining == 0)
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
		UE_LOG(LogSimCoreClient, Error, TEXT("Rejected oversized SimCore message"));
		IncomingMessage.Reset();
		bDiscardIncomingMessage = BytesRemaining != 0;
		return;
	}

	IncomingMessage.Append(Fragment);
	if (BytesRemaining != 0) return;

	TArray<uint8> CompleteMessage = MoveTemp(IncomingMessage);
	IncomingMessage.Reset();
	SimCoreProtocol::FVehicleState Parsed;
	FString Error;
	if (!SimCoreProtocol::ParseWorldStateEnvelope(
		CompleteMessage,
		ControlledEntityId,
		Parsed,
		Error))
	{
		UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored SimCore packet: %s"), *Error);
		if (Error.StartsWith(TEXT("Schema version")))
		{
			bAutoReconnectEnabled = false;
			SetConnectionState(ESimCoreConnectionState::Incompatible);
			++SocketGeneration;
			ReleaseSocket(true, TEXT("Incompatible SimCore schema version"));
			ResetReceivedState();
		}
		return;
	}

	if (bHasState && Parsed.Sequence <= LatestState.Sequence)
	{
		++DroppedOutOfOrderStateCount;
		return;
	}

	const double ArrivalTimeSeconds = FPlatformTime::Seconds();
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
	++ReceivedStateCount;
	LatestState = MoveTemp(Parsed);
	LatestStateReceiveTimeSeconds = ArrivalTimeSeconds;
	bHasState = true;
}
